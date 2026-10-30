#include "VehicleModelManager.h"
#include "Joystick.h"
#include "JoystickManager.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "RoomScope.h"
#include "SecretProtector.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "VideoManager.h"
#include "VideoSettings.h"

#include <QtCore/QDateTime>
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

/// Value sent when a button is switched off and none is configured: the other end of the range.
int defaultOffValue(bool gpio, int onValue)
{
    if (gpio) {
        return (onValue == 0) ? 1 : 0;
    }
    return (onValue >= 1500) ? 1000 : 2000;
}

int functionOffValue(const QVariantMap &function)
{
    if (function.contains(QStringLiteral("offValue"))) {
        return function.value(QStringLiteral("offValue")).toInt();
    }
    return defaultOffValue(function.value(QStringLiteral("kind")).toString() == QLatin1String("gpio"),
                           function.value(QStringLiteral("value")).toInt());
}

/// A function writes its off value only when the model asks for it (a button may just ignore the release).
bool functionSendsOff(const QVariantMap &function)
{
    return function.value(QStringLiteral("sendOff"), true).toBool();
}

/// Values of a "steps" button (for example 1000, 1500, 2000 for three speeds): one segment each on screen; a
/// joystick button walks through them.
QList<int> functionSteps(const QVariantMap &function)
{
    QList<int> steps;
    const QVariantList values = function.value(QStringLiteral("steps")).toList();
    for (const QVariant &value : values) {
        steps.append(value.toInt());
    }
    return steps;
}

QString buttonMode(const QVariantMap &button)
{
    const QString mode = button.value(QStringLiteral("mode")).toString();
    if ((mode == QLatin1String("momentary")) || (mode == QLatin1String("steps"))) {
        return mode;
    }
    return QStringLiteral("toggle");
}

int buttonStepCount(const QVariantMap &button)
{
    int count = 0;
    const QVariantList functions = button.value(QStringLiteral("functions")).toList();
    for (const QVariant &functionVar : functions) {
        count = qMax(count, static_cast<int>(functionSteps(functionVar.toMap()).count()));
    }
    return count;
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
            const int value = function.value(QLatin1String("value")).toInt();
            // Nova files have no off value; pick the opposite end of the range.
            const int offValue = function.contains(QLatin1String("offValue")) ? function.value(QLatin1String("offValue")).toInt()
                                                                              : defaultOffValue(gpio, value);
            QVariantList steps;
            const QJsonArray exchangeSteps = function.value(QLatin1String("steps")).toArray();
            for (const QJsonValue &step : exchangeSteps) {
                steps.append(step.toInt());
            }
            functions.append(QVariantMap{
                {QStringLiteral("channel"), function.value(QLatin1String("channel")).toInt()},
                {QStringLiteral("kind"), gpio ? QStringLiteral("gpio") : QStringLiteral("pwm")},
                {QStringLiteral("value"), value},
                {QStringLiteral("offValue"), offValue},
                {QStringLiteral("sendOff"), function.value(QLatin1String("sendOff")).toBool(true)},
                {QStringLiteral("steps"), steps},
            });
        }
        buttons.append(QVariantMap{
            {QStringLiteral("name"), button.value(QLatin1String("name")).toString()},
            {QStringLiteral("mode"), buttonMode(button.toVariantMap())},
            {QStringLiteral("functions"), functions},
        });
    }

    QVariantMap model{
        {QString::fromLatin1(kNameKey), exchange.value(QLatin1String("text")).toString().trimmed()},
        {QString::fromLatin1(kCamerasKey), cameras},
        {QStringLiteral("buttons"), buttons},
    };
    // NX only: files from Nova have no motor telemetry or battery block
    for (const QString &key : {QStringLiteral("motorTelemetry"), QStringLiteral("battery")}) {
        if (exchange.contains(key)) {
            model.insert(key, exchange.value(key).toObject().toVariantMap());
        }
    }
    return model;
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
            QJsonArray steps;
            for (const int step : functionSteps(function)) {
                steps.append(step);
            }
            functions.append(QJsonObject{
                {QStringLiteral("channel"), function.value(QStringLiteral("channel")).toInt()},
                {QStringLiteral("signal"), gpio ? QStringLiteral("GPIO") : QStringLiteral("PWM")},
                {QStringLiteral("value"), function.value(QStringLiteral("value")).toInt()},
                {QStringLiteral("offValue"), functionOffValue(function)},
                {QStringLiteral("sendOff"), functionSendsOff(function)},
                {QStringLiteral("steps"), steps},
            });
        }
        buttons.append(QJsonObject{
            {QStringLiteral("name"), button.value(QStringLiteral("name")).toString()},
            {QStringLiteral("mode"), buttonMode(button)},
            {QStringLiteral("functionsArray"), functions},
        });
    }

    QJsonObject exchange{
        {QStringLiteral("text"), model.value(QLatin1String(kNameKey)).toString()},
        {QStringLiteral("camerasArray"), cameras},
        {QStringLiteral("buttonsArray"), buttons},
    };
    for (const QString &key : {QStringLiteral("motorTelemetry"), QStringLiteral("battery")}) {
        if (model.contains(key)) {
            exchange.insert(key, QJsonObject::fromVariantMap(model.value(key).toMap()));
        }
    }
    return exchange;
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

