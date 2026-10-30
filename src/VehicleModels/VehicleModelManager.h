#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include "Vehicle.h"

class Joystick;

Q_DECLARE_LOGGING_CATEGORY(VehicleModelManagerLog)


/// Stores per-vehicle profiles ("models"): the cameras on board and the on-screen buttons that drive servo
/// or relay outputs. Models are plain maps so the settings page can edit a copy and save it back whole:
///
///     { name, cameras: [{ type, name, ip, user, password, mainUrl, secondaryUrl, audio }],
///             buttons: [{ name, functions: [{ channel, kind: "pwm"|"gpio", value }] }],
///             motorTelemetry: { enabled, leftEsc, rightEsc, showTemperature, tempWarn, tempCrit, currentWarn, currentCrit },
///             battery: { enabled, emptyVoltage, fullVoltage } }
///
/// battery, when enabled, sets the charge shown on the main screen from the voltage (empty = 0 %, full = 100 %) in
/// place of the values entered in the battery menu.
///
/// Camera passwords and stream URLs (which embed the credentials) are encrypted on disk, see SecretProtector.
class VehicleModelManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList models      READ models         NOTIFY modelsChanged)
    Q_PROPERTY(QStringList  modelNames  READ modelNames     NOTIFY modelsChanged)
    Q_PROPERTY(QStringList  cameraTypes READ cameraTypes    CONSTANT)
    Q_PROPERTY(QVariantMap  activeModel READ activeModel    NOTIFY activeModelChanged)
    /// Cameras of the active model with their display state: [{ name, enabled, main }]
    Q_PROPERTY(QVariantList cameraStates        READ cameraStates       NOTIFY cameraStatesChanged)
    /// Camera index shown by each auxiliary video item, -1 for an unused item
    Q_PROPERTY(QList<int>   auxiliaryCameras    READ auxiliaryCameras   NOTIFY cameraStatesChanged)
    /// true: every camera plays its secondary (SD) stream instead of the main (HD) one
    /// On/off state of each button of the active model, by index
    Q_PROPERTY(QVariantList buttonStates        READ buttonStates       NOTIFY buttonStatesChanged)
    /// Value chosen on each button of the active model that has several values ("steps" mode), by index: the
    /// position of the value the vehicle reports on its output, else the one last chosen here; -1 while unknown
    Q_PROPERTY(QVariantList buttonSteps         READ buttonSteps        NOTIFY buttonStatesChanged)
    Q_PROPERTY(bool         secondaryStream     READ secondaryStream    WRITE setSecondaryStream NOTIFY secondaryStreamChanged)
    /// true from a change of the main picture (HD/SD, another camera) until the new stream shows its
    /// first frame; the screen keeps the previous picture meanwhile instead of going black
    Q_PROPERTY(bool         switchingStream     READ switchingStream    NOTIFY switchingStreamChanged)

