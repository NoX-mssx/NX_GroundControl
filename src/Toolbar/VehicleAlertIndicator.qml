import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Exclamation mark for vehicle problems that are not GPS loss or a failsafe: orange for a warning, red for a
/// critical problem, hidden while there is nothing to show. It collects
///   - the live checks (motor controller temperature and current against the model thresholds),
///   - the messages that would otherwise pop up while driving (see MainWindow.addVehicleNotice).
/// Each entry is { text, level } with level 1 (warning) or 2 (critical); messages also carry time and count.
/// The vehicle does not say when a problem from a message is over, so a message lights the mark only while it is
/// fresh (repeated within the last 30 s for a warning, 60 s for a critical one) and until a "... cleared" message
/// resolves it. Older messages stay in the list, dimmed, until they are cleared.
Item {
    id:             control
    objectName:     "toolbar_vehicleAlertIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          alertIcon.width

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    /// Live problems of the active vehicle
    readonly property var alerts: _motorAlerts()
    /// Messages received while driving, newest first
    readonly property var notices: mainWindow.vehicleNotices
    /// The messages that still light the mark
    readonly property var activeNotices: notices.filter(function(notice) { return control._isActive(notice) })

    readonly property int level: Math.max(alerts.reduce(function(worst, alert) { return Math.max(worst, alert.level) }, 0),
                                          activeNotices.reduce(function(worst, notice) { return Math.max(worst, notice.level) }, 0))

    property bool showIndicator: _activeVehicle && (alerts.length > 0 || activeNotices.length > 0)

    readonly property int _warningHoldMs:   30000
    readonly property int _criticalHoldMs:  60000
    property real _now: Date.now()

    function _isActive(notice) {
        if (notice.level <= 0 || notice.resolved) {
            return false
        }
        return (_now - notice.time.getTime()) < (notice.level >= 2 ? _criticalHoldMs : _warningHoldMs)
    }

    function _levelColor(level) {
        if (level <= 0) {
            return qgcPal.colorGreen
        }
        return level >= 2 ? qgcPal.colorRed : qgcPal.colorOrange
    }

    // Lets messages age out of the mark
    Timer {
        interval:       1000
        repeat:         true
        running:        control.notices.length > 0
        onTriggered:    control._now = Date.now()
    }
    onNoticesChanged: _now = Date.now()

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
                        visible:    control.alerts.length === 0 && control.activeNotices.length === 0
                        text:       qsTr("No problems")
                        opacity:    0.7
                    }

                    // ---- Messages received while driving ----
                    QGCLabel {
                        Layout.topMargin:   ScreenTools.defaultFontPixelHeight * 0.3
                        visible:            control.notices.length > 0
                        text:               qsTr("Messages")
                        font.weight:        Font.ExtraBold
                    }

                    QGCFlickable {
                        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 60
                        Layout.preferredHeight: Math.min(noticeColumn.height, ScreenTools.defaultFontPixelHeight * 20)
                        contentHeight:          noticeColumn.height
                        clip:                   true
                        visible:                control.notices.length > 0

                        ColumnLayout {
                            id:         noticeColumn
                            width:      parent.width
                            spacing:    ScreenTools.defaultFontPixelHeight * 0.4

                            Repeater {
                                model: control.notices

                                RowLayout {
                                    Layout.fillWidth:   true
                                    spacing:            ScreenTools.defaultFontPixelWidth
                                    // Over, resolved or only a report that something is over: kept as history
                                    opacity:            control._isActive(modelData) ? 1 : 0.55

                                    required property var modelData

                                    QGCLabel {
                                        Layout.alignment:   Qt.AlignTop
                                        text:               Qt.formatTime(modelData.time, "HH:mm:ss")
                                        color:              qgcPal.colorGrey
                                    }

                                    QGCLabel {
                                        Layout.fillWidth:   true
                                        text:               modelData.text + (modelData.count > 1 ? "  ×" + modelData.count : "")
                                        color:              control._levelColor(modelData.level)
                                        wrapMode:           Text.WordWrap
                                    }

                                    QGCButton {
                                        Layout.alignment:   Qt.AlignTop
                                        visible:            modelData.reboot
                                        enabled:            control._activeVehicle && !control._activeVehicle.armed
                                        text:               qsTr("Reboot vehicle")
                                        onClicked: {
                                            mainWindow.closeIndicatorDrawer()
                                            control._activeVehicle.rebootVehicle()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    QGCButton {
                        visible:    control.notices.length > 0
                        text:       qsTr("Clear messages")
                        onClicked: {
                            mainWindow.clearVehicleNotices()
                            if (control.alerts.length === 0) {
                                mainWindow.closeIndicatorDrawer()
                            }
                        }
                    }
                }
            }
        }
    }
}