VehicleModelManager *VehicleModelManager::_instance = nullptr;

VehicleModelManager::VehicleModelManager(QObject *parent)
    : QObject(parent)
{
    _instance = this;
    _load();

    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
                   this, &VehicleModelManager::_activeVehicleChanged);
    // Joystick buttons can be assigned to the model buttons and to the HD / SD switch
    (void) connect(JoystickManager::instance(), &JoystickManager::activeJoystickChanged,
                   this, &VehicleModelManager::_activeJoystickChanged);
    _activeJoystickChanged(JoystickManager::instance()->activeJoystick());
    (void) connect(this, &VehicleModelManager::modelsChanged, this, &VehicleModelManager::_updateActiveModel);
    // Each room has its own models: the lists change with the room the settings show
    _scope = RoomScope::current();
    RoomScope::notifyChanges(this, [this]() {
        const QString scope = RoomScope::current();
        if (scope != _scope) {
            _scope = scope;
            emit modelsChanged();
        }
    });
    // A high-definition stream can take a long time to start; after that the old picture is let go anyway.
    _switchTimer.setSingleShot(true);
    _switchTimer.setInterval(25000);
    (void) connect(&_switchTimer, &QTimer::timeout, this, &VehicleModelManager::_endStreamSwitch);
    _activeVehicleChanged(MultiVehicleManager::instance()->activeVehicle());
}

void VehicleModelManager::_activeVehicleChanged(Vehicle *vehicle)
{
    if (_activeVehicle) {
        (void) disconnect(_activeVehicle->vehicleLinkManager(), nullptr, this, nullptr);
        (void) disconnect(_activeVehicle, &Vehicle::servoOutputsChanged, this, &VehicleModelManager::_servoOutputsChanged);
    }

    _activeVehicle = vehicle;
    if (_activeVehicle) {
        (void) connect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::primaryLinkChanged,
                       this, &VehicleModelManager::_updateActiveModel);
        // A button with several values shows the one the vehicle has on its output (e.g. 1500 after start)
        (void) connect(_activeVehicle, &Vehicle::servoOutputsChanged, this, &VehicleModelManager::_servoOutputsChanged);
    }

    _updateActiveModel();
}

