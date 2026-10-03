#include "VehicleModelManager.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "SecretProtector.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "VideoManager.h"
#include "VideoSettings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>

QGC_LOGGING_CATEGORY(VehicleModelManagerLog, "VehicleModels.VehicleModelManager")

namespace {

constexpr const char *kFileName = "VehicleModels.json";
constexpr const char *kModelsKey = "models";
constexpr const char *kNameKey = "name";
constexpr const char *kCamerasKey = "cameras";
constexpr const char *kPasswordKey = "password";
constexpr const char *kProtectedSuffix = "Protected";

struct CameraType {
    const char *name;
    const char *exchangeId;     ///< modelName in the exchange format
    const char *mainPath;
    const char *secondaryPath;
};

// Stream paths are the vendors' documented defaults; a camera with a changed channel layout needs the
// URLs edited by hand, which the settings page allows.
constexpr CameraType kCameraTypes[] = {
    {"Generic", "generic", "", ""},
    {"Dahua (DH-IPC-HFW2449T)", "dahua_hfw2449t", "/cam/realmonitor?channel=1&subtype=0", "/cam/realmonitor?channel=1&subtype=1"},
    {"Dahua (DH-IPC-HDW5541TM)", "dahua_hdw5541tm", "/cam/realmonitor?channel=1&subtype=0", "/cam/realmonitor?channel=1&subtype=1"},
    {"UNV (Ultra 265)", "unv_ultra_265", "/cam/realmonitor?channel=1&subtype=0", "/cam/realmonitor?channel=1&subtype=1"},
    {"Hikvision (DS-2CD)", "hikvision_ds2cd", "/Streaming/Channels/101", "/Streaming/Channels/102"},
};

/// Only "unv_ultra_265" is a known identifier of the exchange format; the others are matched loosely so
/// files using a different spelling still land on the right camera type.
QString cameraTypeFromExchangeId(const QString &exchangeId)
{
    for (const CameraType &type : kCameraTypes) {
        if (exchangeId.compare(QLatin1String(type.exchangeId), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(type.name);
        }
    }

    const QString id = exchangeId.toLower();
    if (id.contains(QLatin1String("5541"))) {
        return QString::fromLatin1(kCameraTypes[2].name);
    }
    if (id.contains(QLatin1String("dahua")) || id.contains(QLatin1String("2449"))) {
        return QString::fromLatin1(kCameraTypes[1].name);
    }
    if (id.contains(QLatin1String("unv"))) {
        return QString::fromLatin1(kCameraTypes[3].name);
    }
    if (id.contains(QLatin1String("hik"))) {
        return QString::fromLatin1(kCameraTypes[4].name);
    }
    return QString::fromLatin1(kCameraTypes[0].name);
}

QString exchangeIdFromCameraType(const QString &typeName)
{
    for (const CameraType &type : kCameraTypes) {
        if (typeName == QLatin1String(type.name)) {
            return QString::fromLatin1(type.exchangeId);
        }
    }
    return QString::fromLatin1(kCameraTypes[0].exchangeId);
}

QVariantMap modelFromExchange(const QJsonObject &exchange)
{
    QVariantList cameras;
    const QJsonArray exchangeCameras = exchange.value(QLatin1String("camerasArray")).toArray();
    for (const QJsonValue &cameraValue : exchangeCameras) {
        const QJsonObject camera = cameraValue.toObject();
        cameras.append(QVariantMap{
            {QStringLiteral("type"), cameraTypeFromExchangeId(camera.value(QLatin1String("modelName")).toString())},
            {QStringLiteral("name"), camera.value(QLatin1String("name")).toString()},
            {QStringLiteral("ip"), camera.value(QLatin1String("ip")).toString()},
            {QStringLiteral("user"), camera.value(QLatin1String("login")).toString()},
            {QStringLiteral("password"), camera.value(QLatin1String("password")).toString()},
            {QStringLiteral("mainUrl"), camera.value(QLatin1String("url")).toString()},
            {QStringLiteral("secondaryUrl"), camera.value(QLatin1String("secondaryUrl")).toString()},
            {QStringLiteral("audio"), camera.value(QLatin1String("audioEnabled")).toBool()},
        });
    }

    QVariantList buttons;
    const QJsonArray exchangeButtons = exchange.value(QLatin1String("buttonsArray")).toArray();
    for (const QJsonValue &buttonValue : exchangeButtons) {
        const QJsonObject button = buttonValue.toObject();
        QVariantList functions;
        const QJsonArray exchangeFunctions = button.value(QLatin1String("functionsArray")).toArray();
        for (const QJsonValue &functionValue : exchangeFunctions) {
            const QJsonObject function = functionValue.toObject();
            const bool gpio = function.value(QLatin1String("signal")).toString().compare(QLatin1String("GPIO"), Qt::CaseInsensitive) == 0;
            functions.append(QVariantMap{
                {QStringLiteral("channel"), function.value(QLatin1String("channel")).toInt()},
                {QStringLiteral("kind"), gpio ? QStringLiteral("gpio") : QStringLiteral("pwm")},
                {QStringLiteral("value"), function.value(QLatin1String("value")).toInt()},
            });
        }
        buttons.append(QVariantMap{
            {QStringLiteral("name"), button.value(QLatin1String("name")).toString()},
            {QStringLiteral("functions"), functions},
        });
    }

    return QVariantMap{
        {QString::fromLatin1(kNameKey), exchange.value(QLatin1String("text")).toString().trimmed()},
        {QString::fromLatin1(kCamerasKey), cameras},
        {QStringLiteral("buttons"), buttons},
    };
}

QJsonObject modelToExchange(const QVariantMap &model)
{
    QJsonArray cameras;
    const QVariantList modelCameras = model.value(QLatin1String(kCamerasKey)).toList();
    for (const QVariant &cameraVar : modelCameras) {
        const QVariantMap camera = cameraVar.toMap();
        cameras.append(QJsonObject{
            {QStringLiteral("modelName"), exchangeIdFromCameraType(camera.value(QStringLiteral("type")).toString())},
            {QStringLiteral("name"), camera.value(QStringLiteral("name")).toString()},
            {QStringLiteral("url"), camera.value(QStringLiteral("mainUrl")).toString()},
            {QStringLiteral("secondaryUrl"), camera.value(QStringLiteral("secondaryUrl")).toString()},
            {QStringLiteral("login"), camera.value(QStringLiteral("user")).toString()},
            {QStringLiteral("password"), camera.value(QLatin1String(kPasswordKey)).toString()},
            {QStringLiteral("ip"), camera.value(QStringLiteral("ip")).toString()},
            {QStringLiteral("audioEnabled"), camera.value(QStringLiteral("audio")).toBool()},
        });
    }

    QJsonArray buttons;
    const QVariantList modelButtons = model.value(QStringLiteral("buttons")).toList();
    for (const QVariant &buttonVar : modelButtons) {
        const QVariantMap button = buttonVar.toMap();
        QJsonArray functions;
        const QVariantList buttonFunctions = button.value(QStringLiteral("functions")).toList();
        for (const QVariant &functionVar : buttonFunctions) {
            const QVariantMap function = functionVar.toMap();
            const bool gpio = function.value(QStringLiteral("kind")).toString() == QLatin1String("gpio");
            functions.append(QJsonObject{
                {QStringLiteral("channel"), function.value(QStringLiteral("channel")).toInt()},
                {QStringLiteral("signal"), gpio ? QStringLiteral("GPIO") : QStringLiteral("PWM")},
                {QStringLiteral("value"), function.value(QStringLiteral("value")).toInt()},
            });
        }
        buttons.append(QJsonObject{
            {QStringLiteral("name"), button.value(QStringLiteral("name")).toString()},
            {QStringLiteral("functionsArray"), functions},
        });
    }

    return QJsonObject{
        {QStringLiteral("text"), model.value(QLatin1String(kNameKey)).toString()},
        {QStringLiteral("camerasArray"), cameras},
        {QStringLiteral("buttonsArray"), buttons},
    };
}

/// Swaps the secret camera fields between their in-memory and on-disk form. The stream URLs count as
/// secret because they embed the camera credentials.
QVariantList convertCameraSecrets(const QVariantList &cameras, bool forDisk)
{
    static const QStringList secretKeys = {
        QString::fromLatin1(kPasswordKey),
        QStringLiteral("mainUrl"),
        QStringLiteral("secondaryUrl"),
    };

    QVariantList converted;
    for (const QVariant &cameraVar : cameras) {
        QVariantMap camera = cameraVar.toMap();
        for (const QString &key : secretKeys) {
            const QString protectedKey = key + QLatin1String(kProtectedSuffix);
            if (forDisk) {
                camera[protectedKey] = SecretProtector::protect(camera.value(key).toString());
                camera.remove(key);
            } else {
                camera[key] = SecretProtector::unprotect(camera.value(protectedKey).toString());
                camera.remove(protectedKey);
            }
        }
        converted.append(camera);
    }
    return converted;
}

} // namespace

VehicleModelManager::VehicleModelManager(QObject *parent)
    : QObject(parent)
{
    _load();

    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
                   this, &VehicleModelManager::_activeVehicleChanged);
    (void) connect(this, &VehicleModelManager::modelsChanged, this, &VehicleModelManager::_updateActiveModel);
    _activeVehicleChanged(MultiVehicleManager::instance()->activeVehicle());
}

