#include "NxCorePlugin.h"
#include "AppSettings.h"
#include "BatteryIndicatorSettings.h"
#include "FlyViewSettings.h"
#include "LogManagerSettings.h"
#include "VideoSettings.h"
#include "Fact.h"
#include "FactMetaData.h"
#include "FactValueGrid.h"
#include "InstrumentValueData.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsFact.h"
#include "SettingsManager.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QUrl>

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

void NxCorePlugin::init()
{
    QGCCorePlugin::init();

    // The CPU video path is forced through adjustSettingMetaData, but VideoSettings makes the setting visible again
    // after that hook; hide it before the settings pages load.
    Fact *const forceCpuVideoPath = SettingsManager::instance()->videoSettings()->forceCpuVideoPath();
    if (auto *const setting = qobject_cast<SettingsFact *>(forceCpuVideoPath)) {
        setting->setUserVisible(false);
    }
}

bool NxCorePlugin::overrideSettingsGroupVisibility(const QString &name)
{
    static const QStringList hiddenGroups = {
        QStringLiteral("ADSBVehicleManager"),
        QStringLiteral("NTRIP"),
        QStringLiteral("PlanView"),
        QStringLiteral("RemoteID"),
        QStringLiteral("Viewer3D"),
    };

    return !hiddenGroups.contains(name);
}

void NxCorePlugin::adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &userVisible)
{
    QGCCorePlugin::adjustSettingMetaData(settingsGroup, metaData, userVisible);

    if ((settingsGroup == BatteryIndicatorSettings::settingsGroup) &&
        (metaData.name() == BatteryIndicatorSettings::valueDisplayName)) {
        // Voltage: the vehicle-reported percentage is only meaningful with a configured battery monitor.
        metaData.setRawDefaultValue(1);
    }

    if (settingsGroup == VideoSettings::settingsGroup) {
        if (metaData.name() == VideoSettings::forceCpuVideoPathName) {
            // The GPU zero-copy path shares one decoder device across pipelines and resets it whenever a
            // stream (re)starts, which blanks the other camera windows. It also hands the decoder Qt's own
            // Direct3D device: with it on, an AMD driver ended the application from inside a draw call
            // (0xC0000409, control flow guard) seconds after a graphics reset. Copy frames through the CPU,
            // always: a hidden setting takes its default, so a value saved earlier is ignored (and the
            // room video settings no longer carry it). init() keeps it hidden.
            metaData.setRawDefaultValue(true);
            userVisible = false;
        } else if (metaData.name() == VideoSettings::rtpJitterLatencyMsName) {
            // Starlink jitter regularly exceeds the stock 80 ms.
            metaData.setRawDefaultValue(300);
        }
    }

    if (settingsGroup == FlyViewSettings::settingsGroup) {
        // Guided Commands: take-off and flight altitudes, the fixed-wing loiter radius and the map go-to limits do not
        // apply to a rover driven with a joystick. Hiding every setting of the group hides its tab; go-to keeps its
        // defaults (1000 m, with confirmation).
        static const QStringList guidedSettings = {
            QString::fromLatin1(FlyViewSettings::guidedMinimumAltitudeName),
            QString::fromLatin1(FlyViewSettings::guidedMaximumAltitudeName),
            QString::fromLatin1(FlyViewSettings::maxGoToLocationDistanceName),
            QString::fromLatin1(FlyViewSettings::forwardFlightGoToLocationLoiterRadName),
            QString::fromLatin1(FlyViewSettings::goToLocationRequiresConfirmInGuidedName),
        };
        if (guidedSettings.contains(metaData.name())) {
            userVisible = false;
        }
    }

    if ((settingsGroup == LogManagerSettings::settingsGroup) && (metaData.name() == LogManagerSettings::diskLoggingEnabledName)) {
        // Keeps the log of a run that crashed: AppLog.log in the Logs folder, rotated at 10 MB
        metaData.setRawDefaultValue(true);
    }

    if ((settingsGroup == AppSettings::settingsGroup) && (metaData.name() == AppSettings::indoorPaletteName)) {
        // Only the dark (glass) theme is designed; the light one is not offered.
        metaData.setRawDefaultValue(1);
        userVisible = false;
    }
}

void NxCorePlugin::factValueGridCreateDefaultSettings(FactValueGrid *factValueGrid)
{
    struct Cell {
        const char *factName;
        const char *icon;
        bool showUnits;
    };
    static constexpr Cell cells[2][2] = {
        {{"GroundSpeed", "arrow-simple-right.svg", true}, {"DistanceToHome", "bookmark copy 3.svg", true}},
        {{"FlightTime", "timer.svg", false}, {"FlightDistance", "travel-walk.svg", true}},
    };

    factValueGrid->setFontSize(FactValueGrid::MediumFontSize);
    (void) factValueGrid->appendColumn();
    (void) factValueGrid->appendColumn();
    // A grid tied to one vehicle card is a single row; the main telemetry bar gets both.
    const int rowCount = factValueGrid->specificVehicleForCard() ? 1 : 2;
    if (rowCount == 2) {
        factValueGrid->appendRow();
    }

    for (int col = 0; col < 2; col++) {
        QmlObjectListModel *column = factValueGrid->columns()->value<QmlObjectListModel*>(col);
        if (!column) {
            continue;
        }
        for (int row = 0; row < rowCount; row++) {
            InstrumentValueData *value = column->value<InstrumentValueData*>(row);
            if (!value) {
                continue;
            }
            const Cell &cell = cells[col][row];
            value->setFact(QStringLiteral("Vehicle"), QString::fromLatin1(cell.factName));
            value->setIcon(QString::fromLatin1(cell.icon));
            if (value->fact()) {
                value->setText(value->fact()->shortDescription());
            }
            value->setShowUnits(cell.showUnits);
        }
    }
}

const QVariantList &NxCorePlugin::toolBarIndicators()
{
    static const QVariantList indicators = [this]() {
        QVariantList list = QGCCorePlugin::toolBarIndicators();
        list.append(QVariant::fromValue(QUrl::fromUserInput(QStringLiteral("qrc:/qml/QGroundControl/Toolbar/LinkQualityIndicator.qml"))));
        return list;
    }();

    return indicators;
}