void VehicleModelManager::_updateActiveModel()
{
    QVariantMap activeModel;
    if (_activeVehicle) {
        const SharedLinkInterfacePtr link = _activeVehicle->vehicleLinkManager()->primaryLink().lock();
        if (link && link->linkConfiguration()) {
            activeModel = modelForLink(link->linkConfiguration()->vehicleModel(), link->linkConfiguration()->room());
        }
    }

    if (activeModel == _activeModel) {
        return;
    }
    _activeModel = activeModel;
    // A different vehicle starts with all its buttons shown as off, and no value of a several-value button chosen
    _buttonStates.clear();
    _buttonSteps.clear();
    _stepChosenAt.clear();
    emit activeModelChanged();
    emit buttonStatesChanged();

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
            VideoManager::instance()->setAuxiliaryVideo(i, QString(), false);
        }
        return;
    }

    const auto cameraHasAudio = [this](int cameraIndex) {
        return (cameraIndex >= 0) && _activeCameras().value(cameraIndex).toMap().value(QStringLiteral("audio")).toBool();
    };

    VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings();
    const QString mainUri = _cameraUri(_mainCameraIndex);
    // Changing the URL below restarts the main stream, which then picks the audio setting up by itself.
    const bool mainUrlChanges = !mainUri.isEmpty() && (videoSettings->rtspUrl()->rawValue().toString() != mainUri);
    if (mainUrlChanges && VideoManager::instance()->decoding()) {
        _beginStreamSwitch();
    }
    VideoManager::instance()->setMainStreamAudio(cameraHasAudio(_mainCameraIndex), !mainUrlChanges);
    if (!mainUri.isEmpty()) {
        videoSettings->rtspUrl()->setRawValue(mainUri);
        videoSettings->videoSource()->setRawValue(VideoSettings::videoSourceRTSP);
    }

    const QList<int> auxiliary = auxiliaryCameras();
    for (int i = 0; i < kAuxiliaryCameraCount; i++) {
        const int cameraIndex = auxiliary.at(i);
        VideoManager::instance()->setAuxiliaryVideo(i, (cameraIndex < 0) ? QString() : _cameraUri(cameraIndex),
                                                    cameraHasAudio(cameraIndex));
    }
}

void VehicleModelManager::_beginStreamSwitch()
{
    // The old picture stops first and the new one starts afterwards; the switch ends at that start.
    _switchSawStop = false;
    (void) connect(VideoManager::instance(), &VideoManager::decodingChanged, this, &VehicleModelManager::_decodingChanged,
                   Qt::UniqueConnection);
    _switchTimer.start();
    if (!_switchingStream) {
        _switchingStream = true;
        emit switchingStreamChanged();
    }
}

void VehicleModelManager::_decodingChanged()
{
    if (!_switchingStream) {
        return;
    }
    if (!VideoManager::instance()->decoding()) {
        _switchSawStop = true;
    } else if (_switchSawStop) {
        _endStreamSwitch();
    }
}

void VehicleModelManager::_endStreamSwitch()
{
    _switchTimer.stop();
    _switchSawStop = false;
    if (_switchingStream) {
        _switchingStream = false;
        emit switchingStreamChanged();
    }
}

void VehicleModelManager::_sendFunction(const QVariantMap &function, int value)
{
    const bool gpio = function.value(QStringLiteral("kind")).toString() == QLatin1String("gpio");
    _activeVehicle->sendMavCommand(_activeVehicle->defaultComponentId(),
                                   gpio ? MAV_CMD_DO_SET_RELAY : MAV_CMD_DO_SET_SERVO,
                                   true,
                                   function.value(QStringLiteral("channel")).toFloat(),
                                   static_cast<float>(value));
}

void VehicleModelManager::_setButtonState(int buttonIndex, bool on, int step)
{
    while (_buttonStates.count() < (buttonIndex + 1)) {
        _buttonStates.append(false);
    }
    while (_buttonSteps.count() < (buttonIndex + 1)) {
        _buttonSteps.append(-1);
    }
    _buttonStates[buttonIndex] = on;
    _buttonSteps[buttonIndex] = step;
    emit buttonStatesChanged();
}

void VehicleModelManager::_activeJoystickChanged(Joystick *joystick)
{
    if (_joystick) {
        (void) disconnect(_joystick, nullptr, this, nullptr);
    }
    _joystick = joystick;
    if (!_joystick) {
        return;
    }
    (void) connect(_joystick, &Joystick::modelButton, this, &VehicleModelManager::_joystickModelButton);
    (void) connect(_joystick, &Joystick::toggleVideoQuality, this, [this]() { setSecondaryStream(!_secondaryStream); });
}

