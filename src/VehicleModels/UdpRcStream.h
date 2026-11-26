#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QMutex>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtNetwork/QHostAddress>
#include <array>
#include <atomic>

/// Vehicle control as the TX12 Ethernet module does it, for vehicles that take no control over MAVLink ("Zmei"):
/// 16 channels in CRSF units (172..992..1811), each a big-endian uint16, channel 1 first, 32 bytes per UDP packet,
/// sent at an even rate. Same frames and safety rules as zmiy_rc_emulator.py.

/// Settings of a vehicle model ("rcControl" in the model)
struct UdpRcSettings
{
    static constexpr int kChannels = 16;
    static constexpr quint16 kCrsfMin = 172;
    static constexpr quint16 kCrsfMid = 992;
    static constexpr quint16 kCrsfMax = 1811;
    /// Highest value the arm channel may carry: 1811 there is "arm + Auto", which is never sent
    static constexpr quint16 kArmChannelMax = 1500;

    bool enabled = false;        ///< model type "udp": this stream, not MAVLink, drives the vehicle
    QString host;                ///< empty: the vehicle address of the link
    quint16 port = 14300;
    quint16 sourcePort = 14300;  ///< taken when free, any port otherwise
    double rateHz = 100.0;
    std::array<quint16, kChannels> rest{
        {992, 992, 173, 173, 173, 420, 173, 173, 1811, 173, 992, 992, 992, 992, 992, 992}};
    int throttleChannel = 1;  ///< channels count from 1
    int steerChannel = 2;
    int throttleAxis = 0;     ///< joystick axes count from 0
    int steerAxis = 1;
    bool invertThrottle = false;
    bool invertSteer = false;
    double deadzone = 0.08;
    double throttleLimit = 0.3;  ///< share of the full stroke
    double steerLimit = 0.5;
    double slew = 4.0;           ///< full strokes per second a command may grow by
    int armAxis = 6;             ///< -1: no arm switch, always disarmed
    double armThreshold = 0.5;   ///< arm switch on above this
    int armChannel = 8;
    quint16 armValue = 992;
    quint16 disarmValue = 173;

    static UdpRcSettings fromModel(const QVariantMap& rcControl);
    QVariantMap toModel() const;
    /// Empty when usable; otherwise what is wrong, for the model editor
    QString problem() const;
    bool operator==(const UdpRcSettings& other) const = default;
};

/// The per-frame logic: stick shaping, the throttle-centred and arm interlocks, the slew limit
class UdpRcLogic
{
public:
    enum class ArmBlock
    {
        None,
        SwitchOnAtStart,  ///< the arm switch has not been in "disarm" since the start or the joystick came
        ThrottleNotCentered,
        NoJoystick,
    };

    /// Arm or disarm from the screen (a click on the arm state), besides the switch
    enum class ArmRequest
    {
        None,
        Arm,
        Disarm,
    };

    struct Input
    {
        bool joystick = false;   ///< a joystick is there and its axes are fresh
        quint64 generation = 0;  ///< changes whenever a joystick (re)appears
        QVector<double> axes;    ///< raw value / 32768, as pygame gives it
        ArmRequest armRequest = ArmRequest::None;
    };

    struct Output
    {
        std::array<quint16, UdpRcSettings::kChannels> channels{};
        bool armed = false;
        bool throttleCentered = false;
        ArmBlock armBlock = ArmBlock::None;
        double throttle = 0;    ///< command sent, after limit and slew, -1..1
        double steer = 0;
        bool armGuard = false;  ///< the arm channel held a value above kArmChannelMax; disarm went instead
        /// An arm from the screen asked for in this frame and not done, and why; None otherwise
        ArmBlock screenArmRefused = ArmBlock::None;
    };

    explicit UdpRcLogic(const UdpRcSettings& settings = UdpRcSettings());

    void setSettings(const UdpRcSettings& settings) { _settings = settings; }

    const UdpRcSettings& settings() const { return _settings; }

    /// One frame; @p nowSeconds from a monotonic clock, @p throttleScale the high-ping limit (1 = none)
    Output step(const Input& input, double nowSeconds, double throttleScale = 1.0);

    /// Neutral sticks and disarm, for the last second of a stream
    std::array<quint16, UdpRcSettings::kChannels> neutralDisarmed() const;

    static QByteArray frame(const std::array<quint16, UdpRcSettings::kChannels>& channels);
    /// -1..1 to CRSF, rounded like Python's round()
    static quint16 toCrsf(double value);
    /// Dead zone and its stretch, then the inversion
    static double shapeAxis(double value, double deadzone, bool invert);
    /// Growth limited to @p step, a fall towards zero at once; across zero it goes to zero first
    static double slewTo(double current, double target, double step);

private:
    std::array<quint16, UdpRcSettings::kChannels> _build(double throttle, double steer, bool armed,
                                                         bool& armGuard) const;

    UdpRcSettings _settings;
    quint64 _generation = 0;
    bool _throttleOk = false;
    bool _armed = false;        ///< what goes out: armed by the switch or from the screen
    bool _switchArmed = false;
    bool _screenArmed = false;  ///< armed from the screen; the switch going off (or a disarm click) ends it
    bool _switchWasOn = false;
    bool _armReady = false;
    double _throttle = 0;
    double _steer = 0;
    double _lastFrame = -1;
};

/// Sends the frames from its own thread, on a precise timer, while the link is on
class UdpRcStream : public QThread
{
    Q_OBJECT

public:
    struct Status
    {
        bool running = false;
        double rateHz = 0;  ///< frames sent in the last second
        quint64 sent = 0;
        quint64 errors = 0;
        QString lastError;
        bool joystick = false;
        bool armed = false;
        bool throttleCentered = false;
        UdpRcLogic::ArmBlock armBlock = UdpRcLogic::ArmBlock::None;
        double throttle = 0;
        double steer = 0;
        bool armGuard = false;
        quint16 sourcePort = 0;
        quint64 screenArmRefusals = 0;  ///< arms from the screen not done so far
        UdpRcLogic::ArmBlock lastScreenArmRefusal = UdpRcLogic::ArmBlock::None;
    };

    UdpRcStream(const UdpRcSettings& settings, const QHostAddress& target, QObject* parent = nullptr);
    ~UdpRcStream() override;

    /// Takes effect with the next frame; the stream does not stop
    void updateSettings(const UdpRcSettings& settings, const QHostAddress& target);
    /// About one second of neutral and disarm, then the thread ends
    void stopGracefully();
    /// Ends at once, with no last frames
    void stopNow();

    Status status() const;
    QHostAddress target() const;

    /// Arm or disarm from the screen, applied by the next frame. An arm needs the joystick and its throttle
    /// centred, like the switch; a refusal shows in Status::screenArmRefusals.
    void requestArm(bool arm);

protected:
    void run() override;

private:
    mutable QMutex _mutex;
    UdpRcSettings _settings;
    QHostAddress _target;
    bool _settingsChanged = false;
    Status _status;
    std::atomic<int> _armRequest{0};  ///< UdpRcLogic::ArmRequest, taken by the next frame
    std::atomic<bool> _graceful{false};
    std::atomic<bool> _abort{false};
};
