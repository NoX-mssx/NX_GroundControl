#include "NxCorePlugin.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QApplicationStatic>

QGC_LOGGING_CATEGORY(NxCorePluginLog, "Custom.NxCorePlugin")

Q_APPLICATION_STATIC(NxCorePlugin, _nxCorePluginInstance);

NxOptions::NxOptions(QObject *parent)
    : QGCOptions(parent)
{
}

/*===========================================================================*/

NxCorePlugin::NxCorePlugin(QObject *parent)
    : QGCCorePlugin(parent)
    , _options(new NxOptions(this))
{
    qCDebug(NxCorePluginLog) << this;
}

QGCCorePlugin *NxCorePlugin::instance()
{
    return _nxCorePluginInstance();
}
