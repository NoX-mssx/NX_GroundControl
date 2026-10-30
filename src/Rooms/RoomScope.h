#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>

#include <functional>

class QObject;

/// Whose links, vehicle models and WireGuard tunnels the Rover page shows and edits: a room this computer
/// administers (its room key) or this computer's own (""). Each scope has its own items, so nothing of one room
/// shows up in another; names only have to be unique within a scope.
///
/// Models and tunnels carry their scope in a "room" field. Items stored before scopes existed have none: they are
/// given the scopes of the saved links that use them (copied when links of several scopes do), and the ones no
/// link uses go to this computer.
namespace RoomScope {

/// Field of a model or tunnel profile that holds its scope
inline constexpr char kRoomKey[] = "room";

/// The scope the settings pages edit now
QString current();

/// Calls changed (in context) whenever current() may have changed
void notifyChanges(QObject *context, const std::function<void()> &changed);

/// The scopes of the saved links whose setting linkKey ("vehicle_model", "wireguard_profile") is name
QStringList linkScopes(const QString &linkKey, const QString &name);

/// Gives every untagged item (a map with a "name") its scope as described above, copying it into further scopes
/// when needed. @return true when anything changed
bool tagUntagged(QVariantList &items, const QString &linkKey);

} // namespace RoomScope
