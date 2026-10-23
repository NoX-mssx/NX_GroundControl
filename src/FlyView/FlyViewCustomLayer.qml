import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap

// To implement a custom overlay copy this code to your own control in your custom code source. Then override the
// FlyViewCustomLayer.qml resource with your own qml. See the custom example and documentation for details.
Item {
    id: _root

    property var parentToolInsets               // These insets tell you what screen real estate is available for positioning the controls in your overlay
    property var totalToolInsets:   _toolInsets // These are the insets for your custom overlay additions
    property var mapControl

    // since this file is a placeholder for the custom layer in a standard build, we will just pass through the parent insets
    QGCToolInsets {
        id:                     _toolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    parentToolInsets.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    Math.max(parentToolInsets.bottomEdgeLeftInset, modelButtonRow.visible ? modelButtonRow.height + (modelButtonRow.anchors.margins * 2) : 0)
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
    }

    /// Height at the bottom-left corner taken by the model buttons; the map/video PiP sits above it.
    readonly property real bottomLeftReservedHeight: modelButtonRow.visible ? modelButtonRow.height + modelButtonRow.anchors.margins : 0

    readonly property var  _cameraStates:       VehicleModelManager.cameraStates
    readonly property var  _auxiliaryCameras:   VehicleModelManager.auxiliaryCameras
    readonly property bool _anyCameraHasAudio:  (VehicleModelManager.activeModel.cameras || []).some(function(camera) { return camera.audio === true })
    readonly property real _windowMargin:       ScreenTools.defaultFontPixelWidth
    readonly property var  _activeVehicle:      QGroundControl.multiVehicleManager.activeVehicle
    readonly property bool _throttleLimited:    LinkQualityMonitor.throttleLimitEnabled && LinkQualityMonitor.throttlePercent < 100
    readonly property var  _videoSettings:      QGroundControl.settingsManager.videoSettings
    readonly property real _railButtonSize:     ScreenTools.defaultFontPixelHeight * 2.8
    // NX glass surfaces over the video
    readonly property color _glassColor:        Qt.rgba(14 / 255, 18 / 255, 22 / 255, 0.72)
    readonly property color _glassBorder:       Qt.rgba(1, 1, 1, 0.12)
    readonly property color _glassButton:       Qt.rgba(1, 1, 1, 0.10)
    readonly property color _accent:            "#DB6536"
    readonly property color _accentText:        "#0B1F1D"
    readonly property real _auxiliaryWidth:     ScreenTools.defaultFontPixelWidth * 40
    readonly property real _auxiliaryHeight:    (ScreenTools.defaultFontPixelHeight * 1.6) + (_auxiliaryWidth * 9 / 16)

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    /// Round icon or text button of the right rail; highlighted while checked.
    component RailButton: Rectangle {
        id:             railButton
        width:          _root._railButtonSize
        height:         width
        radius:         width / 2
        color:          checked ? _root._accent : (railMouseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : _root._glassButton)

        property bool   checked:    false
        property string iconSource: ""
        property string label:      ""
        property color  iconColor:  checked ? _root._accentText : qgcPal.text

        signal clicked()

        QGCColoredImage {
            anchors.centerIn:   parent
            width:              parent.width * 0.48
            height:             width
            sourceSize.height:  height
            source:             railButton.iconSource
            color:              railButton.iconColor
            visible:            railButton.iconSource !== ""
        }

        QGCLabel {
            anchors.centerIn:   parent
            text:               railButton.label
            color:              railButton.iconColor
            font.weight:        Font.ExtraBold
            visible:            railButton.label !== ""
        }

        MouseArea {
            id:             railMouseArea
            anchors.fill:   parent
            hoverEnabled:   true
            onClicked:      railButton.clicked()
        }
    }

    // Buttons of the active vehicle model: switches, highlighted while on
    Rectangle {
        id:                 modelButtonRow
        anchors.margins:    ScreenTools.defaultFontPixelWidth
        anchors.left:       parent.left
        anchors.bottom:     parent.bottom
        width:              modelButtonLayout.implicitWidth + ScreenTools.defaultFontPixelWidth
        height:             _root._railButtonSize + ScreenTools.defaultFontPixelWidth
        radius:             height / 2
        color:              _root._glassColor
        border.color:       _root._glassBorder
        visible:            modelButtonRepeater.count > 0

        RowLayout {
            id:                 modelButtonLayout
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelWidth / 2

            Repeater {
                id:     modelButtonRepeater
                model:  VehicleModelManager.activeModel.buttons || []

                Rectangle {
                    id:                     modelButton
                    Layout.preferredHeight: _root._railButtonSize
                    Layout.preferredWidth:  Math.max(_root._railButtonSize * 1.6, modelButtonLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 3)
                    radius:                 height / 2
                    color:                  _on ? _root._accent : (modelButtonMouseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : _root._glassButton)

                    required property var modelData
                    required property int index

                    readonly property bool _on: VehicleModelManager.buttonStates[index] === true
                    readonly property bool _momentary:  modelData.mode === "momentary"
                    readonly property bool _steps:      modelData.mode === "steps"
                    // Number of positions of a "steps" button: the longest value list of its functions
                    readonly property int  _stepCount: {
                        let count = 0
                        for (const fn of (modelData.functions || [])) {
                            count = Math.max(count, (fn.steps || []).length)
                        }
                        return count
                    }

                    QGCLabel {
                        id:                 modelButtonLabel
                        anchors.centerIn:   parent
                        text:               modelButton._steps && modelButton._stepCount > 0
                                                ? modelButton.modelData.name + " " + ((VehicleModelManager.buttonSteps[index] || 0) + 1) + "/" + modelButton._stepCount
                                                : modelButton.modelData.name
                        color:              modelButton._on ? _root._accentText : qgcPal.text
                        font.weight:        Font.ExtraBold
                    }

                    MouseArea {
                        id:             modelButtonMouseArea
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      VehicleModelManager.toggleButton(modelButton.index)
                        onPressed:      if (modelButton._momentary) VehicleModelManager.pressButton(modelButton.index)
                        onReleased:     if (modelButton._momentary) VehicleModelManager.releaseButton(modelButton.index)
                        onCanceled:     if (modelButton._momentary) VehicleModelManager.releaseButton(modelButton.index)
                    }
                }
            }
        }
    }

    // Extra cameras, stacked upwards from the map/video PiP in the bottom-left corner.
    // Declared one by one, not with a Repeater: VideoManager finds the video items by objectName in
    // the QObject tree at startup, and Repeater delegates are not part of that tree.
    AuxiliaryCameraWindow {
        slot:           0
        cameraIndex:    _root._auxiliaryCameras[0] ?? -1
        cameraName:     cameraIndex >= 0 && _root._cameraStates[cameraIndex] ? _root._cameraStates[cameraIndex].name : ""
        defaultWidth:   _root._auxiliaryWidth
        defaultX:       0
        defaultY:       _root.height - _root.parentToolInsets.bottomEdgeLeftInset - (_root._auxiliaryHeight + _root._windowMargin)
    }

    AuxiliaryCameraWindow {
        slot:           1
        cameraIndex:    _root._auxiliaryCameras[1] ?? -1
        cameraName:     cameraIndex >= 0 && _root._cameraStates[cameraIndex] ? _root._cameraStates[cameraIndex].name : ""
        defaultWidth:   _root._auxiliaryWidth
        defaultX:       0
        defaultY:       _root.height - _root.parentToolInsets.bottomEdgeLeftInset - (2 * (_root._auxiliaryHeight + _root._windowMargin))
    }

    // Camera controls on the right edge
    Rectangle {
        id:                     cameraControls
        anchors.right:          parent.right
        anchors.verticalCenter: parent.verticalCenter
        width:                  railColumn.width + ScreenTools.defaultFontPixelWidth
        height:                 railColumn.height + ScreenTools.defaultFontPixelWidth
        radius:                 width / 2
        color:                  _root._glassColor
        border.color:           _root._glassBorder
        visible:                _root._cameraStates.length > 0

        Column {
            id:                 railColumn
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelWidth / 2

            RailButton {
                iconSource: QGroundControl.videoManager.audioMuted ? "/res/nx/SpeakerOff.svg" : "/res/nx/Speaker.svg"
                visible:    _root._anyCameraHasAudio
                onClicked:  QGroundControl.videoManager.audioMuted = !QGroundControl.videoManager.audioMuted
            }

            RailButton {
                label:      VehicleModelManager.secondaryStream ? qsTr("SD") : qsTr("HD")
                checked:    !VehicleModelManager.secondaryStream
                onClicked:  VehicleModelManager.secondaryStream = !VehicleModelManager.secondaryStream
            }

            RailButton {
                iconSource: "/res/nx/Gear.svg"
                checked:    cameraSelectionPanel.visible
                onClicked: {
                    cameraCommandPanel.visible = false
                    cameraSelectionPanel.visible = !cameraSelectionPanel.visible
                }
            }

            RailButton {
                iconSource: "/res/nx/Camera.svg"
                checked:    cameraCommandPanel.visible
                onClicked: {
                    cameraSelectionPanel.visible = false
                    cameraCommandPanel.visible = !cameraCommandPanel.visible
                }
            }
        }
    }

    // Above the camera controls: records the main camera on this computer. Switched on in the camera menu; while
    // a recording runs the button stays, so it can always be stopped.
    Rectangle {
        id:                     recordButton
        anchors.right:          parent.right
        anchors.bottom:         cameraControls.top
        anchors.bottomMargin:   ScreenTools.defaultFontPixelWidth
        width:                  cameraControls.width
        height:                 width
        radius:                 width / 2
        color:                  _root._glassColor
        border.color:           QGroundControl.videoManager.recording ? qgcPal.colorRed : _root._glassBorder
        visible:                cameraControls.visible && (_root._videoSettings.showRecControl.rawValue || QGroundControl.videoManager.recording)

        RailButton {
            anchors.centerIn:   parent
            checked:            QGroundControl.videoManager.recording
            onClicked: {
                if (QGroundControl.videoManager.recording) {
                    QGroundControl.videoManager.stopRecording()
                } else {
                    QGroundControl.videoManager.startRecording()
                }
            }

            Rectangle {
                anchors.centerIn:   parent
                width:              parent.width * (QGroundControl.videoManager.recording ? 0.32 : 0.38)
                height:             width
                radius:             QGroundControl.videoManager.recording ? width * 0.15 : width / 2
                color:              QGroundControl.videoManager.recording ? _root._accentText : qgcPal.colorRed
            }
        }
    }

    // Bottom centre: speed, with the throttle limit announced above it
    Column {
        anchors.horizontalCenter:   parent.horizontalCenter
        anchors.bottom:             parent.bottom
        spacing:                    ScreenTools.defaultFontPixelHeight / 3
        visible:                    _root._activeVehicle !== null

        Rectangle {
            anchors.horizontalCenter:   parent.horizontalCenter
            width:                      limitRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 3
            height:                     limitRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.6
            radius:                     height / 2
            color:                      Qt.rgba(1, 159 / 255, 67 / 255, 0.92)
            visible:                    _root._throttleLimited

            RowLayout {
                id:                 limitRow
                anchors.centerIn:   parent
                spacing:            ScreenTools.defaultFontPixelWidth * 0.8

                QGCColoredImage {
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight
                    Layout.preferredHeight: Layout.preferredWidth
                    sourceSize.height:      Layout.preferredHeight
                    source:                 "/res/nx/Warning.svg"
                    color:                  "#2B1500"
                }
                QGCLabel {
                    text:           LinkQualityMonitor.pingMs < 0 ? qsTr("Throttle stopped · no ping reply")
                                                                  : qsTr("Throttle limited to %1% · ping %2 ms").arg(LinkQualityMonitor.throttlePercent).arg(LinkQualityMonitor.pingMs)
                    color:          "#2B1500"
                    font.weight:    Font.ExtraBold
                }
            }
        }

        Rectangle {
            anchors.horizontalCenter:   parent.horizontalCenter
            visible:                    _root._videoSettings.showSpeedOverlay.rawValue
            // Fixed size: the window is as wide as three digits plus the unit, so a growing speed never resizes it
            width:                      speedRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 6
            height:                     _root._railButtonSize + ScreenTools.defaultFontPixelWidth
            radius:                     height / 2
            color:                      _root._glassColor
            border.color:               _root._glassBorder

            readonly property int _speed: _root._activeVehicle ? Math.min(999, Math.max(0, Math.round(_root._activeVehicle.groundSpeed.value))) : -1

            TextMetrics {
                id:     widestSpeed
                font:   speedValue.font
                text:   "000"
            }

            Row {
                id:                 speedRow
                anchors.centerIn:   parent
                spacing:            ScreenTools.defaultFontPixelWidth * 0.6

                QGCLabel {
                    id:                     speedValue
                    anchors.verticalCenter: parent.verticalCenter
                    width:                  Math.ceil(widestSpeed.advanceWidth)
                    horizontalAlignment:    Text.AlignHCenter
                    text:                   parent.parent._speed >= 0 ? parent.parent._speed : "--"
                    font.pointSize:         ScreenTools.largeFontPointSize * 1.4
                    font.weight:            Font.ExtraBold
                }
                QGCLabel {
                    id:                     speedUnits
                    anchors.verticalCenter: parent.verticalCenter
                    text:                   _root._activeVehicle ? _root._activeVehicle.groundSpeed.units : ""
                    color:                  qgcPal.colorGrey
                }
            }
        }
    }

    // The motor thresholds come from the active vehicle model
    Binding {
        target:     MotorTelemetryMonitor
        property:   "config"
        value:      VehicleModelManager.activeModel.motorTelemetry || ({})
    }

    // Bottom right, left of the distance to home: temperature and current of the left and right motor controllers
    Rectangle {
        id:                     motorTelemetryPill
        anchors.right:          homePill.visible ? homePill.left : parent.right
        anchors.rightMargin:    homePill.visible ? ScreenTools.defaultFontPixelWidth : 0
        anchors.bottom:         parent.bottom
        width:                  motorRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 3.6
        height:                 _root._railButtonSize + ScreenTools.defaultFontPixelWidth
        radius:                 height / 2
        color:                  _root._glassColor
        border.color:           MotorTelemetryMonitor.level >= 2 ? qgcPal.colorRed
                              : MotorTelemetryMonitor.level === 1 ? qgcPal.colorOrange : _root._glassBorder
        visible:                MotorTelemetryMonitor.active && _root._videoSettings.showMotorTelemetryOverlay.rawValue

        function levelColor(level) {
            return level >= 2 ? qgcPal.colorRed : (level === 1 ? qgcPal.colorOrange : qgcPal.text)
        }

        // Whole numbers only. Fixed width per value (three digits of temperature, two of current), so changing
        // readings never resize the window; only a current of 100 A or more widens it, rather than overlap.
        function wholeNumber(value) {
            return String(Math.round(value) || 0)
        }

        TextMetrics {
            id:     widestTemperature
            font:   motorRow.valueFont
            text:   "000°"
        }
        TextMetrics {
            id:     widestCurrent
            font:   motorRow.valueFont
            text:   qsTr("%1 A").arg("00")
        }

        Row {
            id:                 motorRow
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelWidth * 1.6

            property font valueFont: Qt.font({ family: ScreenTools.normalFontFamily, pointSize: ScreenTools.mediumFontPointSize, weight: Font.ExtraBold })

            Repeater {
                model: MotorTelemetryMonitor.motors

                Row {
                    required property var modelData
                    required property int index

                    spacing: ScreenTools.defaultFontPixelWidth * 0.6

                    // Motor pictogram with L or R inside; it takes the colour of the motor's worst reading
                    Item {
                        readonly property int  _level:  modelData.hasData ? Math.max(modelData.temperatureLevel, modelData.currentLevel) : 0
                        readonly property color _color: _level > 0 ? motorTelemetryPill.levelColor(_level) : qgcPal.colorGrey

                        anchors.verticalCenter: parent.verticalCenter
                        width:                  ScreenTools.defaultFontPixelHeight * 2
                        height:                 width

                        QGCColoredImage {
                            anchors.fill:       parent
                            sourceSize.height:  height
                            source:             "/res/nx/Motor.svg"
                            color:              parent._color
                        }
                        QGCLabel {
                            // Centre of the motor body in the 24-unit drawing: x 13, y 13
                            x:                  parent.width * 13 / 24 - width / 2
                            y:                  parent.height * 13 / 24 - height / 2
                            text:               modelData.side === "left" ? qsTr("L") : qsTr("R")
                            color:              parent._color
                            font.weight:        Font.ExtraBold
                            font.pointSize:     ScreenTools.smallFontPointSize
                        }
                    }
                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width:                  Math.max(Math.ceil(widestTemperature.advanceWidth), implicitWidth)
                        horizontalAlignment:    Text.AlignRight
                        text:                   modelData.hasData ? motorTelemetryPill.wholeNumber(modelData.temperature) + "°" : "--"
                        color:                  modelData.hasData ? motorTelemetryPill.levelColor(modelData.temperatureLevel) : qgcPal.colorGrey
                        font:                   motorRow.valueFont
                    }
                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width:                  Math.max(Math.ceil(widestCurrent.advanceWidth), implicitWidth)
                        horizontalAlignment:    Text.AlignRight
                        text:                   modelData.hasData ? qsTr("%1 A").arg(motorTelemetryPill.wholeNumber(modelData.current)) : "--"
                        color:                  modelData.hasData ? motorTelemetryPill.levelColor(modelData.currentLevel) : qgcPal.colorGrey
                        font:                   motorRow.valueFont
                    }
                }
            }
        }

        MouseArea {
            anchors.fill:   parent
            onClicked:      motorDetailsPanel.visible = !motorDetailsPanel.visible
        }
    }

    // All values of both controllers, opened by a click on the window
    Rectangle {
        id:                     motorDetailsPanel
        anchors.right:          motorTelemetryPill.right
        anchors.bottom:         radioPill.visible ? radioPill.top : motorTelemetryPill.top
        anchors.bottomMargin:   ScreenTools.defaultFontPixelHeight / 2
        width:                  motorDetailsGrid.implicitWidth + ScreenTools.defaultFontPixelWidth * 3
        height:                 motorDetailsGrid.implicitHeight + ScreenTools.defaultFontPixelHeight
        radius:                 ScreenTools.defaultFontPixelHeight / 2
        color:                  Qt.rgba(16 / 255, 21 / 255, 26 / 255, 0.94)
        border.color:           _root._glassBorder
        visible:                false

        readonly property var _motors: MotorTelemetryMonitor.motors
        // Room for the widest reading of every row, so the panel keeps its size as the values change
        readonly property real _valueWidth: Math.ceil(Math.max(widestDetailTemperature.advanceWidth, widestDetailCurrent.advanceWidth,
                                                               widestDetailVoltage.advanceWidth, widestDetailRpm.advanceWidth))

        function valueText(side, key, format) {
            const motor = _motors[side]
            if (!motor || !motor.hasData) {
                return "--"
            }
            return format.arg(motorTelemetryPill.wholeNumber(Number(motor[key])))
        }

        readonly property font _valueFont: Qt.font({ family: ScreenTools.normalFontFamily, pointSize: ScreenTools.defaultFontPointSize })

        TextMetrics { id: widestDetailTemperature;  font: motorDetailsPanel._valueFont; text: "000 °C" }
        TextMetrics { id: widestDetailCurrent;      font: motorDetailsPanel._valueFont; text: qsTr("%1 A").arg("00") }
        TextMetrics { id: widestDetailVoltage;      font: motorDetailsPanel._valueFont; text: qsTr("%1 V").arg("00") }
        TextMetrics { id: widestDetailRpm;          font: motorDetailsPanel._valueFont; text: "00000" }

        onVisibleChanged: if (visible && !motorTelemetryPill.visible) visible = false

        GridLayout {
            id:                 motorDetailsGrid
            anchors.centerIn:   parent
            columns:            3
            columnSpacing:      ScreenTools.defaultFontPixelWidth * 2
            rowSpacing:         ScreenTools.defaultFontPixelHeight * 0.2

            QGCLabel { text: qsTr("Motor controllers"); font.weight: Font.ExtraBold }
            QGCLabel {
                text:           qsTr("Left (ESC %1)").arg(motorDetailsPanel._motors.length > 0 ? motorDetailsPanel._motors[0].esc : "")
                font.weight:    Font.DemiBold
            }
            QGCLabel {
                text:           qsTr("Right (ESC %1)").arg(motorDetailsPanel._motors.length > 1 ? motorDetailsPanel._motors[1].esc : "")
                font.weight:    Font.DemiBold
            }

            QGCLabel { text: qsTr("Temperature") }
            QGCLabel { text: motorDetailsPanel.valueText(0, "temperature", "%1 °C"); Layout.minimumWidth: motorDetailsPanel._valueWidth }
            QGCLabel { text: motorDetailsPanel.valueText(1, "temperature", "%1 °C"); Layout.minimumWidth: motorDetailsPanel._valueWidth }

            QGCLabel { text: qsTr("Current") }
            QGCLabel { text: motorDetailsPanel.valueText(0, "current", qsTr("%1 A")) }
            QGCLabel { text: motorDetailsPanel.valueText(1, "current", qsTr("%1 A")) }

            QGCLabel { text: qsTr("Voltage") }
            QGCLabel { text: motorDetailsPanel.valueText(0, "voltage", qsTr("%1 V")) }
            QGCLabel { text: motorDetailsPanel.valueText(1, "voltage", qsTr("%1 V")) }

            QGCLabel { text: qsTr("RPM") }
            QGCLabel { text: motorDetailsPanel.valueText(0, "rpm", "%1") }
            QGCLabel { text: motorDetailsPanel.valueText(1, "rpm", "%1") }
        }

        MouseArea {
            anchors.fill:   parent
            onClicked:      motorDetailsPanel.visible = false
        }
    }

    // Bottom right, above the readouts: the radio of the room. The screen with the track is the volume control:
    // drag (or scroll) right for louder, left for quieter; the number is the volume, 0-100. Play/pause on the
    // right, or the space bar.
    Rectangle {
        id:                     radioPill
        anchors.right:          parent.right
        anchors.bottom:         (homePill.visible || motorTelemetryPill.visible) ? homePill.top : parent.bottom
        anchors.bottomMargin:   (homePill.visible || motorTelemetryPill.visible) ? ScreenTools.defaultFontPixelWidth : 0
        width:                  radioRow.implicitWidth + _gap * 2
        height:                 _root._railButtonSize + ScreenTools.defaultFontPixelWidth
        radius:                 height / 2
        color:                  _root._glassColor
        border.color:           _root._glassBorder
        visible:                RoomRadio.supported && RoomRadio.available && RoomRadio.enabled

        // Screen and button are as tall as a rail button and sit concentric with the rounded ends
        readonly property real _inner:  _root._railButtonSize * 0.85

        // Space plays or pauses while the drive screen is in front. A text field keeps its spaces, and an open
        // dialog blocks the shortcut; holding the key does not toggle again and again.
        Shortcut {
            sequence:       "Space"
            autoRepeat:     false
            enabled:        radioPill.visible && !mainWindow.toolDrawerVisible
            onActivated:    RoomRadio.toggle()
        }
        readonly property real _gap:    (height - _inner) / 2

        TextMetrics {
            id:     widestVolume
            font:   radioVolumeValue.font
            text:   "100"
        }

        RowLayout {
            id:                     radioRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left:           parent.left
            anchors.leftMargin:     radioPill._gap
            spacing:                radioPill._gap

            Slider {
                id:                     radioVolume
                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 32
                Layout.preferredHeight: radioPill._inner
                // The marker stays clear of the rounded ends
                leftPadding:            height / 4
                rightPadding:           height / 4
                topPadding:             0
                bottomPadding:          0
                from:                   0
                to:                     1
                // Dragging is smooth; the step is for the mouse wheel and the keys
                stepSize:               0.05
                wheelEnabled:           true
                value:                  RoomRadio.volume
                onMoved:                RoomRadio.volume = value

                readonly property real _inset:          height * 0.4
                readonly property real _markerWidth:    1.5
                readonly property real _markerX:        leftPadding + visualPosition * (availableWidth - _markerWidth)

                background: Rectangle {
                    implicitWidth:  radioVolume.Layout.preferredWidth
                    implicitHeight: radioVolume.Layout.preferredHeight
                    radius:         height / 2
                    color:          Qt.rgba(1, 1, 1, 0.06)
                    border.color:   Qt.rgba(1, 1, 1, 0.12)

                    // The level, filled up to the marker: a clipped copy of the screen, so its left end is rounded too
                    Item {
                        width:      radioVolume._markerX + radioVolume._markerWidth / 2
                        height:     parent.height
                        clip:       true
                        visible:    radioVolume.value > 0

                        Rectangle {
                            width:  radioVolume.width
                            height: parent.height
                            radius: height / 2
                            color:  Qt.rgba(_root._accent.r, _root._accent.g, _root._accent.b, radioVolume.pressed ? 0.38 : 0.26)
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left:           parent.left
                        anchors.leftMargin:     radioVolume._inset
                        anchors.right:          radioVolumeValue.left
                        anchors.rightMargin:    radioVolume._inset

                        QGCLabel {
                            width:          parent.width
                            text:           qsTr("Radio")
                            color:          qgcPal.colorGrey
                            font.pointSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            width:          parent.width
                            elide:          Text.ElideRight
                            font.weight:    Font.DemiBold
                            text: {
                                if (RoomRadio.playing && RoomRadio.status === "connecting") {
                                    return qsTr("Connecting…")
                                }
                                return RoomRadio.nowPlaying !== "" ? RoomRadio.nowPlaying : qsTr("No tracks on air")
                            }
                        }
                    }

                    QGCLabel {
                        id:                     radioVolumeValue
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right:          parent.right
                        // Clear of the marker at full volume
                        anchors.rightMargin:    radioVolume.rightPadding + radioVolume._markerWidth + radioVolume._inset / 2
                        width:                  Math.ceil(widestVolume.advanceWidth)
                        horizontalAlignment:    Text.AlignRight
                        text:                   Math.round(radioVolume.value * 100)
                        font.weight:            Font.ExtraBold
                        font.pointSize:         ScreenTools.mediumFontPointSize
                    }
                }

                // Thin vertical marker at the volume
                handle: Rectangle {
                    x:      radioVolume._markerX
                    y:      (radioVolume.availableHeight - height) / 2
                    width:  radioVolume._markerWidth
                    height: radioVolume.availableHeight * 0.72
                    color:  radioVolume.pressed ? qgcPal.text : _root._accent
                }
            }

            RailButton {
                Layout.preferredWidth:  _root._railButtonSize * 0.85
                Layout.preferredHeight: Layout.preferredWidth
                checked:                RoomRadio.playing
                iconSource:             RoomRadio.playing ? "/res/nx/Pause.svg" : "/res/nx/Play.svg"
                onClicked:              RoomRadio.toggle()
            }
        }
    }

    // Bottom right: distance to home
    Rectangle {
        id:             homePill
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        width:          homeRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 3.6
        height:         _root._railButtonSize + ScreenTools.defaultFontPixelWidth
        radius:         height / 2
        color:          _root._glassColor
        border.color:   _root._glassBorder
        visible:        _root._activeVehicle !== null && _root._videoSettings.showHomeDistanceOverlay.rawValue

        RowLayout {
            id:                 homeRow
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelWidth * 0.8

            QGCColoredImage {
                Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 1.1
                Layout.preferredHeight: Layout.preferredWidth
                sourceSize.height:      Layout.preferredHeight
                source:                 "/res/nx/Home.svg"
                color:                  qgcPal.text
            }
            QGCLabel {
                text:           _root._activeVehicle ? (_root._activeVehicle.distanceToHome.valueString + " " + _root._activeVehicle.distanceToHome.units) : ""
                font.weight:    Font.ExtraBold
                font.pointSize: ScreenTools.mediumFontPointSize
            }
        }
    }

    // Day/night, illuminator and reboot commands for every camera of the model
    Rectangle {
        id:                     cameraCommandPanel
        anchors.right:          cameraControls.left
        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth
        anchors.verticalCenter: cameraControls.verticalCenter
        width:                  cameraCommandColumn.width + (ScreenTools.defaultFontPixelWidth * 2)
        // The cameras are listed one below the other; the list scrolls when it is taller than the screen
        height:                 Math.min(cameraCommandColumn.height + ScreenTools.defaultFontPixelHeight, _root.height - (ScreenTools.defaultFontPixelHeight * 2))
        radius:                 ScreenTools.defaultFontPixelHeight
        color:                  Qt.rgba(16 / 255, 21 / 255, 26 / 255, 0.94)
        border.color:           _root._glassBorder
        visible:                false
        clip:                   true

        readonly property real _buttonWidth: ScreenTools.defaultFontPixelWidth * 18
        // Camera index whose Reboot was pressed once and is waiting for the confirming second press
        property int _rebootArmedIndex: -1

        Timer {
            id:             rebootDisarmTimer
            interval:       4000
            onTriggered:    cameraCommandPanel._rebootArmedIndex = -1
        }

        Flickable {
            anchors.fill:       parent
            contentWidth:       width
            contentHeight:      cameraCommandColumn.height + ScreenTools.defaultFontPixelHeight
            boundsBehavior:     Flickable.StopAtBounds
            interactive:        contentHeight > height

            ScrollBar.vertical: ScrollBar { }

            Column {
                id:                 cameraCommandColumn
                anchors.horizontalCenter:   parent.horizontalCenter
                y:                  ScreenTools.defaultFontPixelHeight / 2
                spacing:            ScreenTools.defaultFontPixelHeight / 2

                Column {
                    id:         cameraCommandRow
                    spacing:    ScreenTools.defaultFontPixelHeight * 1.2

                    Repeater {
                        model: VehicleModelManager.activeModel.cameras || []

                        Column {
                            id: cameraCommands

                            required property var modelData
                            required property int index

                            spacing: ScreenTools.defaultFontPixelHeight / 3

                            QGCLabel {
                                text:       cameraCommands.modelData.name !== "" ? cameraCommands.modelData.name : qsTr("Camera %1").arg(cameraCommands.index + 1)
                                font.bold:  true
                            }

                            Repeater {
                                model: [
                                    { command: "day",   label: qsTr("Day Mode") },
                                    { command: "night", label: qsTr("Night Mode") },
                                    { command: "irOn",  label: qsTr("IR Light On") },
                                    { command: "irOff", label: qsTr("IR Light Off") }
                                ]

                                QGCButton {
                                    required property var modelData

                                    width:      cameraCommandPanel._buttonWidth
                                    text:       modelData.label
                                    onClicked:  CameraControl.run(cameraCommands.modelData, modelData.command)
                                }
                            }

                            // A reboot drops the video for about a minute, so it takes two presses.
                            QGCButton {
                                readonly property bool armed: cameraCommandPanel._rebootArmedIndex === cameraCommands.index

                                width:  cameraCommandPanel._buttonWidth
                                text:   armed ? qsTr("Confirm Reboot") : qsTr("Reboot")
                                onClicked: {
                                    if (armed) {
                                        cameraCommandPanel._rebootArmedIndex = -1
                                        CameraControl.run(cameraCommands.modelData, "reboot")
                                    } else {
                                        cameraCommandPanel._rebootArmedIndex = cameraCommands.index
                                        rebootDisarmTimer.restart()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // The menu stays just buttons: a command that did not go through is reported under the "!" instead
        Connections {
            target: CameraControl
            function onCommandFailed(message) { mainWindow.addVehicleNotice(message, 1, false) }
        }
    }

    // Which cameras are shown. The main camera is always on; it changes by clicking another camera.
    Rectangle {
        id:                     cameraSelectionPanel
        anchors.right:          cameraControls.left
        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth
        anchors.verticalCenter: cameraControls.verticalCenter
        width:                  cameraSelectionColumn.width + (ScreenTools.defaultFontPixelWidth * 2)
        height:                 cameraSelectionColumn.height + ScreenTools.defaultFontPixelHeight
        radius:                 ScreenTools.defaultFontPixelHeight
        color:                  Qt.rgba(16 / 255, 21 / 255, 26 / 255, 0.94)
        border.color:           _root._glassBorder
        visible:                false

        Column {
            id:                 cameraSelectionColumn
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelHeight / 2

            Repeater {
                model: _root._cameraStates

                QGCCheckBox {
                    required property var modelData
                    required property int index

                    text:       modelData.name !== "" ? modelData.name : qsTr("Camera %1").arg(index + 1)
                    checked:    modelData.enabled
                    enabled:    !modelData.main
                    onClicked:  VehicleModelManager.setCameraEnabled(index, checked)
                }
            }

            QGCCheckBox {
                text:       qsTr("Speed")
                checked:    _root._videoSettings.showSpeedOverlay.rawValue
                onClicked:  _root._videoSettings.showSpeedOverlay.rawValue = checked
            }

            QGCCheckBox {
                text:       qsTr("Video recording")
                checked:    _root._videoSettings.showRecControl.rawValue
                onClicked:  _root._videoSettings.showRecControl.rawValue = checked
            }

            QGCCheckBox {
                text:       qsTr("Distance to home")
                checked:    _root._videoSettings.showHomeDistanceOverlay.rawValue
                onClicked:  _root._videoSettings.showHomeDistanceOverlay.rawValue = checked
            }

            QGCCheckBox {
                text:       qsTr("Motor telemetry")
                visible:    MotorTelemetryMonitor.config.enabled === true
                checked:    _root._videoSettings.showMotorTelemetryOverlay.rawValue
                onClicked:  _root._videoSettings.showMotorTelemetryOverlay.rawValue = checked
            }

            // Shown while the room broadcasts; off also stops the sound
            QGCCheckBox {
                text:       qsTr("Radio")
                visible:    RoomRadio.supported && RoomRadio.available
                checked:    RoomRadio.enabled
                onClicked:  RoomRadio.enabled = checked
            }
        }
    }
}
