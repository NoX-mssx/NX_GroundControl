import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Editor for vehicle models: the cameras on board a vehicle and the buttons driving its servo/relay outputs.
/// Edits are made on a copy (_edit) and only reach VehicleModelManager through Save.
Rectangle {
    id:             root
    objectName:     "settingsPage_VehicleModels"
    color:          qgcPal.window
    anchors.fill:   parent

    readonly property real _margins:    ScreenTools.defaultFontPixelHeight
    readonly property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 45
    readonly property string _newModelText: qsTr("New model...")

    // Name of the stored model being edited, empty while creating a new one
    property string _originalName:  ""
    property var    _edit:          _blankModel()
    property string _errorText:     ""

    function _blankModel() {
        return { name: "", cameras: [], buttons: [] }
    }

    function _blankCamera() {
        return { type: VehicleModelManager.cameraTypes[0], name: "", ip: "", user: "", password: "",
                 mainUrl: "", secondaryUrl: "", audio: false }
    }

    function _blankFunction() {
        return { channel: 1, kind: "pwm", value: 1500 }
    }

    // Repeaters only notice a new object, so structural edits (add/remove/reorder) reassign a copy.
    // Plain field edits write into _edit directly and need no refresh.
    function _refresh() {
        _edit = JSON.parse(JSON.stringify(_edit))
    }

    function _selectModel(name) {
        _errorText = ""
        let stored = name === "" ? null : VehicleModelManager.model(name)
        if (stored && stored.name !== undefined) {
            _originalName = name
            let copy = JSON.parse(JSON.stringify(stored))
            copy.cameras = copy.cameras || []
            copy.buttons = copy.buttons || []
            _edit = copy
        } else {
            _originalName = ""
            _edit = _blankModel()
        }
    }

    function _save() {
        _errorText = VehicleModelManager.saveModel(_originalName, _edit)
        if (_errorText === "") {
            _originalName = _edit.name.trim()
            _selectModel(_originalName)
        }
    }

    function _delete() {
        if (_originalName !== "") {
            VehicleModelManager.deleteModel(_originalName)
        }
        _selectModel("")
    }

    function _fillCameraUrls(camera) {
        let urls = VehicleModelManager.cameraUrls(camera.type, camera.ip, camera.user, camera.password)
        if (urls.mainUrl !== "") {
            camera.mainUrl = urls.mainUrl
            camera.secondaryUrl = urls.secondaryUrl
            _refresh()
        }
    }

    function _moveCamera(index, offset) {
        let target = index + offset
        if (target < 0 || target >= _edit.cameras.length) {
            return
        }
        let camera = _edit.cameras[index]
        _edit.cameras[index] = _edit.cameras[target]
        _edit.cameras[target] = camera
        _refresh()
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCFileDialog {
        id:             fileDialog
        title:          qsTr("Vehicle Model")
        folder:         QGroundControl.settingsManager.appSettings.settingsSavePath
        nameFilters:    [ qsTr("Vehicle Models (*.json)"), qsTr("All Files (*)") ]
        defaultSuffix:  "json"

        onAcceptedForLoad: (file) => {
            close()
            root._errorText = VehicleModelManager.importModels(file)
        }

        // Exports the stored model, so unsaved edits are not part of the file.
        onAcceptedForSave: (file) => {
            close()
            let error = VehicleModelManager.exportModel(root._originalName, file)
            root._errorText = error === "" ? qsTr("Exported to %1").arg(file) : error
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

            SettingsGroupLayout {
                Layout.preferredWidth:  _fieldWidth
                heading:                qsTr("Vehicle Model")

                QGCComboBox {
                    id:                 modelCombo
                    Layout.fillWidth:   true
                    model:              VehicleModelManager.modelNames.concat([ root._newModelText ])
                    currentIndex:       root._originalName === "" ? count - 1 : Math.max(0, VehicleModelManager.modelNames.indexOf(root._originalName))
                    onActivated: (index) => {
                        root._selectModel(index < VehicleModelManager.modelNames.length ? VehicleModelManager.modelNames[index] : "")
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
                        onClicked:          root._save()
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Import...")
                        onClicked:          fileDialog.openForLoad()
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Export...")
                        enabled:            root._originalName !== ""
                        onClicked:          fileDialog.openForSave()
                    }
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    color:              qgcPal.colorOrange
                    text:               root._errorText
                    visible:            root._errorText !== ""
                }
            }

            SettingsGroupLayout {
                Layout.preferredWidth:  _fieldWidth
                heading:                qsTr("Model Name")

                QGCTextField {
                    Layout.fillWidth:   true
                    text:               root._edit.name
                    placeholderText:    qsTr("Model name")
                    onTextEdited:       root._edit.name = text
                }
            }

            Repeater {
                model: root._edit.cameras

                SettingsGroupLayout {
                    id:                     cameraGroup
                    Layout.preferredWidth:  _fieldWidth
                    heading:                qsTr("Camera: %1").arg(modelData.name)

                    required property var modelData
                    required property int index

                    property var camera: root._edit.cameras[index]

                    QGCComboBox {
                        Layout.fillWidth:   true
                        model:              VehicleModelManager.cameraTypes
                        currentIndex:       Math.max(0, VehicleModelManager.cameraTypes.indexOf(cameraGroup.modelData.type))
                        onActivated: (typeIndex) => {
                            cameraGroup.camera.type = VehicleModelManager.cameraTypes[typeIndex]
                            root._fillCameraUrls(cameraGroup.camera)
                        }
                    }

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               cameraGroup.modelData.name
                        placeholderText:    qsTr("Camera Name")
                        onTextEdited:       cameraGroup.camera.name = text
                    }

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               cameraGroup.modelData.ip
                        placeholderText:    qsTr("IP address")
                        onTextEdited:       cameraGroup.camera.ip = text
                        onEditingFinished:  root._fillCameraUrls(cameraGroup.camera)
                    }

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               cameraGroup.modelData.user
                        placeholderText:    qsTr("User name")
                        onTextEdited:       cameraGroup.camera.user = text
                        onEditingFinished:  root._fillCameraUrls(cameraGroup.camera)
                    }

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               cameraGroup.modelData.password
                        placeholderText:    qsTr("Password")
                        echoMode:           TextInput.Password
                        onTextEdited:       cameraGroup.camera.password = text
                        onEditingFinished:  root._fillCameraUrls(cameraGroup.camera)
                    }

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               cameraGroup.modelData.mainUrl
                        placeholderText:    qsTr("RTSP URL")
                        onTextEdited:       cameraGroup.camera.mainUrl = text
                    }

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               cameraGroup.modelData.secondaryUrl
                        placeholderText:    qsTr("Secondary RTSP URL")
                        onTextEdited:       cameraGroup.camera.secondaryUrl = text
                    }

                    QGCCheckBox {
                        text:       qsTr("Audio Enabled")
                        checked:    cameraGroup.modelData.audio === true
                        onClicked:  cameraGroup.camera.audio = checked
                    }

                    RowLayout {
                        spacing: ScreenTools.defaultFontPixelWidth

                        QGCButton {
                            text:       "↑"
                            visible:    cameraGroup.index > 0
                            onClicked:  root._moveCamera(cameraGroup.index, -1)
                        }

                        QGCButton {
                            text:       "↓"
                            visible:    cameraGroup.index < root._edit.cameras.length - 1
                            onClicked:  root._moveCamera(cameraGroup.index, 1)
                        }

                        QGCButton {
                            text: qsTr("Delete Camera")
                            onClicked: {
                                root._edit.cameras.splice(cameraGroup.index, 1)
                                root._refresh()
                            }
                        }
                    }
                }
            }

            QGCButton {
                Layout.alignment:   Qt.AlignRight
                text:               qsTr("Add Camera")
                onClicked: {
                    root._edit.cameras.push(root._blankCamera())
                    root._refresh()
                }
            }

            Repeater {
                model: root._edit.buttons

                SettingsGroupLayout {
                    id:                     buttonGroup
                    Layout.preferredWidth:  _fieldWidth
                    heading:                qsTr("Button: %1").arg(modelData.name)

                    required property var modelData
                    required property int index

                    property var button: root._edit.buttons[index]

                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               buttonGroup.modelData.name
                        placeholderText:    qsTr("Button name")
                        onTextEdited:       buttonGroup.button.name = text
                    }

                    QGCLabel { text: qsTr("Functions:") }

                    Repeater {
                        model: buttonGroup.modelData.functions

                        RowLayout {
                            id:                 functionRow
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelWidth

                            required property var modelData
                            required property int index

                            property var fn: buttonGroup.button.functions[index]

                            // Servo output number for PWM, relay number for GPIO
                            QGCTextField {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 10
                                text:                   functionRow.modelData.channel
                                numericValuesOnly:      true
                                validator:              IntValidator { bottom: 0; top: 32 }
                                onTextEdited:           functionRow.fn.channel = parseInt(text) || 0
                            }

                            QGCComboBox {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 12
                                model:                  [ qsTr("PWM"), qsTr("GPIO") ]
                                currentIndex:           functionRow.modelData.kind === "gpio" ? 1 : 0
                                onActivated: (kindIndex) => {
                                    functionRow.fn.kind = kindIndex === 1 ? "gpio" : "pwm"
                                    functionRow.fn.value = kindIndex === 1 ? 0 : 1500
                                    root._refresh()
                                }
                            }

                            // PWM: pulse width in microseconds. GPIO: 0 = off, 1 = on.
                            QGCTextField {
                                Layout.fillWidth:   true
                                text:               functionRow.modelData.value
                                numericValuesOnly:  true
                                validator:          IntValidator {
                                    bottom: functionRow.modelData.kind === "gpio" ? 0 : 500
                                    top:    functionRow.modelData.kind === "gpio" ? 1 : 2500
                                }
                                onTextEdited:       functionRow.fn.value = parseInt(text) || 0
                            }

                            QGCButton {
                                text: qsTr("Remove")
                                onClicked: {
                                    buttonGroup.button.functions.splice(functionRow.index, 1)
                                    root._refresh()
                                }
                            }
                        }
                    }

                    RowLayout {
                        spacing: ScreenTools.defaultFontPixelWidth

                        QGCButton {
                            text: qsTr("Delete Button")
                            onClicked: {
                                root._edit.buttons.splice(buttonGroup.index, 1)
                                root._refresh()
                            }
                        }

                        QGCButton {
                            text: qsTr("Add Function")
                            onClicked: {
                                buttonGroup.button.functions.push(root._blankFunction())
                                root._refresh()
                            }
                        }
                    }
                }
            }

            QGCButton {
                Layout.alignment:   Qt.AlignRight
                text:               qsTr("Add Button")
                onClicked: {
                    root._edit.buttons.push({ name: "", functions: [ root._blankFunction() ] })
                    root._refresh()
                }
            }
        }
    }
}
