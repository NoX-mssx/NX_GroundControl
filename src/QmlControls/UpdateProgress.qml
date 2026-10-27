import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Download progress, the install notice and the last error of AppUpdater; shared by the update offer and
/// Settings → Updates
ColumnLayout {
    spacing: ScreenTools.defaultFontPixelHeight * 0.3
    visible: AppUpdater.state === "downloading" || AppUpdater.state === "installing" || AppUpdater.error !== ""

    function _megabytes(bytes) {
        const mb = (bytes || 0) / (1024 * 1024)
        return mb < 10 ? mb.toFixed(1) : mb.toFixed(0)
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            AppUpdater.state === "downloading"
        text:               qsTr("Downloading: %1 of %2 MB").arg(_megabytes(AppUpdater.received)).arg(_megabytes(AppUpdater.update.size))
    }
    Rectangle {
        Layout.fillWidth:       true
        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 0.3
        visible:                AppUpdater.state === "downloading"
        radius:                 height / 2
        color:                  Qt.rgba(1, 1, 1, 0.12)

        Rectangle {
            width:  parent.width * Math.min(1, AppUpdater.progress)
            height: parent.height
            radius: parent.radius
            color:  qgcPal.buttonHighlight
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            AppUpdater.state === "installing"
        wrapMode:           Text.WordWrap
        color:              qgcPal.colorGreen
        text:               qsTr("NX Rover closes now and installs the update. Windows asks for permission to make changes: answer Yes. NX Rover starts again by itself when the installation is done.")
    }

    QGCLabel {
        Layout.fillWidth:   true
        visible:            AppUpdater.error !== ""
        wrapMode:           Text.WordWrap
        color:              qgcPal.colorRed
        text:               AppUpdater.error
    }
}
