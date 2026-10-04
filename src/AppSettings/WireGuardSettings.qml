import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Editor for WireGuard tunnel profiles. A profile is a named tunnel that links can select; saving one
/// registers it with Windows, which asks for administrator rights.
Rectangle {
    id:             root
    objectName:     "settingsPage_WireGuard"
    color:          qgcPal.window
    anchors.fill:   parent

    readonly property real _margins:    ScreenTools.defaultFontPixelHeight
    readonly property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 60
    readonly property string _newProfileText: qsTr("New tunnel...")

    // Name of the stored profile being edited, empty while creating a new one
    property string _originalName:  ""
    property var    _edit:          ({})
    property string _publicKey:     ""
    property string _message:       ""

    function _setValue(key, value) {
        let updated = Object.assign({}, _edit)
        updated[key] = value
        _edit = updated
        if (key === "privateKey") {
            _publicKey = WireGuardTunnel.publicKey(value)
        }
    }

    function _load(profile) {
        _edit = Object.assign({}, profile)
        _publicKey = WireGuardTunnel.publicKey(_edit.privateKey ?? "")
    }

    function _selectProfile(name) {
        _message = ""
        let stored = name === "" ? null : WireGuardTunnel.profile(name)
        if (stored && stored.name !== undefined) {
            _originalName = name
            _load(stored)
        } else {
            // A new tunnel starts with its own key pair, so the public key can be given to the server at once.
            _originalName = ""
            _load({ name: "", privateKey: WireGuardTunnel.generatePrivateKey() })
        }
    }

    function _save() {
        _message = WireGuardTunnel.saveProfile(_originalName, _edit)
        if (_message === "") {
            _selectProfile(_edit.name.trim())
            _message = qsTr("Saved. The tunnel is registered with Windows.")
        }
    }

    function _delete() {
        _message = WireGuardTunnel.deleteProfile(_originalName)
        if (_message === "") {
            _selectProfile("")
        }
    }

    Component.onCompleted: _selectProfile(WireGuardTunnel.profileNames.length > 0 ? WireGuardTunnel.profileNames[0] : "")

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCFileDialog {
        id:             confFileDialog
        title:          qsTr("WireGuard Configuration")
        folder:         QGroundControl.settingsManager.appSettings.settingsSavePath
        nameFilters:    [ qsTr("WireGuard Configuration (*.conf)"), qsTr("All Files (*)") ]

        // Fills the fields from an existing tunnel file, keeping its key pair and the name typed so far.
        onAcceptedForLoad: (file) => {
            close()
            let imported = WireGuardTunnel.settingsFromConfFile(file)
            if (imported.privateKey === undefined) {
                root._message = qsTr("The file is not a WireGuard configuration.")
                return
            }
            imported.name = root._edit.name ?? ""
            root._load(imported)
            root._message = qsTr("Imported. Press Save to register the tunnel.")
        }
    }

    QGCFlickable {
        anchors.margins:    _margins
        anchors.fill:       parent
        contentWidth:       mainColumn.width
        contentHeight:      mainColumn.height
        clip:               true

        ColumnLayout {
            id:         mainColumn
            x:          Math.max(0, (parent.width - width) / 2)
            spacing:    _margins

            QGCLabel {
                Layout.preferredWidth:  _fieldWidth
                wrapMode:               Text.WordWrap
                color:                  qgcPal.colorOrange
                text:                   qsTr("WireGuard for Windows is not installed, so tunnels cannot be created.")
                visible:                !WireGuardTunnel.available
            }

            SettingsGroupLayout {
                Layout.preferredWidth:  _fieldWidth
                heading:                qsTr("WireGuard Tunnel")

                QGCComboBox {
                    Layout.fillWidth:   true
                    model:              WireGuardTunnel.profileNames.concat([ root._newProfileText ])
                    currentIndex:       root._originalName === "" ? count - 1 : Math.max(0, WireGuardTunnel.profileNames.indexOf(root._originalName))
                    onActivated: (index) => {
                        root._selectProfile(index < WireGuardTunnel.profileNames.length ? WireGuardTunnel.profileNames[index] : "")
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Delete")
                        enabled:            root._originalName !== ""
                        onClicked:          root._delete()
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Save")
                        primary:            true
                        enabled:            WireGuardTunnel.available
                        onClicked:          root._save()
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Import .conf...")
                        onClicked:          confFileDialog.openForLoad()
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("New Key")
                        onClicked:          root._setValue("privateKey", WireGuardTunnel.generatePrivateKey())
                    }
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    color:              qgcPal.colorOrange
                    text:               root._message
                    visible:            root._message !== ""
                }
            }

            SettingsGroupLayout {
                Layout.preferredWidth:  _fieldWidth
                heading:                qsTr("Tunnel Name")

                QGCTextField {
                    Layout.fillWidth:   true
                    text:               root._edit.name ?? ""
                    placeholderText:    qsTr("Tunnel name")
                    onTextEdited:       root._setValue("name", text)
                }
            }

            SettingsGroupLayout {
                Layout.preferredWidth:  _fieldWidth
                heading:                qsTr("This PC")
                headingDescription:     qsTr("Add this public key as a peer on the server.")

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               root._publicKey
                        readOnly:           true
                    }

                    QGCButton {
                        text:       qsTr("Copy")
                        enabled:    root._publicKey !== ""
                        onClicked:  QGroundControl.copyToClipboard(root._publicKey)
                    }
                }

                Repeater {
                    model: [
                        { key: "address",   label: qsTr("Address"),     hint: "10.30.1.252/24" },
                        { key: "dns",       label: qsTr("DNS"),         hint: qsTr("optional") },
                        { key: "mtu",       label: qsTr("MTU"),         hint: qsTr("optional") }
                    ]

                    RowLayout {
                        required property var modelData

                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 22
                            text:                   modelData.label
                        }

                        QGCTextField {
                            Layout.fillWidth:   true
                            text:               root._edit[modelData.key] ?? ""
                            placeholderText:    modelData.hint
                            onTextEdited:       root._setValue(modelData.key, text)
                        }
                    }
                }
            }

            SettingsGroupLayout {
                Layout.preferredWidth:  _fieldWidth
                heading:                qsTr("Server")

                Repeater {
                    model: [
                        { key: "peerPublicKey", label: qsTr("Public key"),              hint: "" },
                        { key: "endpoint",      label: qsTr("Endpoint"),                hint: "203.0.113.5:51821" },
                        { key: "allowedIps",    label: qsTr("Allowed IPs"),             hint: "10.30.1.0/24, 192.168.106.0/24" },
                        { key: "keepalive",     label: qsTr("Persistent keepalive"),    hint: "25" },
                        { key: "presharedKey",  label: qsTr("Preshared key"),           hint: qsTr("optional") }
                    ]

                    RowLayout {
                        required property var modelData

                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 22
                            text:                   modelData.label
                        }

                        QGCTextField {
                            Layout.fillWidth:   true
                            text:               root._edit[modelData.key] ?? ""
                            placeholderText:    modelData.hint
                            onTextEdited:       root._setValue(modelData.key, text)
                        }
                    }
                }
            }
        }
    }
}
