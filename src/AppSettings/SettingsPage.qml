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
            // NX: left aligned in the page card, as wide as the card up to a comfortable reading width
            x:          ScreenTools.defaultFontPixelWidth * 2
            width:      Math.max(implicitWidth, Math.min(root.width - x * 2, ScreenTools.defaultFontPixelWidth * 110))
            spacing:    ScreenTools.defaultFontPixelHeight
        }
    }
}
