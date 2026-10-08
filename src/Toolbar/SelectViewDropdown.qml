import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ToolIndicatorPage {
    id: root

    property real _toolButtonHeight: ScreenTools.defaultFontPixelHeight * 3

    contentComponent: Component {
        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelHeight * 0.15

            QGCPalette { id: qgcPal; colorGroupEnabled: true }

            // One row per view, each with its own icon
            Repeater {
                model: [
                    { name: "toolbar_viewFly",       label: qsTr("Drive"),            icon: "/res/nx/MenuDrive.svg",        visible: true },
                    { name: "toolbar_viewConfigure", label: qsTr("Vehicle Setup"),    icon: "/res/nx/MenuVehicleSetup.svg", visible: true },
                    { name: "toolbar_viewAnalyze",   label: qsTr("Analyze"),          icon: "/res/nx/MenuAnalyze.svg",      visible: QGroundControl.corePlugin.showAdvancedUI },
                    { name: "toolbar_viewSettings",  label: qsTr("Application Settings"), icon: "/res/nx/MenuSettings.svg", visible: !QGroundControl.corePlugin.options.combineSettingsAndSetup },
                    { name: "toolbar_viewClose",     label: qsTr("Close"),            icon: "/res/nx/MenuClose.svg",        visible: true }
                ]

                delegate: Rectangle {
                    id:                     menuRow
                    objectName:             modelData.name
                    Layout.fillWidth:       true
                    Layout.minimumWidth:    ScreenTools.defaultFontPixelWidth * 26
                    implicitHeight:         ScreenTools.defaultFontPixelHeight * 2.4
                    radius:                 ScreenTools.defaultFontPixelHeight / 2
                    color:                  menuMouseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    visible:                modelData.visible

                    required property var modelData

                    readonly property bool _isClose: modelData.name === "toolbar_viewClose"

                    RowLayout {
                        anchors.fill:           parent
                        anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.2
                        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 1.2
                        spacing:                ScreenTools.defaultFontPixelWidth * 1.2

                        QGCColoredImage {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 1.3
                            Layout.preferredHeight: Layout.preferredWidth
                            sourceSize.height:      Layout.preferredHeight
                            source:                 menuRow.modelData.icon
                            color:                  menuRow._isClose ? qgcPal.warningText : qgcPal.text
                            fillMode:               Image.PreserveAspectFit
                        }

                        QGCLabel {
                            Layout.fillWidth:   true
                            text:               menuRow.modelData.label
                            font.weight:        Font.DemiBold
                            color:              menuRow._isClose ? qgcPal.warningText : qgcPal.text
                        }
                    }

                    MouseArea {
                        id:             menuMouseArea
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked: {
                            if (!mainWindow.allowViewSwitch()) {
                                return
                            }
                            mainWindow.closeIndicatorDrawer()
                            switch (menuRow.modelData.name) {
                            case "toolbar_viewFly":         mainWindow.showFlyView(); break
                            case "toolbar_viewConfigure":   mainWindow.showVehicleConfig(); break
                            case "toolbar_viewAnalyze":     mainWindow.showAnalyzeTool(); break
                            case "toolbar_viewSettings":    mainWindow.showSettingsTool(); break
                            // Route through the window close handler so the active connection checks run.
                            case "toolbar_viewClose":       mainWindow.close(); break
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth:   true
                Layout.topMargin:   ScreenTools.defaultFontPixelHeight * 0.3
                implicitHeight:     1
                color:              qgcPal.groupBorder
            }

            ColumnLayout {
                id: versionColumnLayout
                Layout.fillWidth: true
                spacing: 0

                QGCLabel {
                    id: versionLabel
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("%1 Version").arg(QGroundControl.appName)
                    font.pointSize: ScreenTools.smallFontPointSize
                    wrapMode: QGCLabel.WordWrap
                }

                QGCLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: QGroundControl.qgcVersion
                    font.pointSize: ScreenTools.smallFontPointSize
                    wrapMode: QGCLabel.WrapAnywhere
                }

                QGCLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: QGroundControl.qgcAppDate
                    font.pointSize: ScreenTools.smallFontPointSize
                    wrapMode: QGCLabel.WrapAnywhere
                    visible: QGroundControl.qgcDailyBuild

                    QGCMouseArea {
                        anchors.topMargin: -(parent.y - versionLabel.y)
                        anchors.fill: parent

                        onClicked: (mouse) => {
                            if (mouse.modifiers & Qt.ControlModifier) {
                                QGroundControl.corePlugin.showTouchAreas = !QGroundControl.corePlugin.showTouchAreas
                                showTouchAreasNotification.open()
                            } else if (ScreenTools.isMobile || mouse.modifiers & Qt.ShiftModifier) {
                                mainWindow.closeIndicatorDrawer()
                                if (!QGroundControl.corePlugin.showAdvancedUI) {
                                    advancedModeOnConfirmation.open()
                                } else {
                                    advancedModeOffConfirmation.open()
                                }
                            }
                        }

                        // This allows you to change this on mobile
                        onPressAndHold: {
                            QGroundControl.corePlugin.showTouchAreas = !QGroundControl.corePlugin.showTouchAreas
                            showTouchAreasNotification.open()
                        }
                    }
                }
            }
        }
    }
}
