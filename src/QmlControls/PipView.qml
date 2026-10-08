import QtQuick
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls

Item {
    id:         _root
    width:      _pipSize
    height:     _pipSize * (9/16)
    visible:    item2 && item2.pipState !== item2.pipState.window && show

    property var    item1:                  null    // Required
    property var    item2:                  null    // Optional, may come and go
    property string item1IsFullSettingsKey          // Settings key to save whether item1 was saved in full mode
    property bool   show:                   true

    readonly property string _pipExpandedSettingsKey: "IsPIPVisible"
    readonly property string _pipLockedSettingsKey:   "IsPIPLocked"

    property var    _fullItem
    property var    _pipOrWindowItem
    property alias  _windowContentItem: window.contentItem
    property alias  _pipContentItem:    pipContent
    property bool   _isExpanded:        true
    property real   _pipSize:           parent.width * 0.2
    property real   _maxSize:           0.75                // Percentage of parent control size
    property real   _minSize:           0.10
    property bool   _componentComplete: false
    // Unlocked, the window can be dragged anywhere and resized; locked, it can only be collapsed or expanded.
    property bool   _locked:            QGroundControl.loadBoolGlobalSetting(_pipLockedSettingsKey, true)
    property bool   _dragged:           false

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    Component.onCompleted: {
        _initForItems()
        _componentComplete = true
    }

    onItem2Changed: _initForItems()

    function showWindow() {
        window.width = _root.width
        window.height = _root.height
        window.show()
    }

    function _initForItems() {
        var item1IsFull = QGroundControl.loadBoolGlobalSetting(item1IsFullSettingsKey, true)
        if (item1 && item2) {
            item1.pipState.state = item1IsFull ? item1.pipState.fullState : item1.pipState.pipState
            item2.pipState.state = item1IsFull ? item2.pipState.pipState : item2.pipState.fullState
            _fullItem = item1IsFull ? item1 : item2
            _pipOrWindowItem = item1IsFull ? item2 : item1
        } else {
            item1.pipState.state = item1.pipState.fullState
            _fullItem = item1
            _pipOrWindowItem = null
        }
        _setPipIsExpanded(QGroundControl.loadBoolGlobalSetting(_pipExpandedSettingsKey, true))
    }

    function _swapPip() {
        var item1IsFull = false
        if (item1.pipState.state === item1.pipState.fullState) {
            item1.pipState.state = item1.pipState.pipState
            item2.pipState.state = item2.pipState.fullState
            _fullItem = item2
            _pipOrWindowItem = item1
            item1IsFull = false
        } else {
            item1.pipState.state = item1.pipState.fullState
            item2.pipState.state = item2.pipState.pipState
            _fullItem = item1
            _pipOrWindowItem = item2
            item1IsFull = true
        }
        QGroundControl.saveBoolGlobalSetting(item1IsFullSettingsKey, item1IsFull)
    }

    function _setLocked(locked) {
        QGroundControl.saveBoolGlobalSetting(_pipLockedSettingsKey, locked)
        _locked = locked
    }

    function _setPipIsExpanded(isExpanded) {
        QGroundControl.saveBoolGlobalSetting(_pipExpandedSettingsKey, isExpanded)
        _isExpanded = isExpanded
    }

    Window {
        id:         window
        visible:    false
        onClosing: {
            var item = contentItem.children[0]
            if (item) {
                item.pipState.windowAboutToClose()
                item.pipState.state = item.pipState.pipState
            }
        }
    }

    Item {
        id:             pipContent
        anchors.fill:   parent
        visible:        _isExpanded
        clip:           true
    }

    MouseArea {
        id:             pipMouseArea
        anchors.fill:   parent
        enabled:        _isExpanded
        preventStealing: true
        hoverEnabled:   true
        cursorShape:    _locked ? Qt.ArrowCursor : Qt.SizeAllCursor
        drag.target:    _locked ? null : _root
        drag.axis:      Drag.XAndYAxis
        drag.minimumX:  0
        drag.minimumY:  0
        drag.maximumX:  _root.parent ? Math.max(0, _root.parent.width - _root.width) : 0
        drag.maximumY:  _root.parent ? Math.max(0, _root.parent.height - _root.height) : 0

        onPressed: {
            _dragged = false
            if (!_locked) {
                // Free the window from its corner so the drag can move it.
                _root.anchors.left = undefined
                _root.anchors.bottom = undefined
            }
        }
        onPositionChanged: {
            if (drag.active) {
                _dragged = true
            }
        }
        // A press that moved the window is not a click.
        onClicked: {
            if (!_dragged) {
                _swapPip()
            }
        }
    }

    // MouseArea to drag in order to resize the PiP area
    MouseArea {
        id:                 pipResize
        anchors.fill:       pipResizeIcon
        enabled:            _isExpanded && !_locked
        preventStealing:    true
        cursorShape:        Qt.PointingHandCursor

        property real initialX:     0
        property real initialWidth: 0

        onPressed: (mouse) => {
            // Remove the anchor so the our mouse coordinates stay in the same original place for drag tracking
            pipResize.anchors.fill = undefined
            pipResize.initialX = mouse.x
            pipResize.initialWidth = _root.width
        }

        onReleased: pipResize.anchors.fill = pipResizeIcon

        // Drag
        onPositionChanged: (mouse) => {
            if (pipResize.pressed) {
                var parentWidth = _root.parent.width
                var newWidth = pipResize.initialWidth + mouse.x - pipResize.initialX
                if (newWidth < parentWidth * _maxSize && newWidth > parentWidth * _minSize) {
                    _pipSize = newWidth
                }
            }
        }
    }

    // Resize icon
    Image {
        id:             pipResizeIcon
        source:         "/qmlimages/pipResize.svg"
        fillMode:       Image.PreserveAspectFit
        mipmap:         true
        anchors.right:  parent.right
        anchors.top:    parent.top
        visible:        _isExpanded && !_locked
        height:         ScreenTools.defaultFontPixelHeight * 2.5
        width:          ScreenTools.defaultFontPixelHeight * 2.5
        sourceSize.height:  height
    }

    // Lock
    Rectangle {
        id:             lockButton
        anchors.left:   parent.left
        anchors.top:    parent.top
        anchors.margins: ScreenTools.defaultFontPixelHeight * 0.25
        width:          ScreenTools.defaultFontPixelHeight * 1.8
        height:         width
        radius:         ScreenTools.defaultFontPixelHeight / 4
        color:          Qt.rgba(0, 0, 0, 0.6)
        visible:        _isExpanded

        QGCColoredImage {
            anchors.centerIn:   parent
            width:              parent.width * 0.7
            height:             width
            sourceSize.height:  height
            source:             _locked ? "/InstrumentValueIcons/lock-closed.svg" : "/InstrumentValueIcons/lock-open.svg"
            color:              _locked ? "white" : qgcPal.colorOrange
            fillMode:           Image.PreserveAspectFit
        }

        MouseArea {
            anchors.fill:   parent
            onClicked:      _root._setLocked(!_root._locked)
        }
    }

    // Check min/max constraints on pip size when when parent is resized
    Connections {
        target: _root.parent

        function onWidthChanged() {
            if (!_componentComplete) {
                // Wait until first time setup is done
                return
            }
            var parentWidth = _root.parent.width
            if (_root.width > parentWidth * _maxSize) {
                _pipSize = parentWidth * _maxSize
            } else if (_root.width < parentWidth * _minSize) {
                _pipSize = parentWidth * _minSize
            }
        }
    }

    // Pip to Window
    Image {
        id:             popupPIP
        source:         "/qmlimages/PiP.svg"
        mipmap:         true
        fillMode:       Image.PreserveAspectFit
        anchors.left:   lockButton.right
        anchors.top:    parent.top
        visible:        _isExpanded && !ScreenTools.isMobile && pipMouseArea.containsMouse
        height:         ScreenTools.defaultFontPixelHeight * 2.5
        width:          ScreenTools.defaultFontPixelHeight * 2.5
        sourceSize.height:  height

        MouseArea {
            anchors.fill:   parent
            onClicked:      _pipOrWindowItem.pipState.state = _pipOrWindowItem.pipState.windowState
        }
    }

    Image {
        id:             hidePIP
        source:         "/qmlimages/pipHide.svg"
        mipmap:         true
        fillMode:       Image.PreserveAspectFit
        anchors.left:   parent.left
        anchors.bottom: parent.bottom
        visible:        _isExpanded && (ScreenTools.isMobile || pipMouseArea.containsMouse)
        height:         ScreenTools.defaultFontPixelHeight * 2.5
        width:          ScreenTools.defaultFontPixelHeight * 2.5
        sourceSize.height:  height
        MouseArea {
            anchors.fill:   parent
            onClicked:      _root._setPipIsExpanded(false)
        }
    }

    Rectangle {
        id:                     showPip
        anchors.left :          parent.left
        anchors.bottom:         parent.bottom
        height:                 ScreenTools.defaultFontPixelHeight * 2
        width:                  ScreenTools.defaultFontPixelHeight * 2
        radius:                 ScreenTools.defaultFontPixelHeight / 3
        visible:                !_isExpanded
        color:                  _fullItem.pipState.isDark ? Qt.rgba(0,0,0,0.75) : Qt.rgba(0,0,0,0.5)
        Image {
            width:              parent.width  * 0.75
            height:             parent.height * 0.75
            sourceSize.height:  height
            source:             "/res/buttonRight.svg"
            mipmap:             true
            fillMode:           Image.PreserveAspectFit
            anchors.verticalCenter:     parent.verticalCenter
            anchors.horizontalCenter:   parent.horizontalCenter
        }
        MouseArea {
            anchors.fill:   parent
            onClicked:      _root._setPipIsExpanded(true)
        }
    }
}
