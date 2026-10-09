import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Editor for vehicle models: the cameras on board a vehicle and the buttons driving its servo/relay outputs.
/// Edits are made on a copy (_edit) and only reach VehicleModelManager through Save.
Rectangle {
    id:             root
    objectName:     "settingsPage_VehicleModels"
    color:          "transparent"   // sits in the settings page card
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
        return { channel: 1, kind: "pwm", value: 2000, offValue: 1000 }
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


    /// Model in the list on the left: name and what it holds; the selected one is tinted.
    component ListCard: Rectangle {
        id:                 listCard
        width:              parent ? parent.width : 0
        height:             listCardColumn.implicitHeight + ScreenTools.defaultFontPixelHeight * 1.2
        radius:             ScreenTools.defaultFontPixelHeight
        color:              selected ? Qt.rgba(79 / 255, 209 / 255, 197 / 255, 0.08) : (listCardMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.04))
        border.color:       selected ? qgcPal.buttonHighlight : Qt.rgba(1, 1, 1, 0.10)

        property string title
        property string subtitle
        property bool   selected: false

        signal clicked()

        Column {
            id:                     listCardColumn
            anchors.left:           parent.left
            anchors.right:          parent.right
            anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.6
            anchors.rightMargin:    anchors.leftMargin
            anchors.verticalCenter: parent.verticalCenter

            QGCLabel {
                width:          parent.width
                text:           listCard.title
                font.weight:    Font.ExtraBold
                font.pointSize: ScreenTools.mediumFontPointSize
                elide:          Text.ElideRight
            }
            QGCLabel {
                width:          parent.width
                text:           listCard.subtitle
                color:          qgcPal.colorGrey
                font.pointSize: ScreenTools.smallFontPointSize
                elide:          Text.ElideRight
                visible:        text !== ""
            }
        }

        MouseArea {
            id:             listCardMouse
            anchors.fill:   parent
            hoverEnabled:   true
            onClicked:      listCard.clicked()
        }
    }

    // ---- Left: the stored models ----
    ColumnLayout {
        id:                     listColumn
        anchors.left:           parent.left
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        anchors.margins:        _margins
        width:                  ScreenTools.defaultFontPixelWidth * 34
        spacing:                _margins * 0.6

        RowLayout {
            Layout.fillWidth:   true

            QGCLabel {
                Layout.fillWidth:   true
                text:               qsTr("Models")
                font.weight:        Font.ExtraBold
                font.pointSize:     ScreenTools.largeFontPointSize
            }
            QGCButton {
                text:       qsTr("+ New")
                primary:    true
                onClicked:  root._selectModel("")
            }
        }

        QGCFlickable {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            contentHeight:      modelListColumn.height
            clip:               true

            Column {
                id:         modelListColumn
                width:      parent.width
                spacing:    _margins * 0.5

                Repeater {
                    model: VehicleModelManager.modelNames

                    ListCard {
                        required property string modelData

                        readonly property var _stored: VehicleModelManager.model(modelData)

                        title:      modelData
                        subtitle:   qsTr("%1 cameras · %2 buttons").arg((_stored.cameras || []).length).arg((_stored.buttons || []).length)
                        selected:   modelData === root._originalName
                        onClicked:  root._selectModel(modelData)
                    }
                }

                QGCLabel {
                    width:      parent.width
                    wrapMode:   Text.WordWrap
                    text:       qsTr("No models yet. Create one or import a JSON file.")
                    color:      qgcPal.colorGrey
                    visible:    VehicleModelManager.modelNames.length === 0
                }
            }
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Import from JSON...")
            onClicked:          fileDialog.openForLoad()
        }
    }

    // ---- Right: the model being edited ----
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
            contentHeight:      editorColumn.height
            clip:               true

            ColumnLayout {
                id:         editorColumn
                width:      editorFlickable.width
                spacing:    _margins

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    ColumnLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelHeight * 0.3

                        QGCLabel {
                            text:           qsTr("Model name")
                            color:          qgcPal.colorGrey
                            font.weight:    Font.DemiBold
                        }
                        QGCTextField {
                            Layout.fillWidth:   true
                            text:               root._edit.name
                            placeholderText:    qsTr("Model name")
                            font.weight:        Font.ExtraBold
                            onTextEdited:       root._edit.name = text
                        }
                    }

                    QGCButton {
                        Layout.alignment:   Qt.AlignBottom
                        text:               qsTr("Export JSON...")
                        enabled:            root._originalName !== ""
                        onClicked:          fileDialog.openForSave()
                    }
                }

                // Cameras
                RowLayout {
                    Layout.fillWidth:   true

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               qsTr("Cameras")
                        font.weight:        Font.ExtraBold
                        font.pointSize:     ScreenTools.mediumFontPointSize
                    }
                    QGCButton {
                        text: qsTr("+ Camera")
                        onClicked: {
                            root._edit.cameras.push(root._blankCamera())
                            root._refresh()
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth:   true
                    columns:            editorFlickable.width > ScreenTools.defaultFontPixelWidth * 110 ? 2 : 1
                    columnSpacing:      _margins
                    rowSpacing:         _margins

                    Repeater {
                        model: root._edit.cameras

                            SettingsGroupLayout {
                                id:                     cameraGroup
                                Layout.fillWidth:       true
                                Layout.alignment:       Qt.AlignTop
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
                }

                // Buttons
                RowLayout {
                    Layout.fillWidth:   true
                    Layout.topMargin:   _margins * 0.5

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               qsTr("Buttons on screen")
                        font.weight:        Font.ExtraBold
                        font.pointSize:     ScreenTools.mediumFontPointSize
                    }
                    QGCButton {
                        text: qsTr("+ Button")
                        onClicked: {
                            root._edit.buttons.push({ name: "", functions: [ root._blankFunction() ] })
                            root._refresh()
                        }
                    }
                }

                Repeater {
                    model: root._edit.buttons

                    SettingsGroupLayout {
                        id:                     buttonGroup
                        Layout.fillWidth:       true
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

                        QGCLabel { text: qsTr("Functions (output, type, value when on, value when off):") }

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
                                        functionRow.fn.value = kindIndex === 1 ? 1 : 2000
                                        functionRow.fn.offValue = kindIndex === 1 ? 0 : 1000
                                        root._refresh()
                                    }
                                }

                                // PWM: pulse width in microseconds. GPIO: 0 or 1. Sent when the button is switched on.
                                QGCTextField {
                                    Layout.fillWidth:   true
                                    text:               functionRow.modelData.value
                                    placeholderText:    qsTr("On")
                                    numericValuesOnly:  true
                                    validator:          IntValidator {
                                        bottom: functionRow.modelData.kind === "gpio" ? 0 : 500
                                        top:    functionRow.modelData.kind === "gpio" ? 1 : 2500
                                    }
                                    onTextEdited:       functionRow.fn.value = parseInt(text) || 0
                                }

                                // Sent when the button is switched off again.
                                QGCTextField {
                                    Layout.fillWidth:   true
                                    text:               functionRow.modelData.offValue !== undefined ? functionRow.modelData.offValue : ""
                                    placeholderText:    qsTr("Off")
                                    numericValuesOnly:  true
                                    validator:          IntValidator {
                                        bottom: functionRow.modelData.kind === "gpio" ? 0 : 500
                                        top:    functionRow.modelData.kind === "gpio" ? 1 : 2500
                                    }
                                    onTextEdited:       functionRow.fn.offValue = parseInt(text) || 0
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

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    color:              qgcPal.colorOrange
                    text:               root._errorText
                    visible:            root._errorText !== ""
                }

                RowLayout {
                    Layout.fillWidth:   true
                    Layout.topMargin:   _margins * 0.5

                    QGCButton {
                        text:               qsTr("Delete model")
                        enabled:            root._originalName !== ""
                        textColor:          qgcPal.warningText
                        onClicked:          root._delete()
                    }
                    Item { Layout.fillWidth: true }
                    QGCButton {
                        text:               qsTr("Save")
                        primary:            true
                        fontWeight:         Font.ExtraBold
                        onClicked:          root._save()
                    }
                }
            }
        }
    }
}
