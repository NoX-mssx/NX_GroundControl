import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// NX: a short note that a save was taken, at the bottom of a settings page; it fades after a few seconds.
/// Put one in a page and call show() after a successful save.
Rectangle {
    id:                         toast
    anchors.horizontalCenter:   parent.horizontalCenter
    anchors.bottom:             parent.bottom
    anchors.bottomMargin:       ScreenTools.defaultFontPixelHeight * 5   // above the Save / Cancel row
    width:                      Math.min(toastRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 4,
                                         parent.width - ScreenTools.defaultFontPixelWidth * 4)
    height:                     toastRow.implicitHeight + ScreenTools.defaultFontPixelHeight
    radius:                     height / 2
    color:                      Qt.rgba(0.08, 0.10, 0.11, 0.95)
    border.color:               qgcPal.colorGreen
    border.width:               1
    opacity:                    0
    visible:                    opacity > 0
    z:                          100

    property int showMs: 3500

    function show(message) {
        toastLabel.text = message !== undefined && message !== "" ? message : qsTr("Saved")
        opacity = 1
        hideTimer.restart()
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    Behavior on opacity { NumberAnimation { duration: 250 } }

    Timer {
        id:             hideTimer
        interval:       toast.showMs
        onTriggered:    toast.opacity = 0
    }

    RowLayout {
        id:                 toastRow
        anchors.centerIn:   parent
        width:              Math.min(implicitWidth, toast.width - ScreenTools.defaultFontPixelWidth * 4)
        spacing:            ScreenTools.defaultFontPixelWidth

        QGCLabel {
            text:           "✓"
            color:          qgcPal.colorGreen
            font.weight:    Font.ExtraBold
            font.pointSize: ScreenTools.mediumFontPointSize
        }
        QGCLabel {
            id:                 toastLabel
            Layout.fillWidth:   true
            elide:              Text.ElideRight
            font.weight:        Font.DemiBold
        }
    }

    // A click hides it at once
    MouseArea {
        anchors.fill:   parent
        onClicked:      toast.opacity = 0
    }
}
