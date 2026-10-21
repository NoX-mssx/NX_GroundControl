import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// A card of settings on a setup page: heading and optional description at the top, the controls below.
/// Every setup page uses it, so all of them share the same card look and spacing.
ColumnLayout {
    id: control
    spacing: 0

    default property alias contentItem: _controlsColumn.data

    property string heading
    property string description
    property string iconSource

    property real _margins: ScreenTools.defaultFontPixelHeight * 0.8

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    Rectangle {
        implicitWidth: _cardColumn.implicitWidth + _margins * 2
        implicitHeight: _cardColumn.implicitHeight + _margins * 2
        Layout.fillWidth: true
        color: qgcPal.windowShade
        radius: ScreenTools.defaultFontPixelHeight * 0.6
        border.color: Qt.rgba(1, 1, 1, 0.10)

        ColumnLayout {
            id: _cardColumn
            y: _margins
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: _margins
            anchors.rightMargin: _margins
            spacing: ScreenTools.defaultFontPixelHeight * 0.6

            RowLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth * 1.5
                visible: control.heading !== "" || control.description !== ""

                QGCColoredImage {
                    Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 1.4
                    Layout.preferredHeight: Layout.preferredWidth
                    Layout.alignment: Qt.AlignTop
                    source: control.iconSource
                    color: qgcPal.text
                    fillMode: Image.PreserveAspectFit
                    visible: control.iconSource !== ""
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight * 0.15

                    QGCLabel {
                        Layout.fillWidth: true
                        text: control.heading
                        font.pointSize: ScreenTools.mediumFontPointSize
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        visible: control.heading !== ""
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        text: control.description
                        color: qgcPal.colorGrey
                        wrapMode: Text.WordWrap
                        visible: control.description !== ""
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Qt.rgba(1, 1, 1, 0.08)
                visible: control.heading !== "" || control.description !== ""
            }

            ColumnLayout {
                id: _controlsColumn
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelHeight * 0.6
            }
        }
    }
}
