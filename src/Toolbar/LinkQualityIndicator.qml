import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Round trip time to the vehicle's router. Orange while the throttle is limited, red without replies;
/// the limit itself is announced above the speed on the drive screen.
Item {
    id:             control
    objectName:     "toolbar_linkQualityIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          valuesRow.width

    property bool showIndicator: LinkQualityMonitor.monitoring

    readonly property bool _noReply:    LinkQualityMonitor.pingMs < 0
    readonly property bool _limiting:   LinkQualityMonitor.throttleLimitEnabled && LinkQualityMonitor.throttlePercent < 100
    readonly property color _color:     _noReply ? qgcPal.colorRed : (_limiting ? qgcPal.colorOrange : qgcPal.text)

    QGCPalette { id: qgcPal }

    Row {
        id:                     valuesRow
        anchors.verticalCenter: parent.verticalCenter
        spacing:                ScreenTools.defaultFontPixelWidth * 0.6

        QGCColoredImage {
            anchors.verticalCenter: parent.verticalCenter
            width:                  ScreenTools.defaultFontPixelHeight * 1.3
            height:                 width
            sourceSize.height:      height
            source:                 "/res/nx/Ping.svg"
            color:                  control._color
        }

        QGCLabel {
            anchors.verticalCenter: parent.verticalCenter
            text:                   control._noReply ? qsTr("no reply") : qsTr("%1 ms").arg(LinkQualityMonitor.pingMs)
            color:                  control._color
            font.weight:            Font.ExtraBold
            font.pointSize:         ScreenTools.mediumFontPointSize
        }
    }
}