void VehicleModelManager::_activeVehicleChanged(Vehicle *vehicle)
{
    if (_activeVehicle) {
        (void) disconnect(_activeVehicle->vehicleLinkManager(), nullptr, this, nullptr);
    }

    _activeVehicle = vehicle;
    if (_activeVehicle) {
        (void) connect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::primaryLinkChanged,
                       this, &VehicleModelManager::_updateActiveModel);
    }

    _updateActiveModel();
}

void VehicleModelManager::_updateActiveModel()
{
    QVariantMap activeModel;
    if (_activeVehicle) {
        const SharedLinkInterfacePtr link = _activeVehicle->vehicleLinkManager()->primaryLink().lock();
        if (link && link->linkConfiguration()) {
            activeModel = model(link->linkConfiguration()->vehicleModel());
        }
    }

    if (activeModel == _activeModel) {
        return;
    }
    _activeModel = activeModel;
    emit activeModelChanged();

    _mainCameraIndex = 0;
    _cameraEnabled.clear();
    for (qsizetype i = 0; i < _activeCameras().count(); i++) {
        _cameraEnabled.append(true);
    }
    emit cameraStatesChanged();
    _applyCameraStreams();
}

QVariantList VehicleModelManager::_activeCameras() const
{
    return _activeModel.value(QLatin1String(kCamerasKey)).toList();
}

