import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Red exclamation mark for vehicle problems that are not GPS loss or a failsafe (high current, hot motors and
/// the like). Hidden while `alerts` is empty. Add a problem by appending an entry to `alerts`:
///     { id: "motorTemp", text: qsTr("Motor temperature is high") }
Item {
    id:             control
    objectName:     "toolbar_vehicleAlertIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          alertIcon.width

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    /// Current problems of the active vehicle; the checks are added here later
    readonly property var alerts: []

    property bool showIndicator: _activeVehicle && alerts.length > 0

    QGCColoredImage {
        id:                     alertIcon
        anchors.verticalCenter: parent.verticalCenter
        width:                  ScreenTools.defaultFontPixelHeight * 1.5
        height:                 width
        sourceSize.height:      height
        source:                 "/res/nx/Warning.svg"
        color:                  qgcPal.colorRed
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
                            color:  qgcPal.colorRed
                        }
                    }
                }
            }
        }
    }
}
