import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactControls
import QGroundControl.Controls

Item {
    id: root

    default property alias contentItem: mainLayout.data
    property int sectionFilter: -1

    QGCFlickable {
        objectName:     "settingsPageFlickable"
        anchors.fill:   parent
        contentWidth:   mainLayout.width
        contentHeight:  mainLayout.height

        ColumnLayout {
            id:         mainLayout
            // NX: centred in the page card, up to a comfortable reading width
            x:          Math.max(0, (root.width - width) / 2)
            width:      Math.max(implicitWidth, Math.min(root.width - ScreenTools.defaultFontPixelWidth * 4, ScreenTools.defaultFontPixelWidth * 90))
            spacing:    ScreenTools.defaultFontPixelHeight
        }
    }
}
