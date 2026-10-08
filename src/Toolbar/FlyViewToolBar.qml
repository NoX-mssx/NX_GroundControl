import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// NX top bar: logo menu and link on the left, arm and drive mode in the centre, satellites, ping and
/// battery on the right. It is opaque; the map and video start below it.
Item {
    required property var guidedValueSlider

    id:     control
    width:  parent.width
    height: ScreenTools.toolbarHeight

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property bool   _communicationLost: _activeVehicle ? _activeVehicle.vehicleLinkManager.communicationLost : false
    property bool   _armed:             _activeVehicle ? _activeVehicle.armed : false
    property string _flightMode:        _activeVehicle ? _activeVehicle.flightMode : ""
    property string _linkName:          _activeVehicle ? _activeVehicle.vehicleLinkManager.primaryLinkName : ""
    property real   _margins:           ScreenTools.defaultFontPixelWidth
    property var    _guidedController:  globals.guidedControllerFlyView

    readonly property string _holdMode:     "Hold"
    readonly property string _manualMode:   "Manual"
    readonly property color  _barColor:     "#0E1216"
    readonly property color  _pillColor:    Qt.rgba(1, 1, 1, 0.06)
    readonly property color  _buttonColor:  Qt.rgba(1, 1, 1, 0.10)

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

    // ---- Left: logo menu and active link ----
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

        Rectangle {
            id:                     linkButton
            objectName:             "toolbar_mainStatusIndicator"
            Layout.preferredHeight: control.height * 0.72
            Layout.preferredWidth:  linkRow.implicitWidth + control._margins * 2
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
                    color:                  !control._activeVehicle ? qgcPal.colorGrey : (control._communicationLost ? qgcPal.colorRed : "#4FD1C5")
                }

                QGCLabel {
                    text:           control._activeVehicle ? (control._communicationLost ? qsTr("%1 · comms lost").arg(control._linkName) : control._linkName)
                                                           : qsTr("Not connected")
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

            // Disarming is immediate; arming needs the button held so it cannot happen by accident.
            QGCButton {
                Layout.fillHeight:  true
                visible:            control._armed
                text:               qsTr("Armed")
                fontWeight:         Font.ExtraBold
                backgroundColor:    qgcPal.colorRed
                textColor:          "white"
                onClicked:          control._activeVehicle.armed = false
            }

            QGCDelayButton {
                Layout.fillHeight:  true
                visible:            !control._armed
                text:               qsTr("Disarmed")
                fontWeight:         Font.ExtraBold
                backRadius:         height / 2
                backgroundColor:    control._buttonColor
                defaultDelay:       1000
                onActivated:        control._activeVehicle.armed = true
            }

            Rectangle {
                Layout.preferredWidth:  1
                Layout.fillHeight:      true
                Layout.margins:         control._margins / 2
                color:                  Qt.rgba(1, 1, 1, 0.12)
            }

            Repeater {
                model: [ control._holdMode, control._manualMode ]

                QGCButton {
                    required property string modelData

                    Layout.fillHeight:  true
                    text:               modelData
                    fontWeight:         Font.ExtraBold
                    showBorder:         false
                    checkable:          false
                    backgroundColor:    control._flightMode === modelData ? "#4FD1C5" : "transparent"
                    textColor:          control._flightMode === modelData ? "#0B1F1D" : qgcPal.text
                    onClicked:          control._activeVehicle.flightMode = modelData
                }
            }

            // Any other mode the vehicle is in (e.g. after a failsafe) is shown so it is not hidden.
            QGCLabel {
                Layout.rightMargin: control._margins
                text:               control._flightMode
                color:              qgcPal.colorOrange
                font.weight:        Font.ExtraBold
                visible:            control._flightMode !== "" && control._flightMode !== control._holdMode && control._flightMode !== control._manualMode
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

    // ---- Right: satellites, ping, battery, joystick ----
    Row {
        id:                     rightLayout
        anchors.right:          parent.right
        anchors.rightMargin:    control._margins * 1.5
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        anchors.topMargin:      control.height * 0.18
        anchors.bottomMargin:   control.height * 0.18
        spacing:                ScreenTools.defaultFontPixelWidth * 2

        Repeater {
            model: control._activeVehicle ? [
                "qrc:/qml/QGroundControl/Toolbar/VehicleGPSIndicator.qml",
                "qrc:/qml/QGroundControl/Toolbar/LinkQualityIndicator.qml",
                "qrc:/qml/QGroundControl/Toolbar/BatteryIndicator.qml",
                "qrc:/qml/QGroundControl/Toolbar/JoystickIndicator.qml"
            ] : [ "qrc:/qml/QGroundControl/Toolbar/LinkQualityIndicator.qml" ]

            Loader {
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                source:         modelData
                visible:        item && item.showIndicator
            }
        }
    }

    // ---- Link popup: connect a link, or the connected link with its board ID, disconnect and messages ----
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

                    // Connected
                    ColumnLayout {
                        spacing:    0
                        visible:    control._activeVehicle !== null

                        QGCLabel {
                            text:           control._linkName
                            font.weight:    Font.ExtraBold
                            font.pointSize: ScreenTools.mediumFontPointSize
                        }
                        QGCLabel {
                            text:           qsTr("Board ID %1").arg(linkPopupLayout._linkConfig ? linkPopupLayout._linkConfig.boardId : "")
                            font.pointSize: ScreenTools.smallFontPointSize
                            color:          qgcPal.colorGrey
                            visible:        linkPopupLayout._linkConfig !== null && linkPopupLayout._linkConfig.boardId !== ""
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        visible:            control._activeVehicle !== null
                        text:               qsTr("Disconnect")
                        iconSource:         "/res/nx/Disconnect.svg"
                        fontWeight:         Font.ExtraBold
                        backgroundColor:    qgcPal.colorRed
                        textColor:          "white"
                        onClicked: {
                            mainWindow.closeIndicatorDrawer()
                            if (control._activeVehicle) {
                                control._activeVehicle.closeVehicle()
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
                        heading:            qsTr("Select Link to Connect")
                        visible:            control._activeVehicle === null

                        QGCLabel {
                            text:       qsTr("No Links Configured")
                            visible:    QGroundControl.linkManager.linkConfigurations.count === 0
                        }

                        Repeater {
                            model: QGroundControl.linkManager.linkConfigurations

                            delegate: QGCButton {
                                Layout.fillWidth:   true
                                // The board ID follows the name in a smaller font (the label renders the <small> markup).
                                text:               object.name + (object.boardId !== "" ? "  <small>ID " + object.boardId + "</small>" : "") +
                                                    (object.link ? " (" + qsTr("Connected") + ")" : "")
                                visible:            !object.dynamic
                                enabled:            !object.link

                                onClicked: {
                                    QGroundControl.linkManager.createConnectedLink(object)
                                    mainWindow.closeIndicatorDrawer()
                                }
                            }
                        }

                        QGCButton {
                            Layout.fillWidth:   true
                            text:               qsTr("Configure Links")
                            onClicked: {
                                // Untranslated page key from SettingsPages.json — do not qsTr()
                                mainWindow.showSettingsTool("Comm Links")
                                mainWindow.closeIndicatorDrawer()
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
