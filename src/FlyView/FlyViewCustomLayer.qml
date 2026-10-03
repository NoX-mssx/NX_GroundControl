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
    readonly property real _auxiliaryWidth:     ScreenTools.defaultFontPixelWidth * 40
    readonly property real _auxiliaryHeight:    (ScreenTools.defaultFontPixelHeight * 1.6) + (_auxiliaryWidth * 9 / 16)

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // Buttons of the active vehicle model, each driving the servo/relay outputs configured for it
    Row {
        id:                 modelButtonRow
        anchors.margins:    ScreenTools.defaultFontPixelWidth
        anchors.left:       parent.left
        anchors.bottom:     parent.bottom
        spacing:            ScreenTools.defaultFontPixelWidth
        visible:            modelButtonRepeater.count > 0

        Repeater {
            id:     modelButtonRepeater
            model:  VehicleModelManager.activeModel.buttons || []

            QGCButton {
                required property var modelData

                text:       modelData.name
                onClicked:  VehicleModelManager.runButton(modelData)
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

    QGCLabel {
        anchors.top:        parent.top
        anchors.right:      parent.right
        text:               VehicleModelManager.mainCameraName
        font.pointSize:     ScreenTools.mediumFontPointSize
        visible:            text !== ""
    }

    // Camera controls on the right edge
    Column {
        id:                     cameraControls
        anchors.right:          parent.right
        // Stay clear of the stock photo/video control on the right edge
        anchors.rightMargin:    _root.parentToolInsets.rightEdgeCenterInset
        anchors.verticalCenter: parent.verticalCenter
        spacing:                ScreenTools.defaultFontPixelHeight / 2
        visible:                _root._cameraStates.length > 0

        QGCButton {
            width:      ScreenTools.defaultFontPixelWidth * 8
            text:       QGroundControl.videoManager.audioMuted ? qsTr("Muted") : qsTr("Sound")
            visible:    _root._anyCameraHasAudio
            onClicked:  QGroundControl.videoManager.audioMuted = !QGroundControl.videoManager.audioMuted
        }

        QGCButton {
            width:      ScreenTools.defaultFontPixelWidth * 8
            text:       VehicleModelManager.secondaryStream ? qsTr("SD") : qsTr("HD")
            onClicked:  VehicleModelManager.secondaryStream = !VehicleModelManager.secondaryStream
        }

        QGCButton {
            width:      ScreenTools.defaultFontPixelWidth * 8
            text:       qsTr("Control")
            checkable:  true
            checked:    cameraCommandPanel.visible
            onClicked: {
                cameraSelectionPanel.visible = false
                cameraCommandPanel.visible = !cameraCommandPanel.visible
            }
        }

        QGCButton {
            width:      ScreenTools.defaultFontPixelWidth * 8
            text:       qsTr("Cams")
            checkable:  true
            checked:    cameraSelectionPanel.visible
            onClicked: {
                cameraCommandPanel.visible = false
                cameraSelectionPanel.visible = !cameraSelectionPanel.visible
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
        radius:                 ScreenTools.defaultFontPixelHeight / 4
        color:                  qgcPal.window
        border.color:           qgcPal.groupBorder
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
        radius:                 ScreenTools.defaultFontPixelHeight / 4
        color:                  qgcPal.window
        border.color:           qgcPal.groupBorder
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