QVariantList VehicleModelManager::cameraStates() const
{
    QVariantList states;
    const QVariantList cameras = _activeCameras();
    for (qsizetype i = 0; i < cameras.count(); i++) {
        states.append(QVariantMap{
            {QStringLiteral("name"), cameras.at(i).toMap().value(QLatin1String(kNameKey)).toString()},
            {QStringLiteral("enabled"), _cameraEnabled.value(i, true)},
            {QStringLiteral("main"), i == _mainCameraIndex},
        });
    }
    return states;
}

QList<int> VehicleModelManager::auxiliaryCameras() const
{
    QList<int> auxiliary;
    for (qsizetype i = 0; (i < _activeCameras().count()) && (auxiliary.count() < kAuxiliaryCameraCount); i++) {
        if ((i != _mainCameraIndex) && _cameraEnabled.value(i, true)) {
            auxiliary.append(static_cast<int>(i));
        }
    }
    while (auxiliary.count() < kAuxiliaryCameraCount) {
        auxiliary.append(-1);
    }
    return auxiliary;
}

QString VehicleModelManager::mainCameraName() const
{
    return _activeCameras().value(_mainCameraIndex).toMap().value(QLatin1String(kNameKey)).toString();
}

void VehicleModelManager::setSecondaryStream(bool secondaryStream)
{
    if (secondaryStream == _secondaryStream) {
        return;
    }
    _secondaryStream = secondaryStream;
    emit secondaryStreamChanged();
    _applyCameraStreams();
}

void VehicleModelManager::setCameraEnabled(int cameraIndex, bool enabled)
{
    if ((cameraIndex < 0) || (cameraIndex >= _cameraEnabled.count()) || (cameraIndex == _mainCameraIndex) ||
        (_cameraEnabled.at(cameraIndex) == enabled)) {
        return;
    }
    _cameraEnabled[cameraIndex] = enabled;
    emit cameraStatesChanged();
    _applyCameraStreams();
}

void VehicleModelManager::setMainCamera(int cameraIndex)
{
    if ((cameraIndex < 0) || (cameraIndex >= _cameraEnabled.count()) || (cameraIndex == _mainCameraIndex)) {
        return;
    }
    _mainCameraIndex = cameraIndex;
    _cameraEnabled[cameraIndex] = true;
    emit cameraStatesChanged();
    _applyCameraStreams();
}

