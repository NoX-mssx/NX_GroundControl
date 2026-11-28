import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Editor for WireGuard tunnel profiles. A profile is a named tunnel that links can select; saving one
/// registers it with Windows, which asks for administrator rights. As in WireGuard for Windows, a tunnel is edited
/// as its .conf text: pasted whole, or typed.
Rectangle {
    id:             root
    objectName:     "settingsPage_WireGuard"
    color:          "transparent"   // sits in the settings page card
    anchors.fill:   parent

    readonly property real _margins:    ScreenTools.defaultFontPixelHeight
    readonly property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 60
    /// Room whose tunnels are listed, as in WireGuardTunnel (see RoomScope)
    readonly property string _scope:    RoomManager.currentRole === "admin" ? RoomManager.currentRoom : ""
    readonly property string _newProfileText: qsTr("New tunnel...")

    // Name of the stored profile being edited, empty while creating a new one
    property string _originalName:  ""
    property var    _edit:          ({})
    property string _publicKey:     ""
    property string _message:       ""
    /// The text as read: { settings, missing, ignored } (WireGuardTunnel.parseConfText)
    property var    _parsed:        ({ settings: {}, missing: [], ignored: [] })
    property string _keyOfPublicKey: ""

    function _setValue(key, value) {
        let updated = Object.assign({}, _edit)
        updated[key] = value
        _edit = updated
    }

    function _load(profile) {
        _edit = Object.assign({}, profile)
        confArea.text = WireGuardTunnel.confText(profile)
    }

    function _textEdited(text) {
        _parsed = WireGuardTunnel.parseConfText(text)
        // The public key comes from the wg tool: only for a new private key, not on every key press
        const privateKey = _parsed.settings.privateKey ?? ""
        if (privateKey !== _keyOfPublicKey) {
            _keyOfPublicKey = privateKey
            _publicKey = privateKey === "" ? "" : WireGuardTunnel.publicKey(privateKey)
        }
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
        if (_parsed.missing.length > 0) {
            _message = qsTr("The text lacks: %1.").arg(_parsed.missing.join(", "))
            return
        }
        const profile = Object.assign({}, _parsed.settings, { name: _edit.name ?? "" })
        _message = WireGuardTunnel.saveProfile(_originalName, profile)
        if (_message === "") {
            const savedName = profile.name.trim()
            _selectProfile(savedName)
            savedToast.show(qsTr("Tunnel \"%1\" saved and registered with Windows").arg(savedName))
            // Room vehicles using the tunnel get the change
            RoomManager.publishRoomLinks()
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

    // Each room has its own tunnels: another room opens its first one
    on_ScopeChanged: _selectProfile(WireGuardTunnel.profileNames.length > 0 ? WireGuardTunnel.profileNames[0] : "")

    // Deleting asks first
    QGCPopupDialogFactory {
        id:                 confirmFactory
        dialogComponent:    confirmComponent
    }

    Component {
        id: confirmComponent

        NxConfirmDialog { }
    }

    QGCFileDialog {
        id:                 confFileDialog
        title:              qsTr("WireGuard Configuration")
        folder:             QGroundControl.settingsManager.appSettings.settingsSavePath
        rememberFolderKey:  "NxWireGuardFilesFolder"
        nameFilters:        [ qsTr("WireGuard Configuration (*.conf)"), qsTr("All Files (*)") ]

        // Puts the file's text in the editor, keeping the name typed so far
        onAcceptedForLoad: (file) => {
            close()
            const text = WireGuardTunnel.readConfFile(file)
            if (WireGuardTunnel.parseConfText(text).settings.privateKey === undefined) {
                root._message = qsTr("The file is not a WireGuard configuration.")
                return
            }
            confArea.text = text
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
                        required property int index

                        z:          tunnelDrag.active ? 10 : 0
                        opacity:    tunnelDrag.active ? 0.85 : 1
                        transform:  Translate { y: tunnelDrag.active ? tunnelDrag.activeTranslation.y : 0 }

                        // Hold the card and drag it up or down to change the order
                        // target is null: the card is moved visually only, the list order decides where it ends up
                        DragHandler {
                            id:                 tunnelDrag
                            target:             null
                            xAxis.enabled:      false
                            grabPermissions:    PointerHandler.CanTakeOverFromAnything

                            property real lastY: 0

                            onActiveTranslationChanged: if (active) lastY = activeTranslation.y
                            onActiveChanged: {
                                if (!active) {
                                    const steps = Math.round(lastY / (tunnelCard.height + tunnelListColumn.spacing))
                                    lastY = 0
                                    WireGuardTunnel.moveProfile(tunnelCard.index, Math.max(0, Math.min(WireGuardTunnel.profileNames.length - 1, tunnelCard.index + steps)))
                                }
                            }
                        }

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
            anchors.left:       parent.left
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.bottom:     editorFooter.top
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
                    headingDescription:     qsTr("Add this public key as a peer on the server. It comes from the PrivateKey line below.")

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCTextField {
                            Layout.fillWidth:   true
                            text:               root._publicKey
                            placeholderText:    (root._parsed.settings.privateKey ?? "") === "" ? qsTr("no PrivateKey in the text")
                                                                                                 : qsTr("not a valid PrivateKey")
                            readOnly:           true
                        }

                        QGCButton {
                            text:       qsTr("Copy")
                            enabled:    root._publicKey !== ""
                            onClicked:  QGroundControl.copyToClipboard(root._publicKey)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelHeight * 0.3

                    QGCLabel {
                        text:           qsTr("Configuration")
                        color:          qgcPal.colorGrey
                        font.weight:    Font.DemiBold
                    }
                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorGrey
                        font.pointSize:     ScreenTools.smallFontPointSize
                        text:               qsTr("As in WireGuard: paste the whole tunnel text, or edit it here. [Interface]: PrivateKey, Address, DNS, MTU. [Peer]: PublicKey, PresharedKey, AllowedIPs, Endpoint, PersistentKeepalive.")
                    }

                    // The whole tunnel as .conf text, as the WireGuard for Windows editor shows it
                    TextArea {
                        id:                     confArea
                        Layout.fillWidth:       true
                        Layout.preferredHeight: Math.max(ScreenTools.defaultFontPixelHeight * 16, contentHeight + topPadding + bottomPadding)
                        wrapMode:               TextEdit.WrapAtWordBoundaryOrAnywhere
                        selectByMouse:          true
                        font.family:            ScreenTools.fixedFontFamily
                        font.pointSize:         ScreenTools.defaultFontPointSize
                        color:                  qgcPal.textFieldText
                        placeholderText:        "[Interface]\nPrivateKey = …\nAddress = 10.30.1.252/24\n\n[Peer]\nPublicKey = …\nAllowedIPs = 10.30.1.0/24\nEndpoint = 203.0.113.5:51821\nPersistentKeepalive = 25"
                        background: Rectangle {
                            color:          qgcPal.textField
                            radius:         ScreenTools.defaultFontPixelHeight * 0.3
                            border.color:   root._parsed.missing.length > 0 ? qgcPal.colorOrange : Qt.rgba(1, 1, 1, 0.10)
                        }
                        onTextChanged: root._textEdited(text)
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorOrange
                        text:               qsTr("Missing: %1").arg(root._parsed.missing.join(", "))
                        visible:            root._parsed.missing.length > 0 && confArea.text.trim() !== ""
                    }
                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorGrey
                        text:               qsTr("Not kept: %1").arg(root._parsed.ignored.join("; "))
                        visible:            root._parsed.ignored.length > 0
                    }
                }
            }
        }

        // Message and buttons stay at the bottom while the settings above scroll
        ColumnLayout {
            id:                 editorFooter
            anchors.left:       parent.left
            anchors.right:      parent.right
            anchors.bottom:     parent.bottom
            anchors.margins:    _margins
            spacing:            _margins * 0.5

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
                    text:               qsTr("Delete tunnel")
                    enabled:            root._originalName !== ""
                    textColor:          qgcPal.warningText
                    onClicked: confirmFactory.open({
                        title:      qsTr("Delete Tunnel"),
                        message:    qsTr("Delete the WireGuard tunnel \"%1\"? Windows asks for administrator rights to remove it, and links that use it can no longer connect through it.").arg(root._originalName),
                        action:     function () { root._delete() }
                    })
                }
                Item { Layout.fillWidth: true }
                QGCButton {
                    text:               qsTr("Cancel")
                    onClicked:          root._selectProfile(root._originalName)
                }
                QGCButton {
                    text:               qsTr("Save")
                    primary:            true
                    fontWeight:         Font.ExtraBold
                    enabled:            WireGuardTunnel.available
                    onClicked:          root._save()
                }
            }
        }
    }

    // "Saved" after a save, over the page's bottom
    NxSavedToast { id: savedToast }
}