void VehicleModelManager::_joystickModelButton(int buttonIndex, bool down)
{
    // Each call only acts on the button mode it belongs to: a press-and-hold button follows the joystick
    // button, the others switch on the press.
    if (down) {
        pressButton(buttonIndex);
        toggleButton(buttonIndex);
    } else {
        releaseButton(buttonIndex);
    }
}

void VehicleModelManager::toggleButton(int buttonIndex)
{
    const QVariantList buttons = _activeModel.value(QStringLiteral("buttons")).toList();
    if (!_activeVehicle || (buttonIndex < 0) || (buttonIndex >= buttons.count())) {
        return;
    }

    const QVariantMap button = buttons.at(buttonIndex).toMap();
    const QString mode = buttonMode(button);
    if (mode == QLatin1String("momentary")) {
        return;     // driven by pressButton()/releaseButton()
    }

    const QVariantList functions = button.value(QStringLiteral("functions")).toList();

    if (mode == QLatin1String("steps")) {
        // A joystick button walks to the next value; on screen each value has its own segment (selectStep)
        const int stepCount = buttonStepCount(button);
        if (stepCount < 1) {
            return;
        }
        const int current = (buttonIndex < _buttonSteps.count()) ? _buttonSteps.at(buttonIndex).toInt() : -1;
        selectStep(buttonIndex, (current + 1) % stepCount);
        return;
    }

    const bool on = !((buttonIndex < _buttonStates.count()) && _buttonStates.at(buttonIndex).toBool());
    for (const QVariant &functionVar : functions) {
        const QVariantMap function = functionVar.toMap();
        if (on) {
            _sendFunction(function, function.value(QStringLiteral("value")).toInt());
        } else if (functionSendsOff(function)) {
            _sendFunction(function, functionOffValue(function));
        }
    }
    _setButtonState(buttonIndex, on, 0);
}

void VehicleModelManager::selectStep(int buttonIndex, int step)
{
    const QVariantList buttons = _activeModel.value(QStringLiteral("buttons")).toList();
    if (!_activeVehicle || (buttonIndex < 0) || (buttonIndex >= buttons.count())) {
        return;
    }
    const QVariantMap button = buttons.at(buttonIndex).toMap();
    if ((buttonMode(button) != QLatin1String("steps")) || (step < 0) || (step >= buttonStepCount(button))) {
        return;
    }
    const QVariantList functions = button.value(QStringLiteral("functions")).toList();
    for (const QVariant &functionVar : functions) {
        const QVariantMap function = functionVar.toMap();
        const QList<int> steps = functionSteps(function);
        if (!steps.isEmpty()) {
            _sendFunction(function, steps.at(qMin(step, static_cast<int>(steps.count()) - 1)));
        }
    }
    _stepChosenAt[buttonIndex] = QDateTime::currentMSecsSinceEpoch();
    _setButtonState(buttonIndex, step != 0, step);
}

void VehicleModelManager::_servoOutputsChanged(const QVector<int> &values)
{
    // Each several-value button follows the first of its PWM outputs that the vehicle reports: the value
    // within kStepTolerance of a step lights that step, a value between steps lights none
    const QVariantList buttons = _activeModel.value(QStringLiteral("buttons")).toList();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (int buttonIndex = 0; buttonIndex < buttons.count(); buttonIndex++) {
        const QVariantMap button = buttons.at(buttonIndex).toMap();
        if (buttonMode(button) != QLatin1String("steps")) {
            continue;
        }
        if ((now - _stepChosenAt.value(buttonIndex, 0)) < kStepReportDelayMs) {
            continue;
        }
        const QVariantList functions = button.value(QStringLiteral("functions")).toList();
        for (const QVariant &functionVar : functions) {
            const QVariantMap function = functionVar.toMap();
            const int channel = function.value(QStringLiteral("channel")).toInt();
            const QList<int> steps = functionSteps(function);
            if ((function.value(QStringLiteral("kind")).toString() == QLatin1String("gpio")) || steps.isEmpty() ||
                (channel < 1) || (channel > values.count()) || (values.at(channel - 1) < 0)) {
                continue;
            }
            const int output = values.at(channel - 1);
            int step = -1;
            int distance = kStepTolerance + 1;
            for (int i = 0; i < steps.count(); i++) {
                if (qAbs(steps.at(i) - output) < distance) {
                    distance = qAbs(steps.at(i) - output);
                    step = i;
                }
            }
            while (_buttonSteps.count() < (buttonIndex + 1)) {
                _buttonSteps.append(-1);
            }
            while (_buttonStates.count() < (buttonIndex + 1)) {
                _buttonStates.append(false);
            }
            if (_buttonSteps.at(buttonIndex).toInt() != step) {
                _buttonSteps[buttonIndex] = step;
                _buttonStates[buttonIndex] = step > 0;
                changed = true;
            }
            break;
        }
    }
    if (changed) {
        emit buttonStatesChanged();
    }
}

