import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// UDP RC stream of the vehicle (TX12 Ethernet module frames): the rate it goes out at and, in orange or red, what
/// holds it back. Shown only while a stream runs; the click lists the details.
Item {
    id:             control
    objectName:     "toolbar_rcStreamIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          valuesRow.width

    property bool showIndicator: UdpRcManager.active

    readonly property color _color: UdpRcManager.level >= 2 ? qgcPal.colorRed : (UdpRcManager.level === 1 ? qgcPal.colorOrange : qgcPal.text)

    /// The one thing the operator has to do or know, most urgent first
    readonly property string _hint: {
        if (!UdpRcManager.joystick) {
            return qsTr("no joystick")
        }
        if (!UdpRcManager.throttleCentered) {
            return qsTr("throttle not centred")
        }
        if (UdpRcManager.armBlock === "switch") {
            return qsTr("arm: switch off, then on")
        }
        if (UdpRcManager.armBlock === "throttle") {
            return qsTr("arm: centre the throttle")
        }
        if (UdpRcManager.armMismatch) {
            return qsTr("not armed")
        }
        return ""
    }

    QGCPalette { id: qgcPal }

    TextMetrics {
        id:     widestRate
        font:   rateLabel.font
        text:   qsTr("RC %1 Hz").arg(500)
    }

    Row {
        id:                     valuesRow
        anchors.verticalCenter: parent.verticalCenter
        spacing:                ScreenTools.defaultFontPixelWidth * 0.6

        QGCLabel {
            id:                     rateLabel
            anchors.verticalCenter: parent.verticalCenter
            width:                  Math.ceil(widestRate.advanceWidth)
            text:                   qsTr("RC %1 Hz").arg(UdpRcManager.rateHz)
            color:                  control._color
            font.weight:            Font.ExtraBold
            font.pointSize:         ScreenTools.mediumFontPointSize
        }

        QGCLabel {
            anchors.verticalCenter: parent.verticalCenter
            visible:                UdpRcManager.level > 0
            text:                   "!"
            color:                  control._color
            font.weight:            Font.ExtraBold
            font.pointSize:         ScreenTools.mediumFontPointSize
        }

        QGCLabel {
            anchors.verticalCenter: parent.verticalCenter
            visible:                control._hint !== ""
            text:                   control._hint
            color:                  control._color
            font.weight:            Font.DemiBold
        }
    }

    MouseArea {
        anchors.fill:   parent
        onClicked:      mainWindow.showIndicatorDrawer(detailsPage, control)
    }

    Component {
        id: detailsPage

        ToolIndicatorPage {
            contentComponent: Component {
                GridLayout {
                    columns:        2
                    columnSpacing:  ScreenTools.defaultFontPixelWidth * 2
                    rowSpacing:     ScreenTools.defaultFontPixelHeight * 0.25

                    QGCLabel { text: qsTr("Link") }
                    QGCLabel { text: UdpRcManager.linkName }

                    QGCLabel { text: qsTr("Sent to") }
                    QGCLabel { text: UdpRcManager.target }

                    QGCLabel { text: qsTr("Rate") }
                    QGCLabel { text: qsTr("%1 of %2 Hz").arg(UdpRcManager.rateHz).arg(UdpRcManager.targetRateHz) }

                    QGCLabel { text: qsTr("Send errors") }
                    QGCLabel { text: String(UdpRcManager.errors) }

                    QGCLabel { text: qsTr("Joystick") }
                    QGCLabel {
                        text:   UdpRcManager.joystick ? qsTr("yes") : qsTr("none: neutral and disarm")
                        color:  UdpRcManager.joystick ? qgcPal.text : qgcPal.colorRed
                    }

                    QGCLabel { text: qsTr("Throttle / steering") }
                    QGCLabel { text: qsTr("%1% / %2%").arg(UdpRcManager.throttlePct).arg(UdpRcManager.steerPct) }

                    QGCLabel { text: qsTr("Arm channel") }
                    QGCLabel { text: UdpRcManager.armSent ? qsTr("arm") : qsTr("disarm") }

                    QGCLabel {
                        Layout.columnSpan:      2
                        Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 50
                        wrapMode:               Text.WordWrap
                        visible:                control._hint !== ""
                        color:                  control._color
                        text:                   control._hint
                    }
                }
            }
        }
    }
}