QString VehicleModelManager::_cameraUri(int cameraIndex) const
{
    const QVariantMap camera = _activeCameras().value(cameraIndex).toMap();
    const QString mainUrl = camera.value(QStringLiteral("mainUrl")).toString();
    const QString secondaryUrl = camera.value(QStringLiteral("secondaryUrl")).toString();
    // A camera with only one stream configured keeps playing it in either mode.
    if (_secondaryStream) {
        return secondaryUrl.isEmpty() ? mainUrl : secondaryUrl;
    }
    return mainUrl.isEmpty() ? secondaryUrl : mainUrl;
}

void VehicleModelManager::_applyCameraStreams()
{
    // Without cameras in the model the user's own video settings are left alone.
    if (_activeCameras().isEmpty()) {
        for (int i = 0; i < kAuxiliaryCameraCount; i++) {
            VideoManager::instance()->setAuxiliaryVideoUri(i, QString());
        }
        return;
    }

    const QString mainUri = _cameraUri(_mainCameraIndex);
    if (!mainUri.isEmpty()) {
        VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings();
        videoSettings->rtspUrl()->setRawValue(mainUri);
        videoSettings->videoSource()->setRawValue(VideoSettings::videoSourceRTSP);
    }

    const QList<int> auxiliary = auxiliaryCameras();
    for (int i = 0; i < kAuxiliaryCameraCount; i++) {
        VideoManager::instance()->setAuxiliaryVideoUri(i, (auxiliary.at(i) < 0) ? QString() : _cameraUri(auxiliary.at(i)));
    }
}

void VehicleModelManager::runButton(const QVariantMap &button) const
{
    if (!_activeVehicle) {
        return;
    }

    const QVariantList functions = button.value(QStringLiteral("functions")).toList();
    for (const QVariant &functionVar : functions) {
        const QVariantMap function = functionVar.toMap();
        const bool gpio = function.value(QStringLiteral("kind")).toString() == QLatin1String("gpio");
        _activeVehicle->sendMavCommand(_activeVehicle->defaultComponentId(),
                                       gpio ? MAV_CMD_DO_SET_RELAY : MAV_CMD_DO_SET_SERVO,
                                       true,
                                       function.value(QStringLiteral("channel")).toFloat(),
                                       function.value(QStringLiteral("value")).toFloat());
    }
}

QStringList VehicleModelManager::modelNames() const
{
    QStringList names;
    for (const QVariant &modelVar : _models) {
        names.append(modelVar.toMap().value(kNameKey).toString());
    }
    return names;
}

QStringList VehicleModelManager::cameraTypes() const
{
    QStringList types;
    for (const CameraType &type : kCameraTypes) {
        types.append(QString::fromLatin1(type.name));
    }
    return types;
}

QVariantMap VehicleModelManager::model(const QString &name) const
{
    const int index = _indexOf(name);
    return (index < 0) ? QVariantMap() : _models.at(index).toMap();
}

QString VehicleModelManager::saveModel(const QString &originalName, const QVariantMap &model)
{
    const QString name = model.value(kNameKey).toString().trimmed();
    if (name.isEmpty()) {
        return tr("Enter a model name.");
    }

    const int originalIndex = _indexOf(originalName);
    const int nameIndex = _indexOf(name);
    if ((nameIndex >= 0) && (nameIndex != originalIndex)) {
        return tr("A model named \"%1\" already exists.").arg(name);
    }

    QVariantMap stored = model;
    stored[kNameKey] = name;

    const QVariantList previous = _models;
    if (originalIndex >= 0) {
        _models[originalIndex] = stored;
    } else {
        _models.append(stored);
    }

    if (!_save()) {
        _models = previous;
        return tr("Could not write the vehicle models file.");
    }

    emit modelsChanged();
    return QString();
}

void VehicleModelManager::deleteModel(const QString &name)
{
    const int index = _indexOf(name);
    if (index < 0) {
        return;
    }

    const QVariantList previous = _models;
    _models.removeAt(index);
    if (!_save()) {
        _models = previous;
        return;
    }

    emit modelsChanged();
}