void VehicleModelManager::pressButton(int buttonIndex)
{
    const QVariantList buttons = _activeModel.value(QStringLiteral("buttons")).toList();
    if (!_activeVehicle || (buttonIndex < 0) || (buttonIndex >= buttons.count())) {
        return;
    }
    const QVariantMap button = buttons.at(buttonIndex).toMap();
    if (buttonMode(button) != QLatin1String("momentary")) {
        return;
    }
    const QVariantList functions = button.value(QStringLiteral("functions")).toList();
    for (const QVariant &functionVar : functions) {
        const QVariantMap function = functionVar.toMap();
        _sendFunction(function, function.value(QStringLiteral("value")).toInt());
    }
    _setButtonState(buttonIndex, true, 0);
}

void VehicleModelManager::releaseButton(int buttonIndex)
{
    const QVariantList buttons = _activeModel.value(QStringLiteral("buttons")).toList();
    if (!_activeVehicle || (buttonIndex < 0) || (buttonIndex >= buttons.count())) {
        return;
    }
    const QVariantMap button = buttons.at(buttonIndex).toMap();
    if ((buttonMode(button) != QLatin1String("momentary"))
        || !((buttonIndex < _buttonStates.count()) && _buttonStates.at(buttonIndex).toBool())) {
        return;
    }
    const QVariantList functions = button.value(QStringLiteral("functions")).toList();
    for (const QVariant &functionVar : functions) {
        const QVariantMap function = functionVar.toMap();
        if (functionSendsOff(function)) {
            _sendFunction(function, functionOffValue(function));
        }
    }
    _setButtonState(buttonIndex, false, 0);
}

