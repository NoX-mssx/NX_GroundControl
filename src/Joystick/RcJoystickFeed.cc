#include "RcJoystickFeed.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>

namespace {

QMutex s_mutex;
RcJoystickFeed::Snapshot s_snapshot;
const Joystick* s_source = nullptr;

const QElapsedTimer& feedClock()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer started;
        started.start();
        return started;
    }();
    return timer;
}

}  // namespace

std::atomic<bool> RcJoystickFeed::_wanted{false};

qint64 RcJoystickFeed::now()
{
    return feedClock().elapsed();
}

void RcJoystickFeed::publish(const Joystick* source, const QVector<double>& axes)
{
    const qint64 time = now();
    QMutexLocker locker(&s_mutex);
    if (!s_snapshot.valid || (source != s_source)) {
        s_snapshot.generation++;
        s_source = source;
    }
    s_snapshot.valid = true;
    s_snapshot.timeMs = time;
    s_snapshot.axes = axes;
}

void RcJoystickFeed::invalidate(const Joystick* source)
{
    QMutexLocker locker(&s_mutex);
    if (source == s_source) {
        s_snapshot.valid = false;
        s_snapshot.axes.clear();
    }
}

RcJoystickFeed::Snapshot RcJoystickFeed::read()
{
    const qint64 time = now();
    QMutexLocker locker(&s_mutex);
    Snapshot snapshot = s_snapshot;
    if (snapshot.valid && ((time - snapshot.timeMs) > kStaleMs)) {
        snapshot.valid = false;
    }
    return snapshot;
}
