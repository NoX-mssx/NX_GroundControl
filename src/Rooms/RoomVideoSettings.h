#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QStringList>

class Fact;

/// The video settings a room admin may send to an operator: the stream handling, display and recording options of
/// the Video settings page, not the video source (that comes with the vehicle model).
namespace RoomVideoSettings
{
    /// Names of the VideoSettings facts that are sent, in the order of the Video settings page
    const QStringList &keys();

    /// The fact of this computer for a key, or nullptr
    Fact *fact(const QString &key);

    /// The values this computer uses now
    QJsonObject current();

    /// Sets the values of the known keys (converted and checked against each setting's limits); unknown keys and
    /// invalid values are skipped. Returns the number of settings that changed.
    int apply(const QJsonObject &settings);
}