QStringList VehicleModelManager::modelNames() const
{
    QStringList names;
    const QString scope = RoomScope::current();
    for (const QVariant &modelVar : _models) {
        const QVariantMap model = modelVar.toMap();
        if (model.value(QLatin1String(RoomScope::kRoomKey)).toString() == scope) {
            names.append(model.value(kNameKey).toString());
        }
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
    const int index = _indexOf(name, RoomScope::current());
    if (index < 0) {
        return _roomModels.value(name).toMap();
    }
    return _models.at(index).toMap();
}

QVariantMap VehicleModelManager::modelForLink(const QString &name, const QString &room) const
{
    if (name.startsWith(QLatin1String("room:"))) {
        return _roomModels.value(name).toMap();
    }
    int index = _indexOf(name, room);
    // A link whose room has no model of that name (e.g. moved by an older version) still finds one
    for (int i = 0; (index < 0) && (i < _models.count()); i++) {
        if (!name.isEmpty() && (_models.at(i).toMap().value(kNameKey).toString() == name)) {
            index = i;
        }
    }
    return (index < 0) ? QVariantMap() : _models.at(index).toMap();
}

QString VehicleModelManager::copyModelToRoom(const QString &name, const QString &fromRoom, const QString &toRoom)
{
    if ((fromRoom == toRoom) || (_indexOf(name, toRoom) >= 0)) {
        return QString();
    }
    const int index = _indexOf(name, fromRoom);
    if (index < 0) {
        return QString();
    }
    QVariantMap copy = _models.at(index).toMap();
    copy[QLatin1String(RoomScope::kRoomKey)] = toRoom;
    _models.append(copy);
    if (!_save()) {
        _models.removeLast();
        return tr("Could not write the vehicle models file.");
    }
    emit modelsChanged();
    return QString();
}

void VehicleModelManager::setRoomModels(const QVariantMap &models)
{
    if (models == _roomModels) {
        return;
    }
    _roomModels = models;
    _updateActiveModel();
}

QString VehicleModelManager::saveModel(const QString &originalName, const QVariantMap &model)
{
    const QString name = model.value(kNameKey).toString().trimmed();
    if (name.isEmpty()) {
        return tr("Enter a model name.");
    }

    const QString scope = RoomScope::current();
    const int originalIndex = _indexOf(originalName, scope);
    const int nameIndex = _indexOf(name, scope);
    if ((nameIndex >= 0) && (nameIndex != originalIndex)) {
        return tr("A model named \"%1\" already exists.").arg(name);
    }

    QVariantMap stored = model;
    stored[kNameKey] = name;
    stored[QLatin1String(RoomScope::kRoomKey)] = scope;

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
    const int index = _indexOf(name, RoomScope::current());
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

void VehicleModelManager::moveModel(int from, int to)
{
    // from and to count the models of the current scope, as listed
    QList<int> listed;
    const QString scope = RoomScope::current();
    for (int i = 0; i < _models.count(); i++) {
        if (_models.at(i).toMap().value(QLatin1String(RoomScope::kRoomKey)).toString() == scope) {
            listed.append(i);
        }
    }
    if ((from == to) || (from < 0) || (to < 0) || (from >= listed.count()) || (to >= listed.count())) {
        return;
    }

    const QVariantList previous = _models;
    _models.move(listed.at(from), listed.at(to));
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

bool VehicleModelManager::isCustomCameraUrl(const QString &url) const
{
    if (url.trimmed().isEmpty()) {
        return false;
    }

    const QUrl parsed(url.trimmed());
    if ((parsed.scheme().compare(QLatin1String("rtsp"), Qt::CaseInsensitive) != 0) ||
        ((parsed.port() != -1) && (parsed.port() != 554))) {
        return true;
    }

    const QString path = parsed.path() + (parsed.hasQuery() ? (QLatin1Char('?') + parsed.query()) : QString());
    for (const CameraType &cameraType : kCameraTypes) {
        if ((cameraType.mainPath[0] != '\0') &&
            ((path == QLatin1String(cameraType.mainPath)) || (path == QLatin1String(cameraType.secondaryPath)))) {
            return false;
        }
    }
    return true;
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
    const QString scope = RoomScope::current();
    QStringList importedNames;
    for (const QJsonValue &exchangeValue : exchangeModels) {
        QVariantMap model = modelFromExchange(exchangeValue.toObject());
        const QString name = model.value(QLatin1String(kNameKey)).toString();
        if (name.isEmpty()) {
            continue;
        }
        model[QString::fromLatin1(kNameKey)] = _uniqueName(name, scope);
        model[QLatin1String(RoomScope::kRoomKey)] = scope;
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
    const int index = _indexOf(name, RoomScope::current());
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

QString VehicleModelManager::_uniqueName(const QString &name, const QString &room) const
{
    QString candidate = name;
    for (int suffix = 2; _indexOf(candidate, room) >= 0; suffix++) {
        candidate = QStringLiteral("%1 (%2)").arg(name).arg(suffix);
    }
    return candidate;
}

int VehicleModelManager::_indexOf(const QString &name, const QString &room) const
{
    if (name.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < _models.count(); i++) {
        const QVariantMap model = _models.at(i).toMap();
        if ((model.value(kNameKey).toString() == name) && (model.value(QLatin1String(RoomScope::kRoomKey)).toString() == room)) {
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
    // Models saved before rooms had their own go to the rooms of the links using them
    if (RoomScope::tagUntagged(_models, QStringLiteral("vehicle_model"))) {
        (void) _save();
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
