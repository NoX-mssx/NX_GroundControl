#include "RoomVideoSettings.h"

#include "Fact.h"
#include "FactMetaData.h"
#include "SettingsManager.h"
#include "VideoSettings.h"

#include <QtCore/QJsonValue>

namespace RoomVideoSettings
{

const QStringList &keys()
{
    static const QStringList list = {
        QStringLiteral("aspectRatio"),           QStringLiteral("stretchVideoToWindow"),
        QStringLiteral("disableWhenDisarmed"),   QStringLiteral("lowLatencyMode"),
        QStringLiteral("rtpJitterLatencyMs"),    QStringLiteral("srtLatencyMs"),
        QStringLiteral("showFramesImmediately"), QStringLiteral("rtspUseUdp"),
        QStringLiteral("showDamagedFrames"),     QStringLiteral("rtspTimeout"),
        QStringLiteral("rtspAutoReconnect"),     QStringLiteral("forceVideoDecoder"),
        QStringLiteral("recordingFormat"),       QStringLiteral("enableStorageLimit"),
        QStringLiteral("maxVideoSize"),
    };
    return list;
}

Fact *fact(const QString &key)
{
    if (!keys().contains(key)) {
        return nullptr;
    }
    VideoSettings *const settings = SettingsManager::instance()->videoSettings();
    return settings ? settings->property(key.toLatin1().constData()).value<Fact *>() : nullptr;
}

QJsonObject current()
{
    QJsonObject values;
    for (const QString &key : keys()) {
        if (Fact *const setting = fact(key)) {
            values.insert(key, QJsonValue::fromVariant(setting->rawValue()));
        }
    }
    return values;
}

int apply(const QJsonObject &settings)
{
    int changed = 0;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        Fact *const setting = fact(it.key());
        if (!setting) {
            continue;
        }
        QVariant typed;
        QString error;
        if (!setting->metaData()->convertAndValidateRaw(it.value().toVariant(), false /* convertOnly */, typed, error)) {
            continue;
        }
        if (typed != setting->rawValue()) {
            setting->setRawValue(typed);
            changed++;
        }
    }
    return changed;
}

} // namespace RoomVideoSettings
