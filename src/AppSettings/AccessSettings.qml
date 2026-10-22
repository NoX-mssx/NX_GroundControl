import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Access page of a room admin: the room token, the operators with the vehicles each one may drive, and the
/// join requests waiting for an answer.
Rectangle {
    id:             root
    objectName:     "settingsPage_Access"
    color:          "transparent"   // sits in the settings page card
    anchors.fill:   parent

    readonly property real  _margins:       ScreenTools.defaultFontPixelHeight
    readonly property var   _room:          RoomManager.currentRoomInfo
    readonly property color _cardColor:     Qt.rgba(1, 1, 1, 0.04)
    readonly property color _cardBorder:    Qt.rgba(1, 1, 1, 0.10)
    readonly property color _selectedColor: Qt.rgba(79 / 255, 209 / 255, 197 / 255, 0.08)

    property string _selectedId:    ""
    property bool   _showToken:     false
    property string _info:          ""

    readonly property var _selected: {
        const members = RoomManager.members
        for (let i = 0; i < members.length; i++) {
            if (members[i].deviceId === _selectedId) {
                return members[i]
            }
        }
        return null
    }

    function _board(boardId) {
        const boards = RoomManager.boards
        for (let i = 0; i < boards.length; i++) {
            if (boards[i].boardId === boardId) {
                return boards[i]
            }
        }
        return null
    }

    function _boardName(boardId) {
        const board = _board(boardId)
        return board ? board.name : boardId
    }

    function _memberName(deviceId) {
        const members = RoomManager.members
        for (let i = 0; i < members.length; i++) {
            if (members[i].deviceId === deviceId) {
                return members[i].name
            }
        }
        return ""
    }

    function _time(seconds) {
        return seconds > 0 ? new Date(seconds * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat) : ""
    }

    function _memberSubtitle(member) {
        let parts = [ member.role === "admin" ? qsTr("Admin") : qsTr("Operator") ]
        if (member.deviceCode !== "") parts.push(qsTr("code %1").arg(member.deviceCode))
        if (member.activeBoard !== "") parts.push(qsTr("driving %1").arg(_boardName(member.activeBoard)))
        else if (!member.enabled) parts.push(qsTr("access off"))
        return parts.join(" · ")
    }

    Connections {
        target: RoomManager
        function onInfo(message) {
            root._info = message
            infoTimer.restart()
        }
    }

    Timer {
        id:             infoTimer
        interval:       10000
        onTriggered:    root._info = ""
    }

    // Refreshes right away when the page opens, instead of waiting for the next periodic refresh
    Component.onCompleted: RoomManager.refresh()

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    _margins
        spacing:            _margins * 0.6

        // ---- Room and its token ----
        Rectangle {
            Layout.fillWidth:       true
            Layout.preferredHeight: headerColumn.implicitHeight + _margins * 1.2
            radius:                 _margins
            color:                  _cardColor
            border.color:           _cardBorder

            ColumnLayout {
                id:                     headerColumn
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin:     _margins
                anchors.rightMargin:    _margins
                spacing:                _margins * 0.5

                RowLayout {
                    Layout.fillWidth:   true

                    QGCLabel {
                        text:           qsTr("Access: %1").arg(root._room.name ?? "")
                        font.weight:    Font.ExtraBold
                        font.pointSize: ScreenTools.largeFontPointSize
                    }
                    Item { Layout.fillWidth: true }
                    QGCLabel {
                        text:           RoomManager.busy ? qsTr("Updating…") : ""
                        color:          qgcPal.colorGrey
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCLabel { text: qsTr("Room token") }
                    QGCTextField {
                        Layout.fillWidth:   true
                        readOnly:           true
                        text:               root._room.inviteToken ?? ""
                        echoMode:           root._showToken ? TextInput.Normal : TextInput.Password
                    }
                    QGCButton {
                        text:       root._showToken ? qsTr("Hide") : qsTr("Show")
                        onClicked:  root._showToken = !root._showToken
                    }
                    QGCButton {
                        text:       qsTr("Copy")
                        primary:    true
                        enabled:    (root._room.inviteToken ?? "") !== ""
                        onClicked: {
                            QGroundControl.copyToClipboard(root._room.inviteToken)
                            root._info = qsTr("The token is copied. Send it to the operator.")
                            infoTimer.restart()
                        }
                    }
                    QGCButton {
                        text:       qsTr("New token")
                        onClicked: {
                            QGroundControl.showMessageDialog(
                                root,
                                qsTr("New Room Token"),
                                qsTr("The old token will stop working for new requests. Operators already in the room keep their access."),
                                Dialog.Ok | Dialog.Cancel,
                                function () { RoomManager.rotateInvite() })
                        }
                    }
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    font.pointSize:     ScreenTools.smallFontPointSize
                    color:              RoomManager.lastError !== "" ? qgcPal.colorRed : (root._info !== "" ? qgcPal.colorGreen : qgcPal.colorGrey)
                    text:               RoomManager.lastError !== "" ? RoomManager.lastError
                                            : (root._info !== "" ? root._info
                                                                 : qsTr("The operator pastes the token in Rover → Rooms; the request then appears under Requests."))
                }
            }
        }

        QGCTabBar {
            id:                 tabBar
            Layout.fillWidth:   true
            currentIndex:       0

            QGCTabButton { text: qsTr("Operators (%1)").arg(RoomManager.members.length) }
            QGCTabButton { text: RoomManager.requests.length > 0 ? qsTr("Requests (%1)").arg(RoomManager.requests.length) : qsTr("Requests") }
        }

        StackLayout {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            currentIndex:       Math.max(0, tabBar.currentIndex)

            // ---- Operators ----
            Item {
                ColumnLayout {
                    id:             memberColumn
                    anchors.left:   parent.left
                    anchors.top:    parent.top
                    anchors.bottom: parent.bottom
                    width:          ScreenTools.defaultFontPixelWidth * 40
                    spacing:        _margins * 0.5

                    QGCFlickable {
                        Layout.fillWidth:   true
                        Layout.fillHeight:  true
                        contentHeight:      memberList.height
                        clip:               true

                        Column {
                            id:         memberList
                            width:      parent.width
                            spacing:    _margins * 0.5

                            Repeater {
                                model: RoomManager.members

                                Rectangle {
                                    id:         memberCard
                                    width:      memberList.width
                                    height:     memberRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 1.2
                                    radius:     ScreenTools.defaultFontPixelHeight
                                    color:      _isSelected ? root._selectedColor : (memberMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : root._cardColor)
                                    border.color: _isSelected ? qgcPal.buttonHighlight : root._cardBorder

                                    readonly property bool _isSelected: root._selectedId === modelData.deviceId

                                    MouseArea {
                                        id:             memberMouse
                                        anchors.fill:   parent
                                        hoverEnabled:   true
                                        onClicked:      root._selectedId = modelData.deviceId
                                    }

                                    RowLayout {
                                        id:                     memberRow
                                        anchors.left:           parent.left
                                        anchors.right:          parent.right
                                        anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.6
                                        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing:                ScreenTools.defaultFontPixelWidth * 1.2

                                        Rectangle {
                                            Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.55
                                            Layout.preferredHeight: Layout.preferredWidth
                                            radius:                 width / 2
                                            color:                  modelData.activeBoard !== "" ? qgcPal.colorGreen
                                                                        : (modelData.enabled ? Qt.rgba(1, 1, 1, 0.3) : qgcPal.colorRed)
                                        }

                                        Column {
                                            Layout.fillWidth: true

                                            QGCLabel {
                                                width:          parent.width
                                                text:           modelData.isMe ? qsTr("%1 (you)").arg(modelData.name) : modelData.name
                                                font.weight:    Font.ExtraBold
                                                font.pointSize: ScreenTools.mediumFontPointSize
                                                elide:          Text.ElideRight
                                            }
                                            QGCLabel {
                                                width:          parent.width
                                                text:           root._memberSubtitle(modelData)
                                                color:          qgcPal.colorGrey
                                                font.pointSize: ScreenTools.smallFontPointSize
                                                elide:          Text.ElideRight
                                            }
                                        }

                                        // Off: the operator keeps his place in the room but cannot connect
                                        QGCCheckBox {
                                            checked:            modelData.enabled
                                            enabled:            !modelData.isMe
                                            onClicked:          RoomManager.setMemberEnabled(modelData.deviceId, checked)

                                            ToolTip.visible:    hovered
                                            ToolTip.delay:      400
                                            ToolTip.text:       qsTr("Access to the room")
                                        }
                                    }
                                }
                            }

                            QGCLabel {
                                width:      parent.width
                                wrapMode:   Text.WordWrap
                                color:      qgcPal.colorGrey
                                visible:    RoomManager.members.length <= 1
                                text:       qsTr("No operators yet. Send them the room token.")
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.left:       memberColumn.right
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.bottom:     parent.bottom
                    anchors.leftMargin: _margins
                    radius:             _margins
                    color:              _cardColor
                    border.color:       _cardBorder

                    Loader {
                        anchors.fill:       parent
                        anchors.margins:    _margins
                        sourceComponent:    root._selected ? memberPanel : roomBoardsPanel
                    }
                }
            }

            // ---- Requests ----
            Item {
                QGCFlickable {
                    anchors.fill:   parent
                    contentHeight:  requestList.height
                    clip:           true

                    Column {
                        id:         requestList
                        width:      Math.min(parent.width, ScreenTools.defaultFontPixelWidth * 110)
                        spacing:    _margins * 0.5

                        Repeater {
                            model: RoomManager.requests

                            Rectangle {
                                width:      requestList.width
                                height:     requestRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 1.2
                                radius:     ScreenTools.defaultFontPixelHeight
                                color:      root._cardColor
                                border.color: root._cardBorder

                                RowLayout {
                                    id:                     requestRow
                                    anchors.left:           parent.left
                                    anchors.right:          parent.right
                                    anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.6
                                    anchors.rightMargin:    ScreenTools.defaultFontPixelWidth
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing:                ScreenTools.defaultFontPixelWidth

                                    Column {
                                        Layout.fillWidth: true

                                        QGCLabel {
                                            width:          parent.width
                                            text:           modelData.name
                                            font.weight:    Font.ExtraBold
                                            font.pointSize: ScreenTools.mediumFontPointSize
                                            elide:          Text.ElideRight
                                        }
                                        QGCLabel {
                                            width:          parent.width
                                            text:           qsTr("Code %1 · %2").arg(modelData.deviceCode).arg(root._time(modelData.requested))
                                            color:          qgcPal.colorGrey
                                            font.pointSize: ScreenTools.smallFontPointSize
                                            elide:          Text.ElideRight
                                        }
                                    }

                                    QGCButton {
                                        text:       qsTr("Reject")
                                        onClicked:  RoomManager.rejectRequest(modelData.deviceId)
                                    }
                                    QGCButton {
                                        text:       qsTr("Let in as admin")
                                        onClicked: {
                                            const request = modelData
                                            QGroundControl.showMessageDialog(
                                                root,
                                                qsTr("Second Admin"),
                                                qsTr("\"%1\" will manage the room like you: its vehicles, operators and requests.").arg(request.name),
                                                Dialog.Ok | Dialog.Cancel,
                                                function () { RoomManager.approveRequest(request.deviceId, true) })
                                        }
                                    }
                                    QGCButton {
                                        text:       qsTr("Let in")
                                        primary:    true
                                        onClicked: {
                                            const deviceId = modelData.deviceId
                                            RoomManager.approveRequest(deviceId, false)
                                            root._selectedId = deviceId
                                            tabBar.currentIndex = 0
                                        }
                                    }
                                }
                            }
                        }

                        QGCLabel {
                            width:      parent.width
                            wrapMode:   Text.WordWrap
                            color:      qgcPal.colorGrey
                            visible:    RoomManager.requests.length === 0
                            text:       qsTr("No requests. When an operator pastes the room token, the request appears here with his computer's name and code: check the code with him before letting him in.")
                        }
                    }
                }
            }
        }
    }

    // ---- Selected operator: his vehicles ----
    Component {
        id: memberPanel

        ColumnLayout {
            spacing: _margins * 0.6

            readonly property var _member: root._selected

            QGCLabel {
                text:           _member ? _member.name : ""
                font.weight:    Font.ExtraBold
                font.pointSize: ScreenTools.largeFontPointSize
            }
            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                color:              qgcPal.colorGrey
                text:               _member ? root._memberSubtitle(_member) + (_member.lastSeen > 0 ? " · " + qsTr("last seen %1").arg(root._time(_member.lastSeen)) : "") : ""
            }

            QGCCheckBoxSlider {
                Layout.fillWidth:   true
                text:               qsTr("Access to the room")
                enabled:            _member && !_member.isMe
                checked:            _member ? _member.enabled : false
                onClicked:          RoomManager.setMemberEnabled(_member.deviceId, checked)
            }

            QGCLabel {
                text:           qsTr("Vehicles")
                font.weight:    Font.ExtraBold
            }

            QGCFlickable {
                Layout.fillWidth:   true
                Layout.fillHeight:  true
                contentHeight:      grantedColumn.height
                clip:               true

                Column {
                    id:         grantedColumn
                    width:      parent.width
                    spacing:    _margins * 0.4

                    Repeater {
                        model: _member ? _member.boards : []

                        Rectangle {
                            width:      grantedColumn.width
                            height:     grantedRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.8
                            radius:     ScreenTools.defaultFontPixelHeight * 0.7
                            color:      root._cardColor
                            border.color: root._cardBorder

                            readonly property var _board: root._board(modelData)

                            RowLayout {
                                id:                     grantedRow
                                anchors.left:           parent.left
                                anchors.right:          parent.right
                                anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.4
                                anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 0.6
                                anchors.verticalCenter: parent.verticalCenter

                                QGCLabel {
                                    text:           root._boardName(modelData)
                                    font.weight:    Font.ExtraBold
                                }
                                QGCLabel {
                                    Layout.fillWidth:   true
                                    elide:              Text.ElideRight
                                    font.pointSize:     ScreenTools.smallFontPointSize
                                    color:              _member && _member.activeBoard === modelData ? qgcPal.colorGreen : qgcPal.colorGrey
                                    text: {
                                        if (_member && _member.activeBoard === modelData) return qsTr("driving now")
                                        if (_board && _board.occupiedBy !== "") return qsTr("occupied by %1").arg(_board.occupiedBy)
                                        return ""
                                    }
                                }
                                QGCButton {
                                    text:       qsTr("Take away")
                                    onClicked:  RoomManager.setBoardGranted(_member.deviceId, modelData, false)
                                }
                            }
                        }
                    }

                    QGCLabel {
                        width:      parent.width
                        wrapMode:   Text.WordWrap
                        color:      qgcPal.colorGrey
                        visible:    !_member || _member.boards.length === 0
                        text:       _member && _member.role === "admin" ? qsTr("Admins can drive every vehicle of the room.")
                                                                         : qsTr("No vehicles given yet.")
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                QGCButton {
                    text:       qsTr("Remove from room")
                    textColor:  qgcPal.warningText
                    visible:    _member && !_member.isMe
                    onClicked: {
                        const member = _member
                        QGroundControl.showMessageDialog(
                            root,
                            qsTr("Remove Operator"),
                            qsTr("Remove \"%1\" from the room? To come back he will need the room token and your approval.").arg(member.name),
                            Dialog.Ok | Dialog.Cancel,
                            function () {
                                root._selectedId = ""
                                RoomManager.removeMember(member.deviceId)
                            })
                    }
                }
                Item { Layout.fillWidth: true }
                QGCButton {
                    text:       qsTr("Back to vehicles")
                    onClicked:  root._selectedId = ""
                }
                QGCButton {
                    text:       qsTr("Give a vehicle")
                    primary:    true
                    fontWeight: Font.ExtraBold
                    visible:    _member && _member.role !== "admin"
                    enabled:    _member && _member.enabled
                    onClicked:  giveDialogFactory.open({ member: _member })
                }
            }
        }
    }

    // ---- No operator selected: the vehicles of the room ----
    Component {
        id: roomBoardsPanel

        ColumnLayout {
            spacing: _margins * 0.6

            QGCLabel {
                text:           qsTr("Vehicles of the room")
                font.weight:    Font.ExtraBold
                font.pointSize: ScreenTools.largeFontPointSize
            }
            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                color:              qgcPal.colorGrey
                text:               qsTr("Select an operator on the left to give him vehicles. The vehicles are the links of this room in Rover.")
            }

            QGCFlickable {
                Layout.fillWidth:   true
                Layout.fillHeight:  true
                contentHeight:      roomBoardColumn.height
                clip:               true

                Column {
                    id:         roomBoardColumn
                    width:      parent.width
                    spacing:    _margins * 0.4

                    Repeater {
                        model: RoomManager.boards

                        Rectangle {
                            width:      roomBoardColumn.width
                            // Rows with and without buttons are the same height
                            height:     Math.max(roomBoardRow.implicitHeight, ScreenTools.defaultFontPixelHeight * 2) + ScreenTools.defaultFontPixelHeight * 0.8
                            radius:     ScreenTools.defaultFontPixelHeight * 0.7
                            color:      root._cardColor
                            border.color: root._cardBorder

                            RowLayout {
                                id:                     roomBoardRow
                                anchors.left:           parent.left
                                anchors.right:          parent.right
                                anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.4
                                anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 0.6
                                anchors.verticalCenter: parent.verticalCenter

                                Rectangle {
                                    Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.5
                                    Layout.preferredHeight: Layout.preferredWidth
                                    radius:                 width / 2
                                    color:                  modelData.occupiedBy !== "" ? qgcPal.colorGreen : Qt.rgba(1, 1, 1, 0.3)
                                }
                                QGCLabel {
                                    text:           modelData.name
                                    font.weight:    Font.ExtraBold
                                }
                                QGCLabel {
                                    Layout.fillWidth:   true
                                    elide:              Text.ElideRight
                                    font.pointSize:     ScreenTools.smallFontPointSize
                                    color:              modelData.hasLink ? qgcPal.colorGrey : qgcPal.colorOrange
                                    text: {
                                        let parts = []
                                        if (modelData.occupiedBy !== "") parts.push(qsTr("driven by %1").arg(modelData.occupiedBy))
                                        parts.push(modelData.grantedTo.length > 0 ? qsTr("given to %1").arg(modelData.grantedTo.length) : qsTr("not given yet"))
                                        if (!modelData.hasLink) parts.push(qsTr("its link was deleted"))
                                        return parts.join(" · ")
                                    }
                                }
                                // Gives the vehicle back when an operator's computer went away without closing the link
                                QGCButton {
                                    text:       qsTr("Free")
                                    visible:    modelData.occupiedBy !== ""
                                    onClicked: {
                                        const board = modelData
                                        QGroundControl.showMessageDialog(
                                            root,
                                            qsTr("Free Vehicle"),
                                            qsTr("Take %1 away from %2? His connection to the vehicle stops.").arg(board.name).arg(board.occupiedBy),
                                            Dialog.Ok | Dialog.Cancel,
                                            function () { RoomManager.freeBoard(board.boardId) })
                                    }
                                }
                                QGCButton {
                                    text:       qsTr("Remove")
                                    visible:    !modelData.hasLink
                                    textColor:  qgcPal.warningText
                                    onClicked:  RoomManager.deleteBoard(modelData.boardId)
                                }
                            }
                        }
                    }

                    QGCLabel {
                        width:      parent.width
                        wrapMode:   Text.WordWrap
                        color:      qgcPal.colorGrey
                        visible:    RoomManager.boards.length === 0
                        text:       qsTr("No vehicles in the room yet. Add links in Rover → Links while this room is selected.")
                    }
                }
            }
        }
    }

    QGCPopupDialogFactory {
        id:                 giveDialogFactory
        dialogComponent:    giveDialogComponent
    }

    Component {
        id: giveDialogComponent

        QGCPopupDialog {
            id:         giveDialog
            title:      qsTr("Give a Vehicle to %1").arg(member ? member.name : "")
            buttons:    Dialog.Ok | Dialog.Cancel
            acceptButtonEnabled: _chosen.length > 0

            property var member:    null
            property var _chosen:   []

            readonly property var _available: {
                if (!member) {
                    return []
                }
                return RoomManager.boards.filter(board => member.boards.indexOf(board.boardId) < 0)
            }

            onAccepted: {
                for (let i = 0; i < _chosen.length; i++) {
                    RoomManager.setBoardGranted(member.deviceId, _chosen[i], true)
                }
            }

            ColumnLayout {
                spacing: ScreenTools.defaultFontPixelHeight / 2

                QGCLabel {
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 50
                    wrapMode:               Text.WordWrap
                    text:                   giveDialog._available.length > 0
                                                ? qsTr("Choose the vehicles. The operator sees them in his link menu at once; several operators may share a vehicle, but only one drives it at a time.")
                                                : qsTr("All vehicles of the room are already given to this operator.")
                }

                Repeater {
                    model: giveDialog._available

                    QGCCheckBox {
                        text:       modelData.occupiedBy !== "" ? qsTr("%1 (driven by %2)").arg(modelData.name).arg(modelData.occupiedBy) : modelData.name
                        onClicked: {
                            const chosen = giveDialog._chosen.filter(id => id !== modelData.boardId)
                            if (checked) {
                                chosen.push(modelData.boardId)
                            }
                            giveDialog._chosen = chosen
                        }
                    }
                }
            }
        }
    }
}
