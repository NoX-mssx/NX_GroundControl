#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(VehicleModelManagerLog)

/// Stores per-vehicle profiles ("models"): the cameras on board and the on-screen buttons that drive servo
/// or relay outputs. Models are plain maps so the settings page can edit a copy and save it back whole:
///
///     { name, cameras: [{ type, name, ip, user, password, mainUrl, secondaryUrl, audio }],
///             buttons: [{ name, functions: [{ channel, kind: "pwm"|"gpio", value }] }] }
///
/// Camera passwords are encrypted on disk, see SecretProtector.
class VehicleModelManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList models      READ models         NOTIFY modelsChanged)
    Q_PROPERTY(QStringList  modelNames  READ modelNames     NOTIFY modelsChanged)
    Q_PROPERTY(QStringList  cameraTypes READ cameraTypes    CONSTANT)

public:
    explicit VehicleModelManager(QObject *parent = nullptr);

    QVariantList models() const { return _models; }
    QStringList modelNames() const;
    QStringList cameraTypes() const;

    /// @return the named model, or an empty map
    Q_INVOKABLE QVariantMap model(const QString &name) const;

    /// Adds a model, or replaces the one called originalName (which may differ from the new name).
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE QString saveModel(const QString &originalName, const QVariantMap &model);

    Q_INVOKABLE void deleteModel(const QString &name);

    /// RTSP URLs for a known camera type.
    /// @return { mainUrl, secondaryUrl }, both empty for the generic type
    Q_INVOKABLE QVariantMap cameraUrls(const QString &type, const QString &ip, const QString &user,
                                       const QString &password) const;

signals:
    void modelsChanged();

private:
    int _indexOf(const QString &name) const;
    void _load();
    bool _save() const;
    QString _filePath() const;

    QVariantList _models;
};
