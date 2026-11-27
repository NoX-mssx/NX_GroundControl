#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

class Fact;
class LinkConfiguration;
class QNetworkAccessManager;
class QNetworkReply;
class Vehicle;

/// Where the active vehicle is shown on the map: its own GPS (Rover), the position of its Starlink dish
/// (STARLINK), or the dish only while the vehicle's GPS has no fix (Auto). The choice is the FlyView setting
/// gpsSource.
///
/// The dish is asked for its location once a second through the link's tunnel, over its local gRPC API
/// (SpaceX.API.Device.Device/Handle, get_location), at the link's Starlink address or else its ping address on
/// port 9200. Only what is shown changes (Vehicle::setExternalPosition); nothing is sent to the vehicle.
///
/// While the vehicle is shown at the dish, its speed is shown from the dish too: the dish gives positions only,
/// so the speed is the distance over the last seconds of them. Without positions from the dish it is 0.
class StarlinkPosition : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// The vehicle is shown at the dish's position now
    Q_PROPERTY(bool     usingStarlink   READ usingStarlink  NOTIFY stateChanged)
    /// The dish gave its position within the last few seconds
    Q_PROPERTY(bool     starlinkFresh   READ starlinkFresh  NOTIFY stateChanged)
    /// Why the dish gives no position, empty while it does or while it is not asked
    Q_PROPERTY(QString  starlinkError   READ starlinkError  NOTIFY stateChanged)
    /// host:port the dish is asked at, empty when the link gives no address
    Q_PROPERTY(QString  dishAddress     READ dishAddress    NOTIFY stateChanged)
    /// The speed shown is the dish's (dishSpeed): the chosen source is the dish, whether it gives positions or not
    Q_PROPERTY(bool     speedFromDish   READ speedFromDish  NOTIFY speedChanged)
    /// Speed over the ground from the dish's positions, m/s; 0 while the dish gives none
    Q_PROPERTY(double   dishSpeed       READ dishSpeed      NOTIFY speedChanged)
    /// Admin view (AdminWatch): the watched vehicle's place from its dish, invalid without a fresh one
    Q_PROPERTY(QGeoCoordinate watchCoordinate READ watchCoordinate NOTIFY watchCoordinateChanged)

public:
    enum Source { Rover = 0, Starlink = 1, Auto = 2 };
    Q_ENUM(Source)

    explicit StarlinkPosition(QObject *parent = nullptr);

    bool usingStarlink() const { return _usingStarlink; }
    bool starlinkFresh() const;
    QString starlinkError() const { return _error; }
    QString dishAddress() const;
    bool speedFromDish() const { return _speedFromDish; }
    double dishSpeed() const { return _dishSpeed; }
    QGeoCoordinate watchCoordinate() const { return _watchCoordinate; }

    /// A position of the dish and when it was taken (ms of a steady clock)
    struct TrackPoint
    {
        qint64 atMs = 0;
        QGeoCoordinate coordinate;
    };
    /// Speed in m/s from the oldest to the newest point of a track, oldest first. 0 with less than
    /// kSpeedMinSpanMs between them, or under kStillMeters apart: a standing dish's position wanders.
    static double trackSpeed(const QList<TrackPoint> &track);

    /// The speed is taken over this much of the track: longer is steadier, shorter follows sooner
    static constexpr qint64 kSpeedWindowMs = 3000;
    static constexpr qint64 kSpeedMinSpanMs = 800;
    static constexpr double kStillMeters = 1.5;

    /// The grpc-status header says OK; Qt may list a header twice ("0, 0") once the trailers arrive
    static bool grpcStatusOk(const QByteArray &value);
    /// Request body of get_location: a gRPC frame holding Request { get_location = {} }
    static QByteArray locationRequest();
    /// Reads the position out of a gRPC response body; on failure returns an invalid coordinate and the reason
    static QGeoCoordinate parseLocationResponse(const QByteArray &body, QString *error);

signals:
    void stateChanged();
    void speedChanged();
    void watchCoordinateChanged();
    /// The chosen source needs the dish, and the dish gives no position; for the "!" of the drive screen
    void problem(const QString &message);

private slots:
    void _activeVehicleChanged(Vehicle *vehicle);
    /// The admin view's link (AdminWatch::watchedConfig) changed: its dish is asked while no vehicle is active
    void _watchedConfigChanged();
    void _updateAddress();
    void _tick();

private:
    int _source() const;
    bool _roverHasFix() const;
    void _poll();
    void _setError(const QString &error);
    void _update();
    void _addTrackPoint(qint64 atMs, const QGeoCoordinate &coordinate);

    QNetworkAccessManager *_network = nullptr;
    QPointer<Vehicle> _vehicle;
    QPointer<LinkConfiguration> _watchConfig;   ///< Admin view: the watched link, while no vehicle is active
    QGeoCoordinate _watchCoordinate;
    Fact *_sourceFact = nullptr;
    QTimer _timer;
    QElapsedTimer _clock;

    QString _url;                       ///< gRPC method URL of the dish, empty without an address
    QPointer<QNetworkReply> _reply;     ///< Request in flight
    QGeoCoordinate _fix;                ///< Last position from the dish
    qint64 _fixAt = -1;                 ///< _clock time of _fix
    QString _error;
    QSet<QString> _reportedErrors;      ///< Reasons given to the "!" since the dish last gave a position
    QSet<QString> _seenErrors;          ///< Reasons logged since then, each logged once
    QList<TrackPoint> _track;           ///< The dish's positions of the last kSpeedWindowMs, oldest first
    bool _speedFromDish = false;
    double _dishSpeed = 0;

    bool _usingStarlink = false;
    bool _fresh = false;                ///< starlinkFresh() when last told
    bool _autoOnDish = false;           ///< Auto has switched to the dish
    qint64 _roverFixSince = -1;         ///< _clock time since the vehicle's GPS has had a fix, -1 without one
    qint64 _roverFixLostAt = -1;        ///< _clock time since the vehicle's GPS has had no fix, -1 with one

    static constexpr int kPollIntervalMs = 1000;
    static constexpr int kTimeoutMs = 3000;
    /// A position older than this is not shown
    static constexpr qint64 kFreshMs = 5000;
    /// Auto takes the dish once the vehicle's GPS has had no fix this long, and goes back once it has had one
    /// for kAutoToRoverMs, so a fix that comes and goes does not make the vehicle jump between the two
    static constexpr qint64 kAutoToDishMs = 3000;
    static constexpr qint64 kAutoToRoverMs = 5000;
    static constexpr int kDefaultPort = 9200;
};
