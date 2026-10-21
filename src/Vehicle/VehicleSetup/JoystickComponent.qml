import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.VehicleSetup
import QGroundControl.FactControls

/// Joystick (the transmitter connected to this computer): state at the top, then the axes and the buttons
/// side by side. The calibration takes the place of the axes and buttons while it runs.
SetupPage {
    pageComponent: joystickManager.activeJoystick ? pageComponent : noJoysticksComponent

    Component {
        id: pageComponent

        ColumnLayout {
            id:         root
            width:      availableWidth
            spacing:    _margins

            property real   _margins:               ScreenTools.defaultFontPixelHeight
            property Fact   activeJoystickNameFact: QGroundControl.settingsManager.joystickManagerSettings.activeJoystickName
            property string activeJoystickName:     activeJoystickNameFact.value
            property var    activeVehicle:          QGroundControl.multiVehicleManager.activeVehicle
            property var    joystick:               joystickManager.activeJoystick
            property bool   calibrated:             joystick ? joystick.settings.calibrated.rawValue : false
            property var    controller:             calibration.controller
            property bool   calibrationOpen:        !calibrated
            property bool   _calibrationShown:      calibrationOpen || (controller && controller.calibrating)

            // Buttons currently held down, by index; the revision makes bindings notice changes
            property var    _pressed:               ({})
            property int    _pressedRevision:       0
            property int    _lastPressed:           -1
            property bool   _showAllButtons:        false

            // Actions that do nothing on a ground vehicle with NX cameras
            readonly property var _hiddenActions: [
                "VTOL: Fixed Wing", "VTOL: Multi-Rotor",
                "Continuous Zoom In", "Continuous Zoom Out", "Step Zoom In", "Step Zoom Out",
                "Continuous Focus In", "Continuous Focus Out", "Step Focus In", "Step Focus Out",
                "Next Video Stream", "Previous Video Stream", "Next Camera", "Previous Camera", "Trigger Camera",
                "Start Recording Video", "Stop Recording Video", "Toggle Recording Video",
                "Gimbal Down", "Gimbal Up", "Gimbal Left", "Gimbal Right", "Gimbal Center", "Gimbal Yaw Lock", "Gimbal Yaw Follow",
                "Gripper Grab", "Gripper Release", "Gripper Hold",
                "Landing gear deploy", "Landing gear retract",
                "Motor Interlock enable", "Motor Interlock disable"
            ]

            QGCPalette { id: qgcPal; colorGroupEnabled: true }

            function _isPressed(index) {
                void _pressedRevision
                return _pressed[index] === true
            }

            /// Model button actions are stored as "Model button N"; returns N, or 0 for any other action
            function _modelButtonNumber(action) {
                const m = /^Model button (\d+)$/.exec(action)
                return m ? parseInt(m[1]) : 0
            }

            function _actionTitle(action) {
                if (joystick && action === joystick.buttonActionNone) {
                    return qsTr("Not assigned")
                }
                const number = _modelButtonNumber(action)
                if (number > 0) {
                    const buttons = VehicleModelManager.activeModel.buttons || []
                    const name = (number <= buttons.length && buttons[number - 1].name) ? buttons[number - 1].name : ""
                    return name !== "" ? qsTr("Model button %1: %2").arg(number).arg(name) : qsTr("Model button %1").arg(number)
                }
                return qsTranslate("Joystick", action)
            }

            /// Actions offered for one button: the ones that make sense here plus whatever it already has
            function _actionChoices(current) {
                if (!joystick) {
                    return []
                }
                const modelButtonCount = (VehicleModelManager.activeModel.buttons || []).length
                const titles = joystick.assignableActionTitles
                const choices = []
                for (let i = 0; i < titles.length; i++) {
                    const action = titles[i]
                    if (action !== current) {
                        if (_hiddenActions.indexOf(action) !== -1) {
                            continue
                        }
                        const number = _modelButtonNumber(action)
                        if (number > modelButtonCount) {
                            continue
                        }
                    }
                    choices.push({ action: action, text: _actionTitle(action) })
                }
                return choices
            }

            function _buttonRows() {
                void _pressedRevision
                if (!joystick) {
                    return []
                }
                const actions = joystick.buttonActions
                const rows = []
                for (let i = 0; i < joystick.buttonCount; i++) {
                    if (_showAllButtons || actions[i] !== joystick.buttonActionNone || i === _lastPressed) {
                        rows.push(i)
                    }
                }
                return rows
            }

            function _percent(value) {
                if (!controller) {
                    return 0
                }
                const range = controller.channelValueMax - controller.channelValueMin
                if (range <= 0) {
                    return 0
                }
                return Math.max(-100, Math.min(100, Math.round(((value - controller.channelValueMin) / range) * 200 - 100)))
            }

            Connections {
                target: root.joystick

                function onRawButtonPressedChanged(index, pressed) {
                    root._pressed[index] = pressed
                    if (pressed) {
                        root._lastPressed = index
                    }
                    root._pressedRevision++
                }
            }

            // ---- State of the joystick ----
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     stateRow.implicitHeight + root._margins * 1.2
                color:              qgcPal.windowShade
                radius:             ScreenTools.defaultFontPixelHeight * 0.6
                border.color:       Qt.rgba(1, 1, 1, 0.10)

                RowLayout {
                    id:                     stateRow
                    anchors.left:           parent.left
                    anchors.right:          parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin:     root._margins * 0.8
                    anchors.rightMargin:    root._margins * 0.8
                    spacing:                ScreenTools.defaultFontPixelWidth * 2

                    QGCComboBox {
                        id:                     joystickCombo
                        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 30
                        visible:                root.activeJoystickName !== "" && QGroundControl.corePlugin.options.allowJoystickSelection

                        onActivated: (index) => { root.activeJoystickNameFact.rawValue = textAt(index) }

                        function _recalc() {
                            const modelNames = [...(joystickManager.availableJoystickNames || [])]
                            if (root.activeJoystickName && !modelNames.includes(root.activeJoystickName)) {
                                modelNames.push(root.activeJoystickName)
                            }
                            model = modelNames
                            currentIndex = Math.max(0, find(root.activeJoystickName))
                        }

                        Component.onCompleted: _recalc()
                        Connections { target: root; function onActiveJoystickNameChanged() { joystickCombo._recalc() } }
                        Connections { target: joystickManager; function onAvailableJoystickNamesChanged() { joystickCombo._recalc() } }
                    }

                    StatePill {
                        text:   qsTr("Connected")
                        ok:     root.joystick !== null
                    }
                    StatePill {
                        text:   root.calibrated ? qsTr("Calibrated") : qsTr("Not calibrated")
                        ok:     root.calibrated
                    }

                    Item { Layout.fillWidth: true }

                    QGCCheckBoxSlider {
                        text:       qsTr("Drive the vehicle with the joystick")
                        checked:    joystickManager.activeJoystickEnabledForActiveVehicle
                        enabled:    root.calibrated
                        onClicked:  joystickManager.activeJoystickEnabledForActiveVehicle = checked
                    }

                    QGCButton {
                        text:       qsTr("Calibrate")
                        visible:    !root._calibrationShown
                        enabled:    root.activeVehicle ? !root.activeVehicle.armed : true
                        onClicked:  root.calibrationOpen = true
                    }
                }
            }

            // ---- Calibration ----
            ConfigSection {
                Layout.fillWidth:   true
                visible:            root._calibrationShown
                heading:            qsTr("Calibration")
                description:        root.activeVehicle && root.activeVehicle.armed
                                        ? qsTr("Disarm the vehicle to calibrate the joystick.")
                                        : qsTr("Press Calibrate and follow the instructions: centre the sticks, then move each one to its ends.")

                RemoteControlCalibration {
                    id:                 calibration
                    Layout.fillWidth:   true
                    showMonitor:        false
                    enabled:            root.activeVehicle ? !root.activeVehicle.armed : true

                    controller: JoystickConfigController {
                        joystick:       joystickManager.activeJoystick
                        statusText:     calibration.statusText
                        cancelButton:   calibration.cancelButton
                        nextButton:     calibration.nextButton
                        joystickMode:   true
                    }

                    useDeadband: controller && controller.joystick && controller.joystick.settings.useDeadband.rawValue

                    Component.onCompleted: controller.start()

                    Connections {
                        target: calibration.controller

                        function onCalibrationCompleted() {
                            root.calibrationOpen = false
                            if (joystickManager.activeJoystickEnabledForActiveVehicle) {
                                return
                            }
                            QGroundControl.showMessageDialog(
                                        root,
                                        qsTr("Enable Joystick"),
                                        qsTr("%1 calibration is complete. Enable it now?").arg(root.joystick.name),
                                        Dialog.Yes | Dialog.No,
                                        function() { joystickManager.activeJoystickEnabledForActiveVehicle = true })
                        }
                    }
                }

                QGCButton {
                    text:       qsTr("Close")
                    visible:    root.calibrated && root.controller && !root.controller.calibrating
                    onClicked:  root.calibrationOpen = false
                }
            }

            // ---- Axes and buttons ----
            GridLayout {
                Layout.fillWidth:   true
                visible:            !root._calibrationShown
                columns:            root.width > ScreenTools.defaultFontPixelWidth * 120 ? 2 : 1
                columnSpacing:      root._margins
                rowSpacing:         root._margins

                ConfigSection {
                    Layout.fillWidth:   true
                    Layout.alignment:   Qt.AlignTop
                    heading:            qsTr("Axes")
                    description:        qsTr("Move the sticks: the bars show what the vehicle receives.")

                    Repeater {
                        model: root.controller ? [
                            { fn: 3, name: qsTr("Throttle: forward / back"),   mapped: root.controller.throttleChannelMapped,  value: root.controller.adjustedThrottleChannelValue,  reversed: root.controller.throttleChannelReversed },
                            { fn: 0, name: qsTr("Steering: left / right (Roll)"), mapped: root.controller.rollChannelMapped,   value: root.controller.adjustedRollChannelValue,      reversed: root.controller.rollChannelReversed },
                            { fn: 2, name: qsTr("Yaw"),                         mapped: root.controller.yawChannelMapped,       value: root.controller.adjustedYawChannelValue,       reversed: root.controller.yawChannelReversed },
                            { fn: 1, name: qsTr("Pitch"),                       mapped: root.controller.pitchChannelMapped,     value: root.controller.adjustedPitchChannelValue,     reversed: root.controller.pitchChannelReversed }
                        ] : []

                        ColumnLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelHeight * 0.4
                            visible:            modelData.mapped

                            required property var modelData

                            readonly property int _percent: root._percent(modelData.value)

                            RowLayout {
                                Layout.fillWidth: true

                                QGCLabel {
                                    Layout.fillWidth:   true
                                    text:               modelData.name
                                    font.weight:        Font.DemiBold
                                }
                                QGCCheckBoxSlider {
                                    text:       qsTr("Reverse")
                                    checked:    !!modelData.reversed
                                    onClicked:  root.controller.setFunctionReversed(modelData.fn, checked)
                                }
                            }

                            RowLayout {
                                Layout.fillWidth:   true
                                spacing:            ScreenTools.defaultFontPixelWidth

                                // Bar from the centre towards the stick position
                                Rectangle {
                                    id:                 track
                                    Layout.fillWidth:   true
                                    implicitHeight:     ScreenTools.defaultFontPixelHeight * 0.7
                                    radius:             height / 2
                                    color:              qgcPal.window
                                    border.color:       Qt.rgba(1, 1, 1, 0.12)

                                    Rectangle {
                                        x:      parent._percent >= 0 ? parent.width / 2 : parent.width / 2 * (1 + parent._percent / 100)
                                        width:  parent.width / 2 * Math.abs(parent._percent) / 100
                                        height: parent.height
                                        radius: height / 2
                                        color:  qgcPal.buttonHighlight
                                    }
                                    Rectangle {
                                        x:      parent.width / 2 - width / 2
                                        y:      -height * 0.2
                                        width:  2
                                        height: parent.height * 1.4
                                        color:  qgcPal.colorGrey
                                    }

                                    readonly property int _percent: parent.parent._percent
                                }

                                QGCLabel {
                                    Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 6
                                    horizontalAlignment:    Text.AlignRight
                                    text:                   (parent.parent._percent > 0 ? "+" : "") + parent.parent._percent + "%"
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth:   true
                        implicitHeight:     1
                        color:              Qt.rgba(1, 1, 1, 0.08)
                    }

                    QGCLabel { text: qsTr("Throttle on the stick") }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        readonly property var _centerZero:      root.joystick ? root.joystick.settings.throttleModeCenterZero : null
                        readonly property var _negativeThrust:  root.joystick ? root.joystick.settings.negativeThrust : null

                        QGCButton {
                            Layout.fillWidth:   true
                            text:               qsTr("Centre is stop: forward and back")
                            primary:            parent._centerZero && parent._centerZero.rawValue
                            onClicked: {
                                parent._centerZero.rawValue = true
                                parent._negativeThrust.rawValue = true
                            }
                        }
                        QGCButton {
                            Layout.fillWidth:   true
                            text:               qsTr("Bottom is stop: forward only")
                            primary:            parent._centerZero && !parent._centerZero.rawValue
                            onClicked:          parent._centerZero.rawValue = false
                        }
                    }

                    FactCheckBoxSlider {
                        Layout.fillWidth:   true
                        text:               qsTr("Smooth throttle")
                        fact:               root.joystick ? root.joystick.settings.throttleSmoothing : null
                        visible:            fact !== null
                    }
                }

                ConfigSection {
                    Layout.fillWidth:   true
                    Layout.alignment:   Qt.AlignTop
                    heading:            qsTr("Buttons and switches")
                    description:        qsTr("Press a button on the transmitter: its row lights up.")

                    Repeater {
                        model: root._buttonRows()

                        RowLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelWidth * 1.5

                            required property int modelData

                            readonly property bool _down: root._isPressed(modelData)
                            readonly property var  _choices: root._actionChoices(root.joystick ? root.joystick.buttonActions[modelData] : "")

                            Rectangle {
                                implicitWidth:  ScreenTools.defaultFontPixelHeight * 0.7
                                implicitHeight: implicitWidth
                                radius:         width / 2
                                color:          parent._down ? qgcPal.buttonHighlight : Qt.rgba(1, 1, 1, 0.15)
                            }

                            QGCLabel {
                                Layout.fillWidth:   true
                                text:               qsTr("Button %1").arg(modelData + 1)
                                font.weight:        parent._down ? Font.DemiBold : Font.Normal
                            }

                            QGCComboBox {
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 30
                                model:                  parent._choices
                                textRole:               "text"
                                currentIndex:           {
                                    const current = root.joystick ? root.joystick.buttonActions[modelData] : ""
                                    return parent._choices.findIndex(c => c.action === current)
                                }
                                onActivated: (index) => root.joystick.setButtonAction(modelData, parent._choices[index].action)
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        visible:            root._buttonRows().length === 0
                        text:               qsTr("No buttons are assigned yet. Press a button on the transmitter to assign it.")
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorGrey
                    }

                    QGCButton {
                        text:       root._showAllButtons ? qsTr("Show assigned only") : qsTr("Show all %1 buttons").arg(root.joystick ? root.joystick.buttonCount : 0)
                        onClicked:  root._showAllButtons = !root._showAllButtons
                    }
                }
            }

            // ---- Everything else the joystick can do, for fine tuning ----
            ConfigSection {
                Layout.fillWidth:   true
                visible:            !root._calibrationShown
                heading:            qsTr("More settings")

                QGCCheckBoxSlider {
                    id:         moreToggle
                    text:       qsTr("Show")
                    checked:    false
                }

                JoystickComponentSettings {
                    Layout.fillWidth:   true
                    visible:            moreToggle.checked
                    joystick:           root.joystick
                }
            }
        }
    }

    /// Green or orange tag with a short state text
    component StatePill: Rectangle {
        property string text
        property bool   ok: true

        implicitWidth:  pillLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 2.4
        implicitHeight: pillLabel.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.4
        radius:         height / 2
        color:          ok ? Qt.rgba(0.24, 0.86, 0.52, 0.18) : Qt.rgba(1, 0.62, 0.26, 0.20)

        QGCLabel {
            id:                 pillLabel
            anchors.centerIn:   parent
            text:               parent.text
            color:              parent.ok ? "#9BEFC0" : "#FFB27A"
        }
    }

    Component {
        id: noJoysticksComponent
        QGCLabel {
            width: availableWidth
            height: availableHeight
            text: qsTr("No joysticks or gamepads detected. Connect the transmitter to this computer by USB.")
            wrapMode: Text.WordWrap
        }
    }
}