public:
    explicit VehicleModelManager(QObject *parent = nullptr);

    QVariantList models() const { return _models; }
    QVariantList buttonStates() const { return _buttonStates; }
    QVariantList buttonSteps() const { return _buttonSteps; }
    QStringList modelNames() const;
    QStringList cameraTypes() const;

    /// Model selected in the link configuration of the active vehicle's primary link, empty if none.
    QVariantMap activeModel() const { return _activeModel; }

    QVariantList cameraStates() const;
    QList<int> auxiliaryCameras() const;
    bool secondaryStream() const { return _secondaryStream; }
    bool switchingStream() const { return _switchingStream; }
    void setSecondaryStream(bool secondaryStream);

    /// Shows or hides a camera. The main camera cannot be hidden.
    Q_INVOKABLE void setCameraEnabled(int cameraIndex, bool enabled);
    /// Moves a camera to the main video; the previous main camera takes an auxiliary item.
    Q_INVOKABLE void setMainCamera(int cameraIndex);

    /// Number of auxiliary video items in the fly view
    static constexpr int kAuxiliaryCameraCount = 2;

    /// Sends every output of a model button to the active vehicle: a servo PWM value or a relay state.
    /// Switches the active model's button on or off, sending its on or off values.
    Q_INVOKABLE void toggleButton(int buttonIndex);
    /// "steps" buttons: sends the value at the given position (0-based) of every function of the button
    Q_INVOKABLE void selectStep(int buttonIndex, int step);
    /// "momentary" buttons: the on value goes out while the button is held, the off value when it is released
    Q_INVOKABLE void pressButton(int buttonIndex);
    Q_INVOKABLE void releaseButton(int buttonIndex);

    /// @return the named model, or an empty map
    Q_INVOKABLE QVariantMap model(const QString &name) const;

    ~VehicleModelManager() override { if (_instance == this) { _instance = nullptr; } }

    /// The instance QML created, nullptr before that
    static VehicleModelManager *instance() { return _instance; }

    /// Models of the vehicles received from a room, keyed by the name their links use ("room:<id>").
    /// They are looked up like the user's own models but never saved or listed.
    void setRoomModels(const QVariantMap &models);

    /// Adds a model, or replaces the one called originalName (which may differ from the new name).
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE QString saveModel(const QString &originalName, const QVariantMap &model);

    Q_INVOKABLE void deleteModel(const QString &name);

    /// Moves a model in the list; the order is stored with the models.
    Q_INVOKABLE void moveModel(int from, int to);

    /// RTSP URLs for a known camera type.
    /// @return { mainUrl, secondaryUrl }, both empty for the generic type
    Q_INVOKABLE QVariantMap cameraUrls(const QString &type, const QString &ip, const QString &user,
                                       const QString &password) const;

    /// True for a stream address entered by hand (SRT, a MediaMTX path, another port), which the
    /// settings page keeps instead of regenerating it from the camera type, IP and login.
    Q_INVOKABLE bool isCustomCameraUrl(const QString &url) const;

    /// Imports every model from a file in the exchange format (see exportModel). A model whose name is
    /// already taken is imported under a numbered name rather than replacing the existing one.
    /// @return a message for the user describing the outcome
    Q_INVOKABLE QString importModels(const QString &filePath);

    /// Writes one model to a JSON file in the exchange format, which keeps the field names of the files
    /// QGC Nova writes so models can move between the two. Camera passwords are written as plain text.
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE QString exportModel(const QString &name, const QString &filePath) const;

signals:
    void modelsChanged();
    void activeModelChanged();
    void cameraStatesChanged();
    void secondaryStreamChanged();
    void switchingStreamChanged();
    void buttonStatesChanged();

private slots:
    void _activeVehicleChanged(Vehicle *vehicle);
    void _updateActiveModel();
    void _decodingChanged();
    void _activeJoystickChanged(Joystick *joystick);
    void _joystickModelButton(int buttonIndex, bool down);
    void _servoOutputsChanged(const QVector<int> &values);

private:
    int _indexOf(const QString &name) const;
    QString _uniqueName(const QString &name) const;
    QVariantList _activeCameras() const;
    QString _cameraUri(int cameraIndex) const;
    void _applyCameraStreams();
    void _beginStreamSwitch();
    void _endStreamSwitch();
    void _sendFunction(const QVariantMap &function, int value);
    void _setButtonState(int buttonIndex, bool on, int step);
    void _load();
    bool _save() const;
    QString _filePath() const;

    QVariantList _models;
    QVariantMap _roomModels;
    static VehicleModelManager *_instance;
    QVariantMap _activeModel;
    QPointer<Vehicle> _activeVehicle;
    QPointer<Joystick> _joystick;
    QList<bool> _cameraEnabled;         ///< Per camera of the active model
    int _mainCameraIndex = 0;
    bool _secondaryStream = false;
    bool _switchingStream = false;
    bool _switchSawStop = false;        ///< The old picture has stopped; the next start ends the switch
    QTimer _switchTimer;                ///< Gives up waiting for the new stream
    QVariantList _buttonSteps;          ///< Per button of the active model: position in "steps" mode, -1 unknown
    QVariantList _buttonStates;         ///< Per button of the active model: switched on
    QHash<int, qint64> _stepChosenAt;   ///< Per "steps" button: when a value was chosen here (ms since epoch)

    /// A reported output this close to a step value counts as that step (microseconds)
    static constexpr int kStepTolerance = 40;
    /// After a value is chosen here the vehicle needs a moment to report it; older reports are ignored meanwhile
    static constexpr int kStepReportDelayMs = 1500;
};
