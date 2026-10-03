import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Round trip time to the vehicle's router, and the throttle limit applied while it is high.
Item {
    id:             control
    objectName:     "toolbar_linkQualityIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          valuesColumn.width

    property bool showIndicator: LinkQualityMonitor.monitoring

    readonly property bool _noReply:    LinkQualityMonitor.pingMs < 0
    readonly property bool _limiting:   LinkQualityMonitor.throttleLimitEnabled && LinkQualityMonitor.throttlePercent < 100

    QGCPalette { id: qgcPal }

    Column {
        id:                     valuesColumn
        anchors.verticalCenter: parent.verticalCenter
        spacing:                0

        QGCLabel {
            anchors.horizontalCenter:   parent.horizontalCenter
            text:                       qsTr("Ping")
            color:                      qgcPal.text
        }

        QGCLabel {
            anchors.horizontalCenter:   parent.horizontalCenter
            text:                       control._noReply ? qsTr("no reply") : qsTr("%1 ms").arg(LinkQualityMonitor.pingMs)
            color:                      control._noReply ? qgcPal.colorRed : (control._limiting ? qgcPal.colorOrange : qgcPal.text)
        }

        QGCLabel {
            anchors.horizontalCenter:   parent.horizontalCenter
            text:                       qsTr("Throttle %1%").arg(LinkQualityMonitor.throttlePercent)
            color:                      qgcPal.colorOrange
            visible:                    control._limiting
        }
    }
}
