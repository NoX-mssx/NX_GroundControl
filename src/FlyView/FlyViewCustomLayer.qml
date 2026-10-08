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
    readonly property real _railButtonSize:     ScreenTools.defaultFontPixelHeight * 2.8
    // NX glass surfaces over the video
    readonly property color _glassColor:        Qt.rgba(14 / 255, 18 / 255, 22 / 255, 0.72)
    readonly property color _glassBorder:       Qt.rgba(1, 1, 1, 0.12)
    readonly property color _glassButton:       Qt.rgba(1, 1, 1, 0.10)
    readonly property color _accent:            "#4FD1C5"
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

                    QGCLabel {
                        id:                 modelButtonLabel
                        anchors.centerIn:   parent
                        text:               modelButton.modelData.name
                        color:              modelButton._on ? _root._accentText : qgcPal.text
                        font.weight:        Font.ExtraBold
                    }

                    MouseArea {
                        id:             modelButtonMouseArea
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      VehicleModelManager.toggleButton(modelButton.index)
                    }
                }
            }
        }
    }

    // Extra cameras, stacked upwards from the map/video PiP in the bottom-left corner
    Repeater {
        model: 2

        AuxiliaryCameraWindow {
            required property int index

            slot:           index
            cameraIndex:    _root._auxiliaryCameras[index] ?? -1
            cameraName:     cameraIndex >= 0 && _root._cameraStates[cameraIndex] ? _root._cameraStates[cameraIndex].name : ""
            defaultWidth:   _root._auxiliaryWidth
            defaultX:       0
            defaultY:       _root.height - _root.parentToolInsets.bottomEdgeLeftInset - ((index + 1) * (_root._auxiliaryHeight + _root._windowMargin))
        }
    }

    // Name of the camera on the main screen
    Rectangle {
        anchors.top:        parent.top
        anchors.left:       parent.left
        width:              mainCameraLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 2.4
        height:             mainCameraLabel.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.5
        radius:             height / 2
        color:              _root._glassColor
        visible:            mainCameraLabel.text !== ""

        QGCLabel {
            id:                 mainCameraLabel
            anchors.centerIn:   parent
            text:               VehicleModelManager.mainCameraName + (VehicleModelManager.secondaryStream ? " · SD" : " · HD")
            font.weight:        Font.DemiBold
        }
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

            // Records the main camera on this computer
            RailButton {
                iconSource: ""
                checked:    QGroundControl.videoManager.recording
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
            width:                      speedRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 6
            height:                     _root._railButtonSize + ScreenTools.defaultFontPixelWidth
            radius:                     height / 2
            color:                      _root._glassColor
            border.color:               _root._glassBorder

            Row {
                id:                 speedRow
                anchors.centerIn:   parent
                spacing:            ScreenTools.defaultFontPixelWidth * 0.6

                QGCLabel {
                    anchors.baseline:   speedUnits.baseline
                    text:               _root._activeVehicle ? _root._activeVehicle.groundSpeed.valueString : "--"
                    font.pointSize:     ScreenTools.largeFontPointSize * 1.4
                    font.weight:        Font.ExtraBold
                }
                QGCLabel {
                    id:                 speedUnits
                    anchors.bottom:     parent.bottom
                    text:               _root._activeVehicle ? _root._activeVehicle.groundSpeed.units : ""
                    color:              qgcPal.colorGrey
                }
            }
        }
    }

    // Bottom right: distance to home
    Rectangle {
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        width:          homeRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 3.6
        height:         _root._railButtonSize + ScreenTools.defaultFontPixelWidth
        radius:         height / 2
        color:          _root._glassColor
        border.color:   _root._glassBorder
        visible:        _root._activeVehicle !== null

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
        height:                 cameraCommandColumn.height + ScreenTools.defaultFontPixelHeight
        radius:                 ScreenTools.defaultFontPixelHeight
        color:                  Qt.rgba(16 / 255, 21 / 255, 26 / 255, 0.94)
        border.color:           _root._glassBorder
        visible:                false

        readonly property real _buttonWidth: ScreenTools.defaultFontPixelWidth * 18
        // Camera index whose Reboot was pressed once and is waiting for the confirming second press
        property int _rebootArmedIndex: -1

        Timer {
            id:             rebootDisarmTimer
            interval:       4000
            onTriggered:    cameraCommandPanel._rebootArmedIndex = -1
        }

        Column {
            id:                 cameraCommandColumn
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelHeight / 2

            Row {
                id:         cameraCommandRow
                spacing:    ScreenTools.defaultFontPixelWidth * 2

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

            QGCLabel {
                width:      cameraCommandRow.width
                wrapMode:   Text.WordWrap
                text:       CameraControl.busy ? qsTr("Sending...") : CameraControl.status
                visible:    text !== ""
            }
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
        }
    }
}
