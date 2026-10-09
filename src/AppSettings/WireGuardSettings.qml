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

    // ---- Left: the stored tunnels ----
    ColumnLayout {
        id:                 listColumn
        anchors.left:       parent.left
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _margins
        width:              ScreenTools.defaultFontPixelWidth * 34
        spacing:            _margins * 0.6

        RowLayout {
            Layout.fillWidth:   true

            QGCLabel {
                Layout.fillWidth:   true
                text:               qsTr("Tunnels")
                font.weight:        Font.ExtraBold
                font.pointSize:     ScreenTools.largeFontPointSize
            }
            QGCButton {
                text:       qsTr("+ New")
                primary:    true
                onClicked:  root._selectProfile("")
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.WordWrap
            color:              qgcPal.colorOrange
            text:               qsTr("WireGuard for Windows is not installed, so tunnels cannot be created.")
            visible:            !WireGuardTunnel.available
        }

        QGCFlickable {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            contentHeight:      tunnelListColumn.height
            clip:               true

            Column {
                id:         tunnelListColumn
                width:      parent.width
                spacing:    _margins * 0.5

                Repeater {
                    model: WireGuardTunnel.profileNames

                    Rectangle {
                        id:             tunnelCard
                        width:          tunnelListColumn.width
                        height:         tunnelCardColumn.implicitHeight + ScreenTools.defaultFontPixelHeight * 1.2
                        radius:         ScreenTools.defaultFontPixelHeight
                        color:          _selected ? Qt.rgba(79 / 255, 209 / 255, 197 / 255, 0.08) : (tunnelMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.04))
                        border.color:   _selected ? qgcPal.buttonHighlight : Qt.rgba(1, 1, 1, 0.10)

                        required property string modelData

                        readonly property bool _selected:   modelData === root._originalName
                        readonly property var  _profile:    WireGuardTunnel.profile(modelData)

                        Column {
                            id:                     tunnelCardColumn
                            anchors.left:           parent.left
                            anchors.right:          parent.right
                            anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.6
                            anchors.rightMargin:    anchors.leftMargin
                            anchors.verticalCenter: parent.verticalCenter

                            QGCLabel {
                                width:          parent.width
                                text:           tunnelCard.modelData
                                font.weight:    Font.ExtraBold
                                font.pointSize: ScreenTools.mediumFontPointSize
                                elide:          Text.ElideRight
                            }
                            QGCLabel {
                                width:          parent.width
                                text:           [ tunnelCard._profile.address ?? "", tunnelCard._profile.endpoint ?? "" ].filter(function(part) { return part !== "" }).join(" · ")
                                color:          qgcPal.colorGrey
                                font.pointSize: ScreenTools.smallFontPointSize
                                elide:          Text.ElideRight
                                visible:        text !== ""
                            }
                        }

                        MouseArea {
                            id:             tunnelMouse
                            anchors.fill:   parent
                            hoverEnabled:   true
                            onClicked:      root._selectProfile(tunnelCard.modelData)
                        }
                    }
                }
            }
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Import .conf...")
            onClicked:          confFileDialog.openForLoad()
        }
    }

    // ---- Right: the tunnel being edited ----
    Rectangle {
        id:                 editorCard
        anchors.left:       listColumn.right
        anchors.right:      parent.right
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _margins
        radius:             ScreenTools.defaultFontPixelHeight
        color:              Qt.rgba(1, 1, 1, 0.04)
        border.color:       Qt.rgba(1, 1, 1, 0.10)

        QGCFlickable {
            id:                 editorFlickable
            anchors.fill:       parent
            anchors.margins:    _margins
            contentHeight:      mainColumn.height
            clip:               true

            ColumnLayout {
                id:         mainColumn
                width:      Math.min(editorFlickable.width, ScreenTools.defaultFontPixelWidth * 90)
                spacing:    _margins

                ColumnLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelHeight * 0.3

                    QGCLabel {
                        text:           qsTr("Tunnel name")
                        color:          qgcPal.colorGrey
                        font.weight:    Font.DemiBold
                    }
                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               root._edit.name ?? ""
                        placeholderText:    qsTr("Tunnel name")
                        font.weight:        Font.ExtraBold
                        onTextEdited:       root._setValue("name", text)
                    }
                }

                SettingsGroupLayout {
                    Layout.fillWidth:       true
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
                    Layout.fillWidth:       true
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

                QGCButton {
                    text:       qsTr("New Key")
                    onClicked:  root._setValue("privateKey", WireGuardTunnel.generatePrivateKey())
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    color:              qgcPal.colorOrange
                    text:               root._message
                    visible:            root._message !== ""
                }

                RowLayout {
                    Layout.fillWidth:   true

                    QGCButton {
                        text:       qsTr("Delete tunnel")
                        enabled:    root._originalName !== ""
                        textColor:  qgcPal.warningText
                        onClicked:  root._delete()
                    }
                    Item { Layout.fillWidth: true }
                    QGCButton {
                        text:       qsTr("Save")
                        primary:    true
                        fontWeight: Font.ExtraBold
                        enabled:    WireGuardTunnel.available
                        onClicked:  root._save()
                    }
                }
            }
        }
    }
}
