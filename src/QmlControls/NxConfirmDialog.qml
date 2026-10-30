import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Asks before something that cannot be undone (deleting a link, a tunnel, a model): the question, Cancel and the
/// action in orange, like the other confirmations of NX Rover. Open it through a QGCPopupDialogFactory with
/// { title, message, confirmText, action }.
QGCPopupDialog {
    id:         confirmDialog
    buttons:    Dialog.NoButton

    property string message
    property string confirmText:    qsTr("Delete")
    /// Called after the dialog closes when the action is confirmed
    property var    action:         null

    ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight

        QGCLabel {
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 50
            wrapMode:               Text.WordWrap
            text:                   confirmDialog.message
        }

        RowLayout {
            Layout.alignment:   Qt.AlignRight
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                text:       qsTr("Cancel")
                onClicked:  confirmDialog.close()
            }
            QGCButton {
                text:               confirmDialog.confirmText
                backgroundColor:    "#DB6536"
                textColor:          "#0B1F1D"
                fontWeight:         Font.DemiBold
                onClicked: {
                    const action = confirmDialog.action
                    confirmDialog.close()
                    if (action) {
                        action()
                    }
                }
            }
        }
    }
}
