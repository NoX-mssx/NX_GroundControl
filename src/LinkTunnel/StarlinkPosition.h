#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

class Fact;
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

public:
    enum Source { Rover = 0, Starlink = 1, Auto = 2 };
    Q_ENUM(Source)

    explicit StarlinkPosition(QObject *parent = nullptr);

    bool usingStarlink() const { return _usingStarlink; }
    bool starlinkFresh() const;
    QString starlinkError() const { return _error; }
    QString dishAddress() const;

    /// The grpc-status header says OK; Qt may list a header twice ("0, 0") once the trailers arrive
    static bool grpcStatusOk(const QByteArray &value);
    /// Request body of get_location: a gRPC frame holding Request { get_location = {} }
    static QByteArray locationRequest();
    /// Reads the position out of a gRPC response body; on failure returns an invalid coordinate and the reason
    static QGeoCoordinate parseLocationResponse(const QByteArray &body, QString *error);

signals:
    void stateChanged();
    /// The chosen source needs the dish, and the dish gives no position; for the "!" of the drive screen
    void problem(const QString &message);

private slots:
    void _activeVehicleChanged(Vehicle *vehicle);
    void _updateAddress();
    void _tick();

private:
    int _source() const;
    bool _roverHasFix() const;
    void _poll();
    void _setError(const QString &error);
    void _update();

    QNetworkAccessManager *_network = nullptr;
    QPointer<Vehicle> _vehicle;
    Fact *_sourceFact = nullptr;
    QTimer _timer;
    QElapsedTimer _clock;

    QString _url;                       ///< gRPC method URL of the dish, empty without an address
    QPointer<QNetworkReply> _reply;     ///< Request in flight
    QGeoCoordinate _fix;                ///< Last position from the dish
    qint64 _fixAt = -1;                 ///< _clock time of _fix
    QString _error;
    QString _reportedError;             ///< Last reason given to the "!", so it is not repeated every second

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
