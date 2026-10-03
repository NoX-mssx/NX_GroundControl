import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

SettingsGroupLayout {
    id: _root
    heading: qsTr("Links")

    property var _linkManager: QGroundControl.linkManager

    Repeater {
        model: _linkManager.linkConfigurations

        RowLayout {
            Layout.fillWidth:   true
            visible:            !object.dynamic

            QGCLabel {
                Layout.fillWidth:   true
                text:               object.name
            }
            QGCColoredImage {
                height:                 ScreenTools.minTouchPixels
                width:                  height
                sourceSize.height:      height
                fillMode:               Image.PreserveAspectFit
                mipmap:                 true
                smooth:                 true
                color:                  qgcPalEdit.text
                source:                 "/res/pencil.svg"
                enabled:                !object.link

                QGCPalette {
                    id: qgcPalEdit
                    colorGroupEnabled: parent.enabled
                }

                QGCMouseArea {
                    fillItem: parent
                    onClicked: {
                        var editingConfig = _linkManager.startConfigurationEditing(object)
                        linkDialogFactory.open({ editingConfig: editingConfig, originalConfig: object })
                    }
                }
            }
            QGCColoredImage {
                height:                 ScreenTools.minTouchPixels
                width:                  height
                sourceSize.height:      height
                fillMode:               Image.PreserveAspectFit
                mipmap:                 true
                smooth:                 true
                color:                  qgcPalDelete.text
                source:                 "/res/TrashDelete.svg"

                QGCPalette {
                    id: qgcPalDelete
                    colorGroupEnabled: parent.enabled
                }

                QGCMouseArea {
                    fillItem:   parent
                    onClicked:  QGroundControl.showMessageDialog(
                                    _root,
                                    qsTr("Delete Link"),
                                    qsTr("Are you sure you want to delete '%1'?").arg(object.name),
                                    Dialog.Ok | Dialog.Cancel,
                                    function () {
                                        _linkManager.removeConfiguration(object)
                                    })
                }
            }
            QGCButton {
                text:       object.linkActive ? qsTr("Disconnect") : qsTr("Connect")
                onClicked: {
                    if (object.linkActive) {
                        _linkManager.disconnectLinkConfiguration(object)
                    } else {
                        _linkManager.createConnectedLink(object)
                    }
                }
            }
        }
    }

    LabelledButton {
        label:      qsTr("Add New Link")
        buttonText: qsTr("Add")

        onClicked: {
            var editingConfig = _linkManager.createConfiguration(ScreenTools.isSerialAvailable ? LinkConfiguration.TypeSerial : LinkConfiguration.TypeUdp, "")
            linkDialogFactory.open({ editingConfig: editingConfig, originalConfig: null })
        }
    }

    QGCPopupDialogFactory {
        id: linkDialogFactory

        dialogComponent: linkDialogComponent
    }

    Component {
        id: linkDialogComponent

        QGCPopupDialog {
            title:                  originalConfig ? qsTr("Edit Link") : qsTr("Add New Link")
            buttons:                Dialog.Save | Dialog.Cancel
            acceptButtonEnabled:    nameField.text !== ""

            property var originalConfig
            property var editingConfig

            onAccepted: {
                linkSettingsLoader.item.saveSettings()
                editingConfig.name = nameField.text
                if (originalConfig) {
                    _linkManager.endConfigurationEditing(originalConfig, editingConfig)
                } else {
                    editingConfig.dynamic = false
                    _linkManager.endCreateConfiguration(editingConfig)
                }
            }

            onRejected: _linkManager.cancelConfigurationEditing(editingConfig)

            ColumnLayout {
                spacing: ScreenTools.defaultFontPixelHeight / 2

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCLabel { text: qsTr("Name") }
                    QGCTextField {
                        id:                 nameField
                        Layout.fillWidth:   true
                        text:               editingConfig.name
                        placeholderText:    qsTr("Enter name")
                    }
                }

                LabelledComboBox {
                    id:         vehicleModelCombo
                    label:      qsTr("Vehicle Model")
                    model:      [ qsTr("None") ].concat(VehicleModelManager.modelNames)
                    // Index 0 is "None"; a model deleted since the link was saved also falls back to it.
                    Component.onCompleted: currentIndex = VehicleModelManager.modelNames.indexOf(editingConfig.vehicleModel) + 1
                    onActivated: (index) => editingConfig.vehicleModel = index > 0 ? VehicleModelManager.modelNames[index - 1] : ""
                }

                QGCCheckBoxSlider {
                    Layout.fillWidth:   true
                    text:               qsTr("Automatically Connect on Start")
                    checked:            editingConfig.autoConnect
                    onCheckedChanged:   editingConfig.autoConnect = checked
                }

                // Stock option for very slow links: it stops joystick control altogether, which is never
                // wanted here, so it is not offered.
                QGCCheckBoxSlider {
                    Layout.fillWidth:   true
                    text:               qsTr("High Latency")
                    checked:            editingConfig.highLatency
                    onCheckedChanged:   editingConfig.highLatency = checked
                    visible:            false
                }

                SettingsGroupLayout {
                    Layout.fillWidth:   true
                    heading:            qsTr("WireGuard Tunnel")
                    visible:            WireGuardTunnel.available

                    QGCLabel {
                        id:                 tunnelStatusLabel
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap

                        property string message: ""

                        text: message !== "" ? message
                                             : (editingConfig.wireGuardTunnel === "" ? qsTr("No tunnel. The link connects over whatever network is up.")
                                                                                      : qsTr("Tunnel %1 starts when this link connects.").arg(editingConfig.wireGuardTunnel))
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCButton {
                            Layout.fillWidth:   true
                            text:               qsTr("Import .conf...")
                            enabled:            nameField.text !== ""
                            onClicked:          tunnelFileDialog.openForLoad()
                        }

                        QGCButton {
                            Layout.fillWidth:   true
                            text:               qsTr("Remove Tunnel")
                            enabled:            editingConfig.wireGuardTunnel !== ""
                            onClicked: {
                                tunnelStatusLabel.message = WireGuardTunnel.remove(editingConfig.wireGuardTunnel)
                                if (tunnelStatusLabel.message === "") {
                                    editingConfig.wireGuardTunnel = ""
                                }
                            }
                        }
                    }

                    QGCFileDialog {
                        id:             tunnelFileDialog
                        title:          qsTr("WireGuard Configuration")
                        folder:         QGroundControl.settingsManager.appSettings.settingsSavePath
                        nameFilters:    [ qsTr("WireGuard Configuration (*.conf)"), qsTr("All Files (*)") ]

                        onAcceptedForLoad: (file) => {
                            close()
                            // A link keeps its tunnel name once it has one, so re-importing replaces the tunnel.
                            let tunnel = editingConfig.wireGuardTunnel !== "" ? editingConfig.wireGuardTunnel
                                                                              : WireGuardTunnel.tunnelNameForLink(nameField.text)
                            tunnelStatusLabel.message = WireGuardTunnel.install(tunnel, file)
                            if (tunnelStatusLabel.message === "") {
                                editingConfig.wireGuardTunnel = tunnel
                            }
                        }
                    }
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
                            text:               editingConfig.pingAddress
                            placeholderText:    qsTr("Vehicle router, e.g. 10.30.1.101")
                            onTextEdited:       editingConfig.pingAddress = text.trim()
                        }
                    }

                    QGCCheckBoxSlider {
                        Layout.fillWidth:   true
                        text:               qsTr("Limit throttle on high ping")
                        checked:            editingConfig.throttleLimitEnabled
                        onCheckedChanged:   editingConfig.throttleLimitEnabled = checked
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth
                        enabled:            editingConfig.throttleLimitEnabled

                        QGCLabel { text: qsTr("Above") }
                        QGCTextField {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 8
                            text:                   editingConfig.pingThreshold1Ms
                            numericValuesOnly:      true
                            validator:              IntValidator { bottom: 1; top: 10000 }
                            onTextEdited:           editingConfig.pingThreshold1Ms = parseInt(text) || 0
                        }
                        QGCLabel { text: qsTr("ms throttle") }
                        QGCTextField {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 6
                            text:                   editingConfig.throttlePercent1
                            numericValuesOnly:      true
                            validator:              IntValidator { bottom: 0; top: 100 }
                            onTextEdited:           editingConfig.throttlePercent1 = parseInt(text) || 0
                        }
                        QGCLabel { text: qsTr("%") }
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth
                        enabled:            editingConfig.throttleLimitEnabled

                        QGCLabel { text: qsTr("Above") }
                        QGCTextField {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 8
                            text:                   editingConfig.pingThreshold2Ms
                            numericValuesOnly:      true
                            validator:              IntValidator { bottom: 1; top: 10000 }
                            onTextEdited:           editingConfig.pingThreshold2Ms = parseInt(text) || 0
                        }
                        QGCLabel { text: qsTr("ms throttle") }
                        QGCTextField {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 6
                            text:                   editingConfig.throttlePercent2
                            numericValuesOnly:      true
                            validator:              IntValidator { bottom: 0; top: 100 }
                            onTextEdited:           editingConfig.throttlePercent2 = parseInt(text) || 0
                        }
                        QGCLabel { text: qsTr("%") }
                    }
                }

                LabelledComboBox {
                    label:                  qsTr("Type")
                    enabled:                originalConfig == null
                    model:                  _linkManager.linkTypeStrings
                    Component.onCompleted:  comboBox.currentIndex = editingConfig.linkType

                    onActivated: (index) => {
                        if (index !== editingConfig.linkType) {
                            var name = nameField.text
                            editingConfig = _linkManager.createConfiguration(index, name)
                        }
                    }
                }

                Loader {
                    id:     linkSettingsLoader
                    source: editingConfig && editingConfig.settingsURL ? editingConfig.settingsURL : ""
                    asynchronous: true

                    property var subEditConfig:         editingConfig
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
    }
}