QVariantMap VehicleModelManager::cameraUrls(const QString &type, const QString &ip, const QString &user,
                                            const QString &password) const
{
    QVariantMap urls = {{QStringLiteral("mainUrl"), QString()}, {QStringLiteral("secondaryUrl"), QString()}};

    for (const CameraType &cameraType : kCameraTypes) {
        if ((type != QString::fromLatin1(cameraType.name)) || (cameraType.mainPath[0] == '\0') || ip.isEmpty()) {
            continue;
        }

        QString credentials;
        if (!user.isEmpty()) {
            // Credentials are percent-encoded so characters like '@' or ':' in a password keep the URL valid.
            credentials = QString::fromLatin1(QUrl::toPercentEncoding(user));
            if (!password.isEmpty()) {
                credentials += QLatin1Char(':') + QString::fromLatin1(QUrl::toPercentEncoding(password));
            }
            credentials += QLatin1Char('@');
        }

        const QString base = QStringLiteral("rtsp://%1%2:554").arg(credentials, ip);
        urls[QStringLiteral("mainUrl")] = base + QString::fromLatin1(cameraType.mainPath);
        urls[QStringLiteral("secondaryUrl")] = base + QString::fromLatin1(cameraType.secondaryPath);
        break;
    }

    return urls;
}

QString VehicleModelManager::importModels(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return tr("Could not open the file: %1").arg(file.errorString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return tr("The file is not valid JSON: %1").arg(parseError.errorString());
    }

    // A file holds either a list of models or a single model object.
    const QJsonArray exchangeModels = document.isArray() ? document.array() : QJsonArray{document.object()};

    const QVariantList previous = _models;
    QStringList importedNames;
    for (const QJsonValue &exchangeValue : exchangeModels) {
        QVariantMap model = modelFromExchange(exchangeValue.toObject());
        const QString name = model.value(QLatin1String(kNameKey)).toString();
        if (name.isEmpty()) {
            continue;
        }
        model[QString::fromLatin1(kNameKey)] = _uniqueName(name);
        _models.append(model);
        importedNames.append(model.value(QLatin1String(kNameKey)).toString());
    }

    if (importedNames.isEmpty()) {
        return tr("No vehicle models found in the file.");
    }
    if (!_save()) {
        _models = previous;
        return tr("Could not write the vehicle models file.");
    }

    emit modelsChanged();
    return tr("Imported: %1").arg(importedNames.join(QStringLiteral(", ")));
}

QString VehicleModelManager::exportModel(const QString &name, const QString &filePath) const
{
    const int index = _indexOf(name);
    if (index < 0) {
        return tr("Save the model before exporting it.");
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return tr("Could not write the file: %1").arg(file.errorString());
    }
    (void) file.write(QJsonDocument(QJsonArray{modelToExchange(_models.at(index).toMap())}).toJson());
    if (!file.commit()) {
        return tr("Could not write the file: %1").arg(file.errorString());
    }
    return QString();
}

QString VehicleModelManager::_uniqueName(const QString &name) const
{
    QString candidate = name;
    for (int suffix = 2; _indexOf(candidate) >= 0; suffix++) {
        candidate = QStringLiteral("%1 (%2)").arg(name).arg(suffix);
    }
    return candidate;
}

int VehicleModelManager::_indexOf(const QString &name) const
{
    if (name.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < _models.count(); i++) {
        if (_models.at(i).toMap().value(kNameKey).toString() == name) {
            return i;
        }
    }
    return -1;
}

QString VehicleModelManager::_filePath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QString::fromLatin1(kFileName));
}

void VehicleModelManager::_load()
{
    QFile file(_filePath());
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(VehicleModelManagerLog) << "Unable to open" << file.fileName() << file.errorString();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(VehicleModelManagerLog) << "Unable to parse" << file.fileName() << parseError.errorString();
        return;
    }

    _models.clear();
    const QVariantList stored = document.object().value(QLatin1String(kModelsKey)).toArray().toVariantList();
    for (const QVariant &modelVar : stored) {
        QVariantMap model = modelVar.toMap();
        model[kCamerasKey] = convertCameraSecrets(model.value(kCamerasKey).toList(), false);
        _models.append(model);
    }
}

bool VehicleModelManager::_save() const
{
    QVariantList stored;
    for (const QVariant &modelVar : _models) {
        QVariantMap model = modelVar.toMap();
        model[kCamerasKey] = convertCameraSecrets(model.value(kCamerasKey).toList(), true);
        stored.append(model);
    }

    const QString path = _filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(VehicleModelManagerLog) << "Unable to create directory for" << path;
        return false;
    }

    QJsonObject root;
    root[QLatin1String(kModelsKey)] = QJsonArray::fromVariantList(stored);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(VehicleModelManagerLog) << "Unable to open" << path << file.errorString();
        return false;
    }
    (void) file.write(QJsonDocument(root).toJson());
    if (!file.commit()) {
        qCWarning(VehicleModelManagerLog) << "Unable to write" << path << file.errorString();
        return false;
    }
    return true;
}
