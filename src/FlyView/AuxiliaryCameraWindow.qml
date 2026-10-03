import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// Small floating window showing one extra camera of the active vehicle model.
/// Clicking the picture moves the camera to the main video. While unlocked the window can be dragged
/// and resized; locked it can only be collapsed to its title bar.
Rectangle {
    id: root

    /// Auxiliary video slot (0-based); the video item is found by VideoManager as "auxVideo<slot + 1>"
    required property int slot
    /// Index into the active model's cameras, -1 when this slot is unused
    property int    cameraIndex:    -1
    property string cameraName:     ""
    property bool   locked:         true
    property bool   collapsed:      false
    /// Position and width used until the user moves or resizes the window
    property real   defaultX:       0
    property real   defaultY:       0
    property real   defaultWidth:   ScreenTools.defaultFontPixelWidth * 40

    readonly property real _aspectRatio:    16 / 9
    readonly property real _titleHeight:    ScreenTools.defaultFontPixelHeight * 1.6
    readonly property real _minimumWidth:   ScreenTools.defaultFontPixelWidth * 20
    property real _videoWidth:              defaultWidth
    property bool _moved:                   false

    x:          defaultX
    y:          defaultY
    width:      _videoWidth
    height:     _titleHeight + (collapsed ? 0 : _videoWidth / _aspectRatio)
    color:      "black"
    border.color: qgcPal.groupBorder
    border.width: 1
    // The video item must exist even while hidden so the video receiver can attach to it at startup.
    visible:    cameraIndex >= 0

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // The default position follows the layout only until the user places the window.
    onDefaultXChanged: if (!_moved) x = defaultX
    onDefaultYChanged: if (!_moved) y = defaultY

    Item {
        id:             videoArea
        anchors.top:    titleBar.bottom
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        visible:        !root.collapsed

        FlightDisplayViewVideoOutput {
            objectName:     "auxVideo" + (root.slot + 1)
            anchors.fill:   parent
        }

        MouseArea {
            anchors.fill:   parent
            onClicked:      VehicleModelManager.setMainCamera(root.cameraIndex)
        }

        // Resize handle
        Rectangle {
            anchors.right:  parent.right
            anchors.bottom: parent.bottom
            width:          ScreenTools.defaultFontPixelHeight
            height:         width
            color:          qgcPal.buttonHighlight
            visible:        !root.locked

            MouseArea {
                anchors.fill:   parent
                cursorShape:    Qt.SizeFDiagCursor
                preventStealing: true

                property real _pressX:      0
                property real _pressWidth:  0

                onPressed: (mouse) => {
                    _pressX = mapToItem(root.parent, mouse.x, mouse.y).x
                    _pressWidth = root._videoWidth
                }
                onPositionChanged: (mouse) => {
                    let currentX = mapToItem(root.parent, mouse.x, mouse.y).x
                    root._videoWidth = Math.max(root._minimumWidth, _pressWidth + (currentX - _pressX))
                }
            }
        }
    }

    Rectangle {
        id:             titleBar
        anchors.top:    parent.top
        anchors.left:   parent.left
        anchors.right:  parent.right
        height:         root._titleHeight
        color:          qgcPal.window
        opacity:        0.85

        // Dragging by the title bar, only while unlocked
        MouseArea {
            anchors.fill:   parent
            enabled:        !root.locked
            cursorShape:    root.locked ? Qt.ArrowCursor : Qt.SizeAllCursor
            drag.target:    root
            drag.axis:      Drag.XAndYAxis
            drag.minimumX:  0
            drag.minimumY:  0
            drag.maximumX:  root.parent ? Math.max(0, root.parent.width - root.width) : 0
            drag.maximumY:  root.parent ? Math.max(0, root.parent.height - root.height) : 0
            onPressed:      root._moved = true
        }

        Row {
            anchors.fill:       parent
            anchors.margins:    ScreenTools.defaultFontPixelHeight * 0.2
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCColoredImage {
                id:                 lockIcon
                anchors.verticalCenter: parent.verticalCenter
                height:             parent.height * 0.8
                width:              height
                sourceSize.height:  height
                source:             root.locked ? "/InstrumentValueIcons/lock-closed.svg" : "/InstrumentValueIcons/lock-open.svg"
                color:              root.locked ? qgcPal.text : qgcPal.colorOrange
                fillMode:           Image.PreserveAspectFit

                MouseArea {
                    anchors.fill:       parent
                    anchors.margins:    -ScreenTools.defaultFontPixelHeight * 0.3
                    onClicked:          root.locked = !root.locked
                }
            }

            QGCColoredImage {
                anchors.verticalCenter: parent.verticalCenter
                height:             parent.height * 0.8
                width:              height
                sourceSize.height:  height
                source:             root.collapsed ? "/InstrumentValueIcons/view-show.svg" : "/InstrumentValueIcons/view-hide.svg"
                color:              qgcPal.text
                fillMode:           Image.PreserveAspectFit

                MouseArea {
                    anchors.fill:       parent
                    anchors.margins:    -ScreenTools.defaultFontPixelHeight * 0.3
                    onClicked:          root.collapsed = !root.collapsed
                }
            }

            QGCLabel {
                anchors.verticalCenter: parent.verticalCenter
                text:                   root.cameraName
            }
        }
    }
}
