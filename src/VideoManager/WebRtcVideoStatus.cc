#include "WebRtcVideoStatus.h"

#include <QtCore/QCoreApplication>
#include <QtQml/QJSEngine>

namespace {
WebRtcVideoStatus* s_instance = nullptr;
}

WebRtcVideoStatus::WebRtcVideoStatus(QObject* parent) : QObject(parent)
{
    s_instance = this;
}

WebRtcVideoStatus* WebRtcVideoStatus::instance()
{
    if (!s_instance) {
        (void) new WebRtcVideoStatus(QCoreApplication::instance());
    }
    return s_instance;
}

WebRtcVideoStatus* WebRtcVideoStatus::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    WebRtcVideoStatus* const status = instance();
    QJSEngine::setObjectOwnership(status, QJSEngine::CppOwnership);
    return status;
}

void WebRtcVideoStatus::reportProblem(const QString& stream, const QString& reason)
{
    QSet<QString>& reported = _reported[stream];
    if (reported.contains(reason)) {
        return;
    }
    reported.insert(reason);
    emit problem(stream, reason);
}

void WebRtcVideoStatus::reportWorking(const QString& stream)
{
    (void) _reported.remove(stream);
}
