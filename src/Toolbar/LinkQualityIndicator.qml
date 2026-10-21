import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Round trip time to the vehicle's router. Orange while the throttle is limited, red without replies;
/// the limit itself is announced above the speed on the drive screen.
Item {
    id:             control
    objectName:     "toolbar_linkQualityIndicator"
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          valuesRow.width

    property bool showIndicator: LinkQualityMonitor.monitoring

    readonly property bool _noReply:    LinkQualityMonitor.pingMs < 0
    readonly property bool _limiting:   LinkQualityMonitor.throttleLimitEnabled && LinkQualityMonitor.throttlePercent < 100
    readonly property color _color:     _noReply ? qgcPal.colorRed : (_limiting ? qgcPal.colorOrange : qgcPal.text)

    QGCPalette { id: qgcPal }

    // The text width is reserved for the widest value so a jumping ping never moves the neighbouring indicators
    TextMetrics {
        id:     widestPing
        font:   pingLabel.font
        text:   qsTr("%1 ms").arg(999)
    }
    TextMetrics {
        id:     widestNoReply
        font:   pingLabel.font
        text:   "---"
    }

    Row {
        id:                     valuesRow
        anchors.verticalCenter: parent.verticalCenter
        spacing:                ScreenTools.defaultFontPixelWidth * 0.6

        QGCColoredImage {
            anchors.verticalCenter: parent.verticalCenter
            width:                  ScreenTools.defaultFontPixelHeight * 1.3
            height:                 width
            sourceSize.height:      height
            source:                 "/res/nx/Ping.svg"
            color:                  control._color
        }

        QGCLabel {
            id:                     pingLabel
            anchors.verticalCenter: parent.verticalCenter
            width:                  Math.ceil(Math.max(widestPing.advanceWidth, widestNoReply.advanceWidth))
            horizontalAlignment:    Text.AlignLeft
            text:                   control._noReply ? "---" : qsTr("%1 ms").arg(LinkQualityMonitor.pingMs)
            color:                  control._color
            font.weight:            Font.ExtraBold
            font.pointSize:         ScreenTools.mediumFontPointSize
        }
    }

    MouseArea {
        anchors.fill:   parent
        onClicked:      mainWindow.showIndicatorDrawer(logChoicePage, control)
    }

    /// The click offers the two logs: the lagometer (video path) and the Starlink log
    Component {
        id: logChoicePage

        ToolIndicatorPage {
            contentComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelHeight / 2

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Lagometer")
                        onClicked:          control._openLog(lagometerPage)
                    }
                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("STARLINK")
                        onClicked:          control._openLog(starlinkLogPage)
                    }
                }
            }
        }
    }

    function _openLog(page) {
        mainWindow.closeIndicatorDrawer()
        mainWindow.showIndicatorDrawer(page, control)
    }

    Component {
        id: lagometerPage

        ToolIndicatorPage {
            contentComponent: Component {
                LagometerPanel {
                    width:              ScreenTools.defaultFontPixelWidth * 70
                    height:             ScreenTools.defaultFontPixelHeight * 32
                    preferredHost:      control._serverHost
                }
            }
        }
    }

    Component {
        id: starlinkLogPage

        ToolIndicatorPage {
            contentComponent: Component {
                StarlinkLogPanel {
                    width:              ScreenTools.defaultFontPixelWidth * 70
                    height:             ScreenTools.defaultFontPixelHeight * 32
                    preferredHost:      control._serverHost
                }
            }
        }
    }

    /// The log server runs on the VPS end of the tunnel: address .1 in the subnet of the pinged router
    readonly property string _serverHost: {
        const m = /^(\d+\.\d+\.\d+)\.\d+$/.exec(LinkQualityMonitor.pingAddress)
        return m ? m[1] + ".1" : ""
    }
}
