import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// Links page: the configured links as cards on the left, the selected one edited on the right.
Rectangle {
    id:             root
    objectName:     "settingsPage_CommLinks"
    color:          "transparent"   // sits in the settings page card
    anchors.fill:   parent

    readonly property real _margins:    ScreenTools.defaultFontPixelHeight
    property var _linkManager:          QGroundControl.linkManager

    // Stored configuration selected in the list (null while adding a new link)
    property var originalConfig:    null
    // Working copy being edited (null when nothing is open)
    property var editingConfig:     null

    function _closeEditor() {
        if (editingConfig) {
            _linkManager.cancelConfigurationEditing(editingConfig)
        }
        editingConfig = null
        originalConfig = null
        editorLoader.active = false
    }

    function _openEditor(config) {
        _closeEditor()
        originalConfig = config
        if (config && config.link) {
            // A connected link is shown but not edited; disconnect it first.
            editorLoader.active = true
            return
        }
        editingConfig = config ? _linkManager.startConfigurationEditing(config)
                               : _linkManager.createConfiguration(LinkConfiguration.TypeUdp, "")
        editorLoader.active = true
    }

    function _save() {
        if (!editingConfig || !editorLoader.item) {
            return
        }
        let editor = editorLoader.item
        if (editor.linkSettings) {
            editor.linkSettings.saveSettings()
        }
        editingConfig.name = editor.nameText
        if (originalConfig) {
            _linkManager.endConfigurationEditing(originalConfig, editingConfig)
        } else {
            editingConfig.dynamic = false
            _linkManager.endCreateConfiguration(editingConfig)
        }
        // The edited copy now belongs to the link manager.
        editingConfig = null
        originalConfig = null
        editorLoader.active = false
    }

    function _subtitle(config) {
        let parts = [ _linkManager.linkTypeStrings[config.linkType] ]
        if (config.vehicleModel !== "") parts.push(config.vehicleModel)
        if (config.wireGuardProfile !== "") parts.push(config.wireGuardProfile)
        if (config.boardId !== "") parts.push(qsTr("ID %1").arg(config.boardId))
        return parts.join(" · ")
    }

    Component.onDestruction: {
        if (editingConfig) {
            _linkManager.cancelConfigurationEditing(editingConfig)
        }
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // ---- Left: the configured links ----
    ColumnLayout {
        id:                 listColumn
        anchors.left:       parent.left
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _margins
        width:              ScreenTools.defaultFontPixelWidth * 40
        spacing:            _margins * 0.6

        RowLayout {
            Layout.fillWidth:   true

            QGCLabel {
                Layout.fillWidth:   true
                text:               qsTr("Links")
                font.weight:        Font.ExtraBold
                font.pointSize:     ScreenTools.largeFontPointSize
            }
            QGCButton {
                text:       qsTr("+ Add")
                primary:    true
                onClicked:  root._openEditor(null)
            }
        }

        QGCFlickable {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            contentHeight:      linkListColumn.height
            clip:               true

            Column {
                id:         linkListColumn
                width:      parent.width
                spacing:    _margins * 0.5

                Repeater {
                    model: _linkManager.linkConfigurations

                    Rectangle {
                        id:         linkCard
                        width:      linkListColumn.width
                        height:     linkCardRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 1.2
                        radius:     ScreenTools.defaultFontPixelHeight
                        visible:    !object.dynamic
                        color:      _selected ? Qt.rgba(79 / 255, 209 / 255, 197 / 255, 0.08) : (linkCardMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.04))
                        border.color: _selected ? qgcPal.buttonHighlight : Qt.rgba(1, 1, 1, 0.10)

                        readonly property bool _selected: root.originalConfig === object

                        MouseArea {
                            id:             linkCardMouse
                            anchors.fill:   parent
                            hoverEnabled:   true
                            onClicked:      root._openEditor(object)
                        }

                        RowLayout {
                            id:                     linkCardRow
                            anchors.left:           parent.left
                            anchors.right:          parent.right
                            anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.6
                            anchors.rightMargin:    ScreenTools.defaultFontPixelWidth
                            anchors.verticalCenter: parent.verticalCenter
                            spacing:                ScreenTools.defaultFontPixelWidth * 1.2

                            Rectangle {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.55
                                Layout.preferredHeight: Layout.preferredWidth
                                radius:                 width / 2
                                color:                  object.link ? qgcPal.buttonHighlight : Qt.rgba(1, 1, 1, 0.3)
                            }

                            Column {
                                Layout.fillWidth: true

                                QGCLabel {
                                    width:          parent.width
                                    text:           object.name
                                    font.weight:    Font.ExtraBold
                                    font.pointSize: ScreenTools.mediumFontPointSize
                                    elide:          Text.ElideRight
                                }
                                QGCLabel {
                                    width:          parent.width
                                    text:           root._subtitle(object)
                                    color:          qgcPal.colorGrey
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    elide:          Text.ElideRight
                                }
                            }

                            QGCButton {
                                text:       object.linkActive ? qsTr("Disconnect") : qsTr("Connect")
                                onClicked: {
                                    if (object.linkActive) {
                                        root._linkManager.disconnectLinkConfiguration(object)
                                    } else {
                                        if (root.originalConfig === object) {
                                            root._closeEditor()
                                        }
                                        root._linkManager.createConnectedLink(object)
                                    }
                                }
                            }
                        }
                    }
                }

                QGCLabel {
                    width:      parent.width
                    wrapMode:   Text.WordWrap
                    text:       qsTr("No links yet. Add one to connect to a vehicle.")
                    color:      qgcPal.colorGrey
                    visible:    _linkManager.linkConfigurations.count === 0
                }
            }
        }
    }

    // ---- Right: editor ----
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

        QGCLabel {
            anchors.centerIn:   parent
            text:               qsTr("Select a link on the left, or add a new one.")
            color:              qgcPal.colorGrey
            visible:            !editorLoader.active
        }

        Loader {
            id:                 editorLoader
            anchors.fill:       parent
            anchors.margins:    _margins
            active:             false
            sourceComponent:    root.editingConfig ? editorComponent : connectedComponent
        }
    }

    // Shown for a connected link
    Component {
        id: connectedComponent

        ColumnLayout {
            spacing: _margins

            QGCLabel {
                text:           root.originalConfig ? root.originalConfig.name : ""
                font.weight:    Font.ExtraBold
                font.pointSize: ScreenTools.largeFontPointSize
            }
            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                text:               qsTr("This link is connected. Disconnect it to change its settings.")
                color:              qgcPal.colorGrey
            }
            Item { Layout.fillHeight: true }
        }
    }

    Component {
        id: editorComponent

        Item {
            property alias nameText:    nameField.text
            property alias linkSettings: linkSettingsLoader.item

            QGCFlickable {
                id:                 editorFlickable
                anchors.left:       parent.left
                anchors.right:      parent.right
                anchors.top:        parent.top
                anchors.bottom:     footerRow.top
                anchors.bottomMargin: _margins
                contentHeight:      editorColumn.height
                clip:               true

                ColumnLayout {
                    id:         editorColumn
                    width:      Math.min(editorFlickable.width, ScreenTools.defaultFontPixelWidth * 90)
                    spacing:    ScreenTools.defaultFontPixelHeight / 2

                    QGCLabel {
                        text:           root.originalConfig ? root.originalConfig.name : qsTr("New link")
                        font.weight:    Font.ExtraBold
                        font.pointSize: ScreenTools.largeFontPointSize
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Name") }
                        QGCTextField {
                            id:                 nameField
                            Layout.fillWidth:   true
                            text:               root.editingConfig.name
                            placeholderText:    qsTr("Enter name")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Board ID") }
                        QGCTextField {
                            Layout.fillWidth:   true
                            text:               root.editingConfig.boardId
                            placeholderText:    qsTr("Board number, e.g. 104")
                            onTextEdited:       root.editingConfig.boardId = text.trim()
                        }
                    }

                    LabelledComboBox {
                        id:         vehicleModelCombo
                        label:      qsTr("Vehicle Model")
                        model:      [ qsTr("None") ].concat(VehicleModelManager.modelNames)
                        // Index 0 is "None"; a model deleted since the link was saved also falls back to it.
                        Component.onCompleted: currentIndex = VehicleModelManager.modelNames.indexOf(root.editingConfig.vehicleModel) + 1
                        onActivated: (index) => root.editingConfig.vehicleModel = index > 0 ? VehicleModelManager.modelNames[index - 1] : ""
                    }

                    LabelledComboBox {
                        label:      qsTr("WireGuard Tunnel")
                        model:      [ qsTr("None") ].concat(WireGuardTunnel.profileNames)
                        visible:    WireGuardTunnel.available
                        // Index 0 is "None"; a tunnel deleted since the link was saved also falls back to it.
                        Component.onCompleted: currentIndex = WireGuardTunnel.profileNames.indexOf(root.editingConfig.wireGuardProfile) + 1
                        onActivated: (index) => root.editingConfig.wireGuardProfile = index > 0 ? WireGuardTunnel.profileNames[index - 1] : ""
                    }

                    QGCCheckBoxSlider {
                        Layout.fillWidth:   true
                        text:               qsTr("Automatically Connect on Start")
                        checked:            root.editingConfig.autoConnect
                        onCheckedChanged:   root.editingConfig.autoConnect = checked
                    }

                    // Stock option for very slow links: it stops joystick control altogether, which is never
                    // wanted here, so it is not offered.
                    QGCCheckBoxSlider {
                        Layout.fillWidth:   true
                        text:               qsTr("High Latency")
                        checked:            root.editingConfig.highLatency
                        onCheckedChanged:   root.editingConfig.highLatency = checked
                        visible:            false
                    }

                    SettingsGroupLayout {
                        Layout.fillWidth:   true
                        heading:            qsTr("Link Quality")

                        RowLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelWidth

                            QGCLabel { text: qsTr("Ping address") }
                            QGCTextField {
                                Layout.fillWidth:   true
                                text:               root.editingConfig.pingAddress
                                placeholderText:    qsTr("Vehicle router, e.g. 10.30.1.101")
                                onTextEdited:       root.editingConfig.pingAddress = text.trim()
                            }
                        }

                        QGCCheckBoxSlider {
                            Layout.fillWidth:   true
                            text:               qsTr("Limit throttle on high ping")
                            checked:            root.editingConfig.throttleLimitEnabled
                            onCheckedChanged:   root.editingConfig.throttleLimitEnabled = checked
                        }

                        RowLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelWidth
                            enabled:            root.editingConfig.throttleLimitEnabled

                            QGCLabel { text: qsTr("Above") }
                            QGCTextField {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 8
                                text:                   root.editingConfig.pingThreshold1Ms
                                numericValuesOnly:      true
                                validator:              IntValidator { bottom: 1; top: 10000 }
                                onTextEdited:           root.editingConfig.pingThreshold1Ms = parseInt(text) || 0
                            }
                            QGCLabel { text: qsTr("ms throttle") }
                            QGCTextField {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 6
                                text:                   root.editingConfig.throttlePercent1
                                numericValuesOnly:      true
                                validator:              IntValidator { bottom: 0; top: 100 }
                                onTextEdited:           root.editingConfig.throttlePercent1 = parseInt(text) || 0
                            }
                            QGCLabel { text: qsTr("%") }
                        }

                        RowLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelWidth
                            enabled:            root.editingConfig.throttleLimitEnabled

                            QGCLabel { text: qsTr("Above") }
                            QGCTextField {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 8
                                text:                   root.editingConfig.pingThreshold2Ms
                                numericValuesOnly:      true
                                validator:              IntValidator { bottom: 1; top: 10000 }
                                onTextEdited:           root.editingConfig.pingThreshold2Ms = parseInt(text) || 0
                            }
                            QGCLabel { text: qsTr("ms throttle") }
                            QGCTextField {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 6
                                text:                   root.editingConfig.throttlePercent2
                                numericValuesOnly:      true
                                validator:              IntValidator { bottom: 0; top: 100 }
                                onTextEdited:           root.editingConfig.throttlePercent2 = parseInt(text) || 0
                            }
                            QGCLabel { text: qsTr("%") }
                        }
                    }

                    LabelledComboBox {
                        label:                  qsTr("Type")
                        enabled:                root.originalConfig == null
                        model:                  _linkManager.linkTypeStrings
                        Component.onCompleted:  comboBox.currentIndex = root.editingConfig.linkType

                        onActivated: (index) => {
                            if (index !== root.editingConfig.linkType) {
                                var name = nameField.text
                                root.editingConfig = root._linkManager.createConfiguration(index, name)
                            }
                        }
                    }

                    Loader {
                        id:     linkSettingsLoader
                        source: root.editingConfig && root.editingConfig.settingsURL ? root.editingConfig.settingsURL : ""
                        asynchronous: true

                        property var subEditConfig:         root.editingConfig
                        property int _firstColumnWidth:     ScreenTools.defaultFontPixelWidth * 12
                        property int _secondColumnWidth:    ScreenTools.defaultFontPixelWidth * 30
                        property int _rowSpacing:           ScreenTools.defaultFontPixelHeight / 2
                        property int _colSpacing:           ScreenTools.defaultFontPixelWidth / 2

                        onStatusChanged: {
                            if (status === Loader.Error) {
                                console.warn("Failed to load link settings page:", source)
                            }
                        }
                    }
                }
            }

            RowLayout {
                id:             footerRow
                anchors.left:   parent.left
                anchors.right:  parent.right
                anchors.bottom: parent.bottom

                QGCButton {
                    text:       qsTr("Delete link")
                    visible:    root.originalConfig !== null
                    textColor:  qgcPal.warningText
                    onClicked: {
                        let config = root.originalConfig
                        QGroundControl.showMessageDialog(
                            root,
                            qsTr("Delete Link"),
                            qsTr("Are you sure you want to delete '%1'?").arg(config.name),
                            Dialog.Ok | Dialog.Cancel,
                            function () {
                                root._closeEditor()
                                root._linkManager.removeConfiguration(config)
                            })
                    }
                }
                Item { Layout.fillWidth: true }
                QGCButton {
                    text:       qsTr("Cancel")
                    onClicked:  root._closeEditor()
                }
                QGCButton {
                    text:       qsTr("Save")
                    primary:    true
                    fontWeight: Font.ExtraBold
                    enabled:    nameField.text !== ""
                    onClicked:  root._save()
                }
            }
        }
    }
}
