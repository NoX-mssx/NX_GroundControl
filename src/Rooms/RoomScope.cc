#include "RoomScope.h"

#include "LinkConfiguration.h"
#include "RoomManager.h"

#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QVariantMap>

namespace RoomScope {

QString current()
{
    RoomManager *const rooms = RoomManager::instance();
    return (rooms->currentRole() == QLatin1String("admin")) ? rooms->currentRoom() : QString();
}

void notifyChanges(QObject *context, const std::function<void()> &changed)
{
    RoomManager *const rooms = RoomManager::instance();
    // The role in the current room follows the rooms list (a request approved, access turned off)
    (void) QObject::connect(rooms, &RoomManager::currentRoomChanged, context, changed);
    (void) QObject::connect(rooms, &RoomManager::roomsChanged, context, changed);
}

QStringList linkScopes(const QString &linkKey, const QString &name)
{
    QStringList scopes;
    if (name.isEmpty()) {
        return scopes;
    }
    QSettings settings;
    const QString root = LinkConfiguration::settingsRoot();
    const int count = settings.value(root + QStringLiteral("/count")).toInt();
    for (int i = 0; i < count; i++) {
        const QString link = root + QStringLiteral("/Link%1/").arg(i);
        if (settings.value(link + linkKey).toString() != name) {
            continue;
        }
        const QString room = settings.value(link + QStringLiteral("room")).toString();
        if (!scopes.contains(room)) {
            scopes.append(room);
        }
    }
    return scopes;
}

bool tagUntagged(QVariantList &items, const QString &linkKey)
{
    bool changed = false;
    const qsizetype count = items.count();
    for (qsizetype i = 0; i < count; i++) {
        QVariantMap item = items.at(i).toMap();
        if (item.contains(QLatin1String(kRoomKey))) {
            continue;
        }
        QStringList scopes = linkScopes(linkKey, item.value(QStringLiteral("name")).toString());
        if (scopes.isEmpty()) {
            scopes.append(QString());
        }
        item[QLatin1String(kRoomKey)] = scopes.takeFirst();
        items[i] = item;
        for (const QString &scope : std::as_const(scopes)) {
            QVariantMap copy = item;
            copy[QLatin1String(kRoomKey)] = scope;
            items.append(copy);
        }
        changed = true;
    }
    return changed;
}

} // namespace RoomScope
