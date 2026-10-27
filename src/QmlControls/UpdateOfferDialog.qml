import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Offer of a newer NX Rover, shown once after the check at start: Decline or Update. Update downloads the
/// installer here (with progress) and then closes NX Rover to install it.
QGCPopupDialog {
    id:         offerDialog
    title:      qsTr("Update Available")
    buttons:    Dialog.NoButton

    readonly property var  _update:     AppUpdater.update
    readonly property bool _vehicleConnected: QGroundControl.multiVehicleManager.activeVehicle !== null

    function _megabytes(bytes) {
        const mb = (bytes || 0) / (1024 * 1024)
        return mb < 10 ? mb.toFixed(1) : mb.toFixed(0)
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ColumnLayout {
        width:      ScreenTools.defaultFontPixelWidth * 60
        spacing:    ScreenTools.defaultFontPixelHeight * 0.6

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.WordWrap
            text:               qsTr("NX Rover %1 is available (you have %2). Download size: %3 MB.")
                                    .arg(offerDialog._update.version || "")
                                    .arg(AppUpdater.currentVersion)
                                    .arg(offerDialog._megabytes(offerDialog._update.size))
        }

        Rectangle {
            Layout.fillWidth:       true
            Layout.preferredHeight: notesLabel.implicitHeight + ScreenTools.defaultFontPixelHeight
            visible:                (offerDialog._update.notes || "") !== ""
            radius:                 ScreenTools.defaultFontPixelHeight * 0.5
            color:                  Qt.rgba(1, 1, 1, 0.05)

            QGCLabel {
                id:                     notesLabel
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins:        ScreenTools.defaultFontPixelHeight * 0.5
                wrapMode:               Text.WordWrap
                textFormat:             Text.PlainText
                text:                   offerDialog._update.notes || ""
            }
        }

        UpdateProgress {
            Layout.fillWidth: true
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            AppUpdater.state === "available" && offerDialog._vehicleConnected
            wrapMode:           Text.WordWrap
            color:              qgcPal.colorOrange
            text:               qsTr("NX Rover closes to install the update: the vehicle will be disconnected.")
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            AppUpdater.state === "available"
            wrapMode:           Text.WordWrap
            color:              qgcPal.colorGrey
            font.pointSize:     ScreenTools.smallFontPointSize
            text:               qsTr("You can also update later in Settings → Updates.")
        }

        RowLayout {
            Layout.alignment:   Qt.AlignRight
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                text:       qsTr("Decline")
                visible:    AppUpdater.state !== "downloading" && AppUpdater.state !== "installing"
                onClicked:  offerDialog.close()
            }
            QGCButton {
                text:       qsTr("Cancel")
                visible:    AppUpdater.state === "downloading"
                onClicked:  AppUpdater.cancel()
            }
            QGCButton {
                text:               qsTr("Update")
                visible:            AppUpdater.state === "available"
                backgroundColor:    "#DB6536"
                textColor:          "#0B1F1D"
                fontWeight:         Font.DemiBold
                onClicked:          AppUpdater.install()
            }
        }
    }
}
