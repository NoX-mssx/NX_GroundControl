import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Exclamation mark for vehicle problems that are not GPS loss or a failsafe (high current, hot motors and
/// the like): orange for a warning, red for a critical problem, hidden while `alerts` is empty.
/// Each entry of `alerts` is { text, level } with level 1 (warning) or 2 (critical).
Item {
    id:             control
    objectName:     "toolbar_vehicleAlertIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          alertIcon.width

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    /// Current problems of the active vehicle
    readonly property var alerts: _motorAlerts()

    readonly property int level: alerts.reduce(function(worst, alert) { return Math.max(worst, alert.level) }, 0)

    property bool showIndicator: _activeVehicle && alerts.length > 0

    function _levelColor(level) {
        return level >= 2 ? qgcPal.colorRed : qgcPal.colorOrange
    }

    /// Motor controller temperature and current against the thresholds of the vehicle model
    function _motorAlerts() {
        const list = []
        const motorAlerts = MotorTelemetryMonitor.alerts
        for (let i = 0; i < motorAlerts.length; i++) {
            const alert = motorAlerts[i]
            const motor = alert.side === "left" ? qsTr("Left motor") : qsTr("Right motor")
            let text = ""
            if (alert.kind === "temperature") {
                text = qsTr("%1: temperature %2 °C (limit %3)").arg(motor).arg(Math.round(alert.value)).arg(alert.threshold)
            } else if (alert.kind === "current") {
                text = qsTr("%1: current %2 A (limit %3)").arg(motor).arg(alert.value.toFixed(1)).arg(alert.threshold)
            } else {
                text = qsTr("No motor telemetry")
            }
            list.push({ text: text, level: alert.level })
        }
        return list
    }

    QGCColoredImage {
        id:                     alertIcon
        anchors.verticalCenter: parent.verticalCenter
        width:                  ScreenTools.defaultFontPixelHeight * 1.5
        height:                 width
        sourceSize.height:      height
        source:                 "/res/nx/Warning.svg"
        color:                  control._levelColor(control.level)
    }

    QGCPalette { id: qgcPal }

    MouseArea {
        anchors.fill:   parent
        onClicked:      mainWindow.showIndicatorDrawer(alertPage, control)
    }

    Component {
        id: alertPage

        ToolIndicatorPage {
            contentComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelHeight / 2

                    QGCLabel {
                        text:           qsTr("Vehicle problems")
                        font.weight:    Font.ExtraBold
                    }

                    Repeater {
                        model: control.alerts

                        QGCLabel {
                            required property var modelData

                            text:   modelData.text
                            color:  control._levelColor(modelData.level)
                        }
                    }

                    QGCLabel {
                        visible:    control.alerts.length === 0
                        text:       qsTr("No problems")
                        opacity:    0.7
                    }
                }
            }
        }
    }
}
