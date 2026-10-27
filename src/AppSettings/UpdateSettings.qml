import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Updates page: the installed version, a check against the room servers and the update itself. A server admin
/// also publishes installers here for every computer of the server.
Rectangle {
    id:             root
    objectName:     "settingsPage_Updates"
    color:          "transparent"   // sits in the settings page card
    anchors.fill:   parent

    readonly property real  _margins:       ScreenTools.defaultFontPixelHeight
    readonly property color _cardColor:     Qt.rgba(1, 1, 1, 0.04)
    readonly property color _cardBorder:    Qt.rgba(1, 1, 1, 0.10)
    readonly property string _state:        AppUpdater.state
    readonly property var   _update:        AppUpdater.update
    readonly property var   _published:     AppUpdater.published
    readonly property var   _installer:     AppUpdater.installer
    readonly property bool  _haveUpdate:    _state === "available" || _state === "downloading" || _state === "installing"
    readonly property bool  _vehicleConnected: QGroundControl.multiVehicleManager.activeVehicle !== null

    function _megabytes(bytes) {
        const mb = (bytes || 0) / (1024 * 1024)
        return mb < 10 ? mb.toFixed(1) : mb.toFixed(0)
    }

    function _date(seconds) {
        return seconds ? Qt.formatDateTime(new Date(seconds * 1000), "dd.MM.yyyy HH:mm") : ""
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCFlickable {
        anchors.fill:       parent
        anchors.margins:    _margins
        contentHeight:      pageColumn.implicitHeight
        clip:               true

        ColumnLayout {
            id:     pageColumn
            width:  parent.width
            spacing: _margins * 0.6

            // ---- This computer ----
            Rectangle {
                Layout.fillWidth:       true
                Layout.preferredHeight: updateColumn.implicitHeight + _margins * 1.6
                radius:                 _margins
                color:                  _cardColor
                border.color:           _cardBorder

                ColumnLayout {
                    id:                     updateColumn
                    anchors.left:           parent.left
                    anchors.right:          parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin:     _margins
                    anchors.rightMargin:    _margins
                    spacing:                _margins * 0.5

                    RowLayout {
                        Layout.fillWidth:   true

                        ColumnLayout {
                            spacing: 0

                            QGCLabel {
                                text:           qsTr("NX Rover Updates")
                                font.weight:    Font.ExtraBold
                                font.pointSize: ScreenTools.largeFontPointSize
                            }
                            QGCLabel {
                                text:       qsTr("Installed version: %1").arg(AppUpdater.currentVersion)
                                color:      qgcPal.colorGrey
                            }
                        }
                        Item { Layout.fillWidth: true }
                        QGCButton {
                            text:       root._state === "checking" ? qsTr("Checking…") : qsTr("Check for updates")
                            enabled:    root._state !== "checking" && root._state !== "downloading" && root._state !== "installing"
                            onClicked:  AppUpdater.check()
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        visible:            !root._haveUpdate
                        wrapMode:           Text.WordWrap
                        color:              root._state === "upToDate" ? qgcPal.colorGreen
                                                : (root._state === "error" ? qgcPal.colorRed : qgcPal.colorGrey)
                        text: {
                            switch (root._state) {
                            case "checking":    return qsTr("Asking the room server…")
                            case "upToDate":    return qsTr("You have the latest version.")
                            case "noServer":    return qsTr("Updates come from the room server. This computer is not registered with one yet: join a room in Rover → Rooms.")
                            case "error":       return AppUpdater.error
                            default:            return qsTr("Updates are checked at every start.")
                            }
                        }
                    }

                    // ---- A newer version ----
                    ColumnLayout {
                        Layout.fillWidth:   true
                        visible:            root._haveUpdate
                        spacing:            _margins * 0.4

                        QGCLabel {
                            Layout.fillWidth:   true
                            wrapMode:           Text.WordWrap
                            font.weight:        Font.DemiBold
                            color:              qgcPal.colorOrange
                            text:               qsTr("Version %1 is available (%2 MB, published %3).")
                                                    .arg(root._update.version || "")
                                                    .arg(root._megabytes(root._update.size))
                                                    .arg(root._date(root._update.published))
                        }

                        Rectangle {
                            Layout.fillWidth:       true
                            Layout.preferredHeight: notesLabel.implicitHeight + _margins
                            visible:                (root._update.notes || "") !== ""
                            radius:                 _margins * 0.5
                            color:                  Qt.rgba(1, 1, 1, 0.05)

                            QGCLabel {
                                id:                 notesLabel
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins:    _margins * 0.5
                                wrapMode:           Text.WordWrap
                                textFormat:         Text.PlainText
                                text:               root._update.notes || ""
                            }
                        }

                        UpdateProgress {
                            Layout.fillWidth:   true
                        }

                        QGCLabel {
                            Layout.fillWidth:   true
                            visible:            root._state === "available" && root._vehicleConnected
                            wrapMode:           Text.WordWrap
                            color:              qgcPal.colorOrange
                            text:               qsTr("NX Rover closes to install the update: the vehicle will be disconnected.")
                        }

                        RowLayout {
                            spacing: ScreenTools.defaultFontPixelWidth

                            QGCButton {
                                text:               qsTr("Update")
                                visible:            root._state === "available"
                                backgroundColor:    "#DB6536"
                                textColor:          "#0B1F1D"
                                fontWeight:         Font.DemiBold
                                onClicked:          AppUpdater.install()
                            }
                            QGCButton {
                                text:       qsTr("Cancel")
                                visible:    root._state === "downloading"
                                onClicked:  AppUpdater.cancel()
                            }
                        }
                    }
                }
            }

            // ---- Publishing (server admin) ----
            Rectangle {
                Layout.fillWidth:       true
                Layout.preferredHeight: publishColumn.implicitHeight + _margins * 1.6
                visible:                AppUpdater.canPublish
                radius:                 _margins
                color:                  _cardColor
                border.color:           _cardBorder

                ColumnLayout {
                    id:                     publishColumn
                    anchors.left:           parent.left
                    anchors.right:          parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin:     _margins
                    anchors.rightMargin:    _margins
                    spacing:                _margins * 0.5

                    readonly property bool  _uploading:     AppUpdater.publishState === "uploading"
                    readonly property string _newVersion:   root._installer.version || ""
                    readonly property bool  _notNewer:      _newVersion !== "" && root._published.available === true
                                                            && root._published.version !== undefined
                                                            && !_isNewer(_newVersion, root._published.version)

                    function _isNewer(a, b) {
                        const left = a.split(".").map(Number)
                        const right = b.split(".").map(Number)
                        for (let i = 0; i < Math.max(left.length, right.length); i++) {
                            const l = left[i] || 0
                            const r = right[i] || 0
                            if (l !== r) {
                                return l > r
                            }
                        }
                        return false
                    }

                    QGCLabel {
                        text:           qsTr("Publish an Update")
                        font.weight:    Font.ExtraBold
                        font.pointSize: ScreenTools.largeFontPointSize
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorGrey
                        text: root._published.available === undefined ? qsTr("Press Check for updates to see what the server has.")
                              : (root._published.available
                                 ? qsTr("On the server: version %1, published %2 by %3.").arg(root._published.version).arg(root._date(root._published.published)).arg(root._published.by || "")
                                 : qsTr("The server has no update. Every computer of the server (admins and operators) gets what you publish here."))
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCButton {
                            text:       qsTr("Choose installer…")
                            enabled:    !publishColumn._uploading
                            onClicked:  installerDialog.open()
                        }
                        QGCLabel {
                            Layout.fillWidth:   true
                            elide:              Text.ElideMiddle
                            visible:            (root._installer.path || "") !== ""
                            text:               (root._installer.name || "") + " · " + qsTr("%1 MB").arg(root._megabytes(root._installer.size))
                                                + (publishColumn._newVersion !== "" ? " · " + qsTr("version %1").arg(publishColumn._newVersion) : "")
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        visible:            (root._installer.path || "") !== "" && publishColumn._newVersion === ""
                        color:              qgcPal.colorRed
                        text:               qsTr("This file carries no version: it is not an NX Rover installer, or it was built before updates were supported.")
                    }
                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        visible:            publishColumn._notNewer
                        color:              qgcPal.colorOrange
                        text:               qsTr("This version is not newer than the one on the server: computers that already have a newer version will not take it.")
                    }

                    QGCLabel {
                        text:       qsTr("What is new (operators see it with the offer)")
                        visible:    publishColumn._newVersion !== ""
                    }
                    TextArea {
                        id:                     notesArea
                        Layout.fillWidth:       true
                        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 4
                        visible:                publishColumn._newVersion !== ""
                        enabled:                !publishColumn._uploading
                        wrapMode:               TextEdit.Wrap
                        font.pointSize:         ScreenTools.defaultFontPointSize
                        color:                  qgcPal.textFieldText
                        background:             Rectangle { color: qgcPal.textField; radius: ScreenTools.defaultFontPixelHeight * 0.3 }
                    }

                    // Upload progress
                    ColumnLayout {
                        Layout.fillWidth:   true
                        visible:            publishColumn._uploading
                        spacing:            ScreenTools.defaultFontPixelHeight * 0.2

                        QGCLabel {
                            text: qsTr("Sending to the server: %1%").arg(Math.round(AppUpdater.publishProgress * 100))
                        }
                        Rectangle {
                            Layout.fillWidth:       true
                            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 0.3
                            radius:                 height / 2
                            color:                  Qt.rgba(1, 1, 1, 0.12)

                            Rectangle {
                                width:  parent.width * Math.min(1, AppUpdater.publishProgress)
                                height: parent.height
                                radius: parent.radius
                                color:  qgcPal.buttonHighlight
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        visible:            AppUpdater.publishState === "done" || AppUpdater.publishState === "error"
                        color:              AppUpdater.publishState === "done" ? qgcPal.colorGreen : qgcPal.colorRed
                        text:               AppUpdater.publishState === "done"
                                                ? qsTr("Published: version %1. Computers get the offer at their next start or check.").arg(root._published.version || "")
                                                : AppUpdater.publishError
                    }

                    RowLayout {
                        spacing: ScreenTools.defaultFontPixelWidth

                        QGCButton {
                            text:       qsTr("Publish")
                            primary:    true
                            visible:    !publishColumn._uploading && (root._installer.path || "") !== ""
                            enabled:    publishColumn._newVersion !== ""
                            onClicked:  AppUpdater.publish(notesArea.text)
                        }
                        QGCButton {
                            text:       qsTr("Cancel")
                            visible:    publishColumn._uploading
                            onClicked:  AppUpdater.cancelPublish()
                        }
                        QGCButton {
                            text:       qsTr("Remove from server")
                            visible:    root._published.available === true && !publishColumn._uploading
                            onClicked: {
                                QGroundControl.showMessageDialog(
                                    root,
                                    qsTr("Remove Update"),
                                    qsTr("Version %1 will no longer be offered. Computers that installed it keep it.").arg(root._published.version),
                                    Dialog.Ok | Dialog.Cancel,
                                    function () { AppUpdater.unpublish() })
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id:             installerDialog
        title:          qsTr("Choose NX Rover Installer")
        fileMode:       FileDialog.OpenFile
        nameFilters:    [ qsTr("Installer (*.exe)"), qsTr("All files (*)") ]
        onAccepted:     AppUpdater.chooseInstaller(selectedFile)
    }
}
