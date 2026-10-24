import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// NX top bar: logo menu, satellites, ping and battery on the left, arm and drive mode in the centre, the
/// link menu on the right. It is opaque; the map and video start below it.
Item {
    required property var guidedValueSlider

    id:     control
    width:  parent.width
    height: ScreenTools.toolbarHeight

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property bool   _communicationLost: _activeVehicle ? _activeVehicle.vehicleLinkManager.communicationLost : false
    property bool   _armed:             _activeVehicle ? _activeVehicle.armed : false
    property string _flightMode:        _activeVehicle ? _activeVehicle.flightMode : ""
    /// A connected link without a vehicle yet (no heartbeat), so it can still be shown and disconnected
    property var    _connectedConfig: {
        let configs = QGroundControl.linkManager.linkConfigurations
        for (let i = 0; i < configs.count; i++) {
            let config = configs.get(i)
            if (config && (!config.dynamic || config.fromRoom) && config.link) {
                return config
            }
        }
        return null
    }
    property string _linkName:          _activeVehicle ? _activeVehicle.vehicleLinkManager.primaryLinkName : (_connectedConfig ? _connectedConfig.name : "")
    property real   _margins:           ScreenTools.defaultFontPixelWidth
    property var    _guidedController:  globals.guidedControllerFlyView

    readonly property string _holdMode:     "Hold"
    readonly property string _manualMode:   "Manual"
    readonly property color  _barColor:     "#0E1216"
    readonly property color  _pillColor:    Qt.rgba(1, 1, 1, 0.06)
    readonly property color  _buttonColor:  Qt.rgba(1, 1, 1, 0.10)
    readonly property color  _accent:       "#DB6536"
    readonly property color  _accentText:   "#0B1F1D"

    /// Width of a toolbar switch that fits the longer of its two titles, so it keeps its size when it flips
    function _switchWidth(metrics) {
        return Math.ceil(metrics.width) + ScreenTools.defaultFontPixelWidth * 4
    }

    TextMetrics {
        id:     armTitles
        font:   Qt.font({ family: ScreenTools.normalFontFamily, pointSize: ScreenTools.defaultFontPointSize, weight: Font.ExtraBold })
        text:   qsTr("Disarmed").length > qsTr("Armed").length ? qsTr("Disarmed") : qsTr("Armed")
    }

    TextMetrics {
        id:     modeTitles
        font:   Qt.font({ family: ScreenTools.normalFontFamily, pointSize: ScreenTools.defaultFontPointSize, weight: Font.ExtraBold })
        text:   control._manualMode
    }

    /// Links offered in the connect menu. An operator in a room gets only the vehicles the admin gave him; the admin
    /// of a room gets the room's links; otherwise this computer's own links.
    function _menuLink(config) {
        if (!config) {
            return false
        }
        if (RoomManager.operatorMode) {
            return config.fromRoom
        }
        const room = RoomManager.currentRole === "admin" ? RoomManager.currentRoom : ""
        return !config.dynamic && config.showInMenu && config.room === room
    }

    function _escapeHtml(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    /// A link in the connect menu: the name in bold, the board ID below it in the same size, nothing else
    function _menuText(config) {
        const name = "<b>" + _escapeHtml(config.name) + "</b>"
        return config.boardId !== "" ? name + "<br>" + _escapeHtml(config.boardId) : name
    }

    /// Opens the link popup, which also lists the vehicle messages (called for critical messages).
    function dropMainStatusIndicatorTool() {
        mainWindow.showIndicatorDrawer(linkPopupComponent, linkButton)
    }

    QGCPalette { id: qgcPal }

    Rectangle {
        anchors.fill:   parent
        color:          control._barColor

        Rectangle {
            anchors.left:   parent.left
            anchors.right:  parent.right
            anchors.bottom: parent.bottom
            height:         1
            color:          Qt.rgba(1, 1, 1, 0.10)
        }
    }

    // ---- Left: logo menu ----
    RowLayout {
        id:                     leftLayout
        anchors.left:           parent.left
        anchors.leftMargin:     control._margins
        anchors.verticalCenter: parent.verticalCenter
        height:                 parent.height
        spacing:                control._margins / 2

        QGCToolBarButton {
            id:                 qgcButton
            objectName:         "toolbar_qgcLogo"
            Layout.fillHeight:  true
            icon.source:        "/res/QGCLogoWhite.svg"
            logo:               true
            onClicked:          mainWindow.showToolSelectDialog()
        }
    }

    // ---- Centre: arm and drive mode, or a pending guided action ----
    Rectangle {
        id:                     modePill
        anchors.centerIn:       parent
        height:                 control.height * 0.72
        width:                  modeRow.implicitWidth + control._margins
        radius:                 height / 2
        color:                  control._pillColor
        visible:                control._activeVehicle && !guidedActionConfirm.visible

        RowLayout {
            id:                 modeRow
            anchors.centerIn:   parent
            height:             parent.height - control._margins
            spacing:            control._margins / 2

            // Two switches. The safe state (Disarmed, Hold) is highlighted in orange; the driving state (Armed, Manual)
            // is plain white so it does not draw the eye. Disarming is a click; arming needs the button held for a
            // second so it cannot happen by accident.
            QGCButton {
                Layout.fillHeight:      true
                Layout.preferredWidth:  control._switchWidth(armTitles)
                visible:                control._armed
                text:                   qsTr("Armed")
                fontWeight:             Font.ExtraBold
                showBorder:             false
                backgroundColor:        "transparent"
                textColor:              qgcPal.text
                onClicked:              control._activeVehicle.armed = false
            }

            QGCDelayButton {
                Layout.fillHeight:      true
                Layout.preferredWidth:  Math.max(control._switchWidth(armTitles), implicitWidth)
                visible:                !control._armed
                text:                   qsTr("Disarmed")
                fontWeight:             Font.ExtraBold
                backRadius:             height / 2
                backgroundColor:        control._accent
                textColor:              control._accentText
                defaultDelay:           1000
                onActivated:            control._activeVehicle.armed = true
            }

            Rectangle {
                Layout.preferredWidth:  1
                Layout.fillHeight:      true
                Layout.margins:         control._margins / 2
                color:                  Qt.rgba(1, 1, 1, 0.12)
            }

            // Hold and Manual: a click switches between them. Any other mode the vehicle is in (e.g. after a
            // failsafe) is shown in orange too, so it is not hidden, and a click puts the vehicle on Hold.
            QGCButton {
                readonly property bool _manual: control._flightMode === control._manualMode

                Layout.fillHeight:      true
                Layout.preferredWidth:  Math.max(control._switchWidth(modeTitles), implicitWidth)
                text:                   control._flightMode !== "" ? control._flightMode : control._holdMode
                fontWeight:             Font.ExtraBold
                showBorder:             false
                checkable:              false
                backgroundColor:        _manual ? "transparent" : control._accent
                textColor:              _manual ? qgcPal.text : control._accentText
                onClicked:              control._activeVehicle.flightMode = (control._flightMode === control._holdMode) ? control._manualMode : control._holdMode
            }
        }
    }

    GuidedActionConfirm {
        id:                         guidedActionConfirm
        anchors.centerIn:           parent
        height:                     parent.height
        guidedController:           control._guidedController
        guidedValueSlider:          control.guidedValueSlider
        messageDisplay:             guidedActionMessageDisplay
    }

    // ---- Left, after the logo: satellites, ping, battery, vehicle problems (hidden when there are none) ----
    Row {
        id:                     indicatorsLayout
        anchors.left:           leftLayout.right
        anchors.leftMargin:     control._margins * 1.5
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        anchors.topMargin:      control.height * 0.18
        anchors.bottomMargin:   control.height * 0.18
        spacing:                ScreenTools.defaultFontPixelWidth * 0.8

        Repeater {
            model: control._activeVehicle ? [
                "qrc:/qml/QGroundControl/Toolbar/VehicleGPSIndicator.qml",
                "qrc:/qml/QGroundControl/Toolbar/LinkQualityIndicator.qml",
                "qrc:/qml/QGroundControl/Toolbar/BatteryIndicator.qml",
                "qrc:/qml/QGroundControl/Toolbar/VehicleAlertIndicator.qml"
            ] : [ "qrc:/qml/QGroundControl/Toolbar/LinkQualityIndicator.qml" ]

            Loader {
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                source:         modelData
                visible:        item && item.showIndicator
            }
        }
    }

    // ---- Right: the link menu ----
    Rectangle {
        id:                     linkButton
        objectName:             "toolbar_mainStatusIndicator"
        anchors.right:          parent.right
        anchors.rightMargin:    control._margins * 1.5
        anchors.verticalCenter: parent.verticalCenter
        height:                 control.height * 0.72
        width:                  linkRow.implicitWidth + control._margins * 2
        radius:                 ScreenTools.defaultFontPixelHeight / 2
        color:                  linkMouseArea.containsMouse ? control._buttonColor : "transparent"

        RowLayout {
            id:                     linkRow
            anchors.centerIn:       parent
            spacing:                control._margins

            Rectangle {
                Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.55
                Layout.preferredHeight: Layout.preferredWidth
                radius:                 width / 2
                color:                  control._activeVehicle ? (control._communicationLost ? qgcPal.colorRed : "#DB6536")
                                                               : (control._linkName !== "" ? qgcPal.colorOrange : qgcPal.colorGrey)
            }

            QGCLabel {
                text:           control._activeVehicle ? (control._communicationLost ? qsTr("%1 · comms lost").arg(control._linkName) : control._linkName)
                                                       : (control._linkName !== "" ? qsTr("%1 · waiting for vehicle").arg(control._linkName) : qsTr("Not connected"))
                font.pointSize: ScreenTools.largeFontPointSize
                font.weight:    Font.ExtraBold
            }

            QGCColoredImage {
                Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.9
                Layout.preferredHeight: Layout.preferredWidth
                sourceSize.height:      Layout.preferredHeight
                source:                 "/res/nx/ChevronDown.svg"
                color:                  qgcPal.text
            }
        }

        MouseArea {
            id:             linkMouseArea
            anchors.fill:   parent
            hoverEnabled:   true
            onClicked:      mainWindow.showIndicatorDrawer(linkPopupComponent, linkButton)
        }
    }

    // ---- Link popup: connect a link, or the connected link (name, board ID), disconnect and messages ----
    Component {
        id: linkPopupComponent

        ToolIndicatorPage {
            contentComponent: Component {
                ColumnLayout {
                    id:         linkPopupLayout
                    spacing:    ScreenTools.defaultFontPixelHeight / 2

                    property var _linkConfig: {
                        let configs = QGroundControl.linkManager.linkConfigurations
                        for (let i = 0; i < configs.count; i++) {
                            let config = configs.get(i)
                            if (config && config.name === control._linkName) {
                                return config
                            }
                        }
                        return null
                    }

                    // Connected (with or without a vehicle)
                    ColumnLayout {
                        spacing:    0
                        visible:    control._linkName !== ""

                        QGCLabel {
                            text:           control._linkName
                            font.weight:    Font.ExtraBold
                            font.pointSize: ScreenTools.mediumFontPointSize
                        }
                        QGCLabel {
                            text:           linkPopupLayout._linkConfig ? linkPopupLayout._linkConfig.boardId : ""
                            font.pointSize: ScreenTools.mediumFontPointSize
                            visible:        linkPopupLayout._linkConfig !== null && linkPopupLayout._linkConfig.boardId !== ""
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        visible:            control._linkName !== ""
                        text:               qsTr("Disconnect")
                        iconSource:         "/res/nx/Disconnect.svg"
                        fontWeight:         Font.ExtraBold
                        backgroundColor:    qgcPal.colorRed
                        textColor:          "white"
                        onClicked: {
                            mainWindow.closeIndicatorDrawer()
                            if (control._activeVehicle) {
                                control._activeVehicle.closeVehicle()
                            } else if (control._connectedConfig) {
                                QGroundControl.linkManager.disconnectLinkConfiguration(control._connectedConfig)
                            }
                        }
                    }

                    SettingsGroupLayout {
                        Layout.fillWidth:   true
                        heading:            qsTr("Vehicle Messages")
                        visible:            control._activeVehicle !== null

                        VehicleMessageList {
                            id:         vehicleMessageList
                            visible:    !noMessages
                        }
                        QGCLabel {
                            text:       qsTr("No new vehicle messages")
                            visible:    vehicleMessageList.noMessages
                        }
                    }

                    // Not connected
                    SettingsGroupLayout {
                        Layout.fillWidth:   true
                        heading:            RoomManager.currentRoom !== "" && RoomManager.currentRoomInfo.name !== undefined
                                                ? qsTr("Select Link to Connect: %1").arg(RoomManager.currentRoomInfo.name)
                                                : qsTr("Select Link to Connect")
                        visible:            control._linkName === ""

                        QGCLabel {
                            text: RoomManager.operatorMode ? qsTr("The admin has not given you any vehicles yet")
                                                           : qsTr("No Links Configured")
                            visible: {
                                const configs = QGroundControl.linkManager.linkConfigurations
                                for (let i = 0; i < configs.count; i++) {
                                    if (control._menuLink(configs.get(i))) {
                                        return false
                                    }
                                }
                                return true
                            }
                        }

                        Repeater {
                            model: QGroundControl.linkManager.linkConfigurations

                            delegate: QGCButton {
                                Layout.fillWidth:       true
                                text:                   control._menuText(object)
                                horizontalAlignment:    Text.AlignHCenter
                                visible:            control._menuLink(object)
                                enabled:            !object.link && RoomManager.connectingBoard === ""

                                onClicked: {
                                    if (object.fromRoom) {
                                        // Reserves the vehicle on the room server first; a busy vehicle is reported there
                                        RoomManager.connectBoard(object.roomBoardId)
                                    } else {
                                        QGroundControl.linkManager.createConnectedLink(object)
                                    }
                                    mainWindow.closeIndicatorDrawer()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // The guided action message display is outside of the GuidedActionConfirm control
    Rectangle {
        id:                         guidedActionMessageDisplay
        anchors.top:                control.bottom
        anchors.topMargin:          control._margins
        anchors.horizontalCenter:   parent.horizontalCenter
        width:                      messageLabel.contentWidth + (control._margins * 2)
        height:                     messageLabel.contentHeight + (control._margins * 2)
        color:                      qgcPal.windowTransparent
        radius:                     ScreenTools.defaultBorderRadius
        visible:                    guidedActionConfirm.visible

        QGCLabel {
            id:         messageLabel
            x:          control._margins
            y:          control._margins
            width:      ScreenTools.defaultFontPixelWidth * 30
            wrapMode:   Text.WordWrap
            text:       guidedActionConfirm.message
        }

        PropertyAnimation {
            id:         messageOpacityAnimation
            target:     guidedActionMessageDisplay
            property:   "opacity"
            from:       1
            to:         0
            duration:   500
        }

        Timer {
            id:             messageFadeTimer
            interval:       4000
            onTriggered:    messageOpacityAnimation.start()
        }
    }

    ParameterDownloadProgress {
        anchors.fill: parent
    }
}
