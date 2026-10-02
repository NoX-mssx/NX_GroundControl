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
/// Camera passwords and stream URLs (which embed the credentials) are encrypted on disk, see SecretProtector.
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

private:
    int _indexOf(const QString &name) const;
    QString _uniqueName(const QString &name) const;
    void _load();
    bool _save() const;
    QString _filePath() const;

    QVariantList _models;
};
