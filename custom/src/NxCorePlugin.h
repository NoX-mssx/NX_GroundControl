#pragma once

#include "QGCCorePlugin.h"
#include "QGCOptions.h"

Q_DECLARE_LOGGING_CATEGORY(NxCorePluginLog)

class NxOptions : public QGCOptions
{
    Q_OBJECT

public:
    explicit NxOptions(QObject *parent = nullptr);

    /// Vehicles are driven by joystick only, so mission planning is not offered.
    bool showPlanView() const final { return false; }
    bool showTakeoffLandActions() const final { return false; }
    bool showSettingsPageSections() const final { return false; }
    bool showPX4LogTransferOptions() const final { return false; }
    bool showSensorCalibrationAirspeed() const final { return false; }
};

/*===========================================================================*/

class NxCorePlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit NxCorePlugin(QObject *parent = nullptr);

    static QGCCorePlugin *instance();

    QGCOptions *options() final { return _options; }
    bool overrideSettingsGroupVisibility(const QString &name) final;
    void adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &userVisible) final;
    void factValueGridCreateDefaultSettings(FactValueGrid *factValueGrid) final;

private:
    NxOptions *_options = nullptr;
};
