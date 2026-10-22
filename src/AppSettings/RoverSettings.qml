import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Rover page. The room bar on top chooses whose vehicles are shown: this computer's own links, vehicle models
/// and tunnels, those of a room this computer administers, or, for an operator, only the vehicles the admin
/// gave him (with no settings at all).
Rectangle {
    id:             root
    objectName:     "settingsPage_Rover"
    color:          "transparent"   // sits in the settings page card
    anchors.fill:   parent

    readonly property real  _margins:       ScreenTools.defaultFontPixelHeight
    readonly property var   _room:          RoomManager.currentRoomInfo
    readonly property bool  _inRoom:        RoomManager.currentRoom !== ""
    readonly property color _cardColor:     Qt.rgba(1, 1, 1, 0.04)
    readonly property color _cardBorder:    Qt.rgba(1, 1, 1, 0.10)

    property bool   _roomsOpen: false
    property string _info:      ""
    /// Tabs opened so far stay loaded, so switching tabs keeps unsaved edits
    property var    _visitedTabs: [ true, false, false ]

    function _roomIndex() {
        const rooms = RoomManager.rooms
        for (let i = 0; i < rooms.length; i++) {
            if (rooms[i].key === RoomManager.currentRoom) {
                return i + 1
            }
        }
        return 0
    }

    function _roomTitle(room) {
        return room.status === "pending" ? qsTr("%1 (waiting)").arg(room.name) : room.name
    }

    function _statusText(room) {
        if (!room || room.key === undefined) {
            return ""
        }
        if (room.status === "pending") {
            return qsTr("Waiting for the admin")
        }
        if (room.status === "rejected") {
            return qsTr("Request rejected")
        }
        if (!room.enabled) {
            return qsTr("Access turned off")
        }
        return room.role === "admin" ? qsTr("Admin") : qsTr("Operator")
    }

    function _statusColor(room) {
        if (!room || room.key === undefined) {
            return qgcPal.colorGrey
        }
        if (room.status === "active" && room.enabled) {
            return qgcPal.colorGreen
        }
        return room.status === "pending" ? qgcPal.colorOrange : qgcPal.colorRed
    }

    function _syncRoomCombo() {
        roomCombo.currentIndex = _roomIndex()
    }

    Component.onCompleted: {
        _syncRoomCombo()
        _roomsOpen = RoomManager.lastError !== ""
    }

    Connections {
        target: RoomManager
        function onCurrentRoomChanged() { root._syncRoomCombo() }
        function onRoomsChanged() { root._syncRoomCombo() }
        function onInfo(message) {
            root._info = message
            infoTimer.restart()
        }
        function onLastErrorChanged() {
            if (RoomManager.lastError !== "") {
                root._roomsOpen = true
            }
        }
    }

    Timer {
        id:             infoTimer
        interval:       10000
        onTriggered:    root._info = ""
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    _margins
        spacing:            _margins * 0.6

        // ---- Room bar ----
        Rectangle {
            Layout.fillWidth:       true
            Layout.preferredHeight: roomBarColumn.implicitHeight + _margins * 1.2
            radius:                 _margins
            color:                  _cardColor
            border.color:           _cardBorder

            ColumnLayout {
                id:                     roomBarColumn
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin:     _margins
                anchors.rightMargin:    _margins
                spacing:                _margins * 0.6

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth * 1.5

                    QGCLabel {
                        text:           qsTr("Room")
                        font.weight:    Font.ExtraBold
                    }

                    QGCComboBox {
                        id:                     roomCombo
                        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 34
                        model:                  [ qsTr("This computer") ].concat(RoomManager.rooms.map(room => root._roomTitle(room)))
                        onActivated: (index) => {
                            RoomManager.currentRoom = index === 0 ? "" : RoomManager.rooms[index - 1].key
                        }
                    }

                    Rectangle {
                        visible:                root._inRoom
                        Layout.preferredHeight: statusLabel.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.4
                        Layout.preferredWidth:  statusLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 2
                        radius:                 height / 2
                        color:                  "transparent"
                        border.color:           root._statusColor(root._room)

                        QGCLabel {
                            id:                 statusLabel
                            anchors.centerIn:   parent
                            text:               root._statusText(root._room)
                            color:              root._statusColor(root._room)
                            font.pointSize:     ScreenTools.smallFontPointSize
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               RoomManager.busy ? qsTr("Updating…") : ""
                        color:              qgcPal.colorGrey
                        font.pointSize:     ScreenTools.smallFontPointSize
                    }

                    QGCButton {
                        text:       root._roomsOpen ? qsTr("Hide") : qsTr("Rooms…")
                        onClicked:  root._roomsOpen = !root._roomsOpen
                    }
                }

                // Errors and confirmations of the room server
                RowLayout {
                    Layout.fillWidth:   true
                    visible:            RoomManager.lastError !== "" || root._info !== ""

                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        text:               RoomManager.lastError !== "" ? RoomManager.lastError : root._info
                        color:              RoomManager.lastError !== "" ? qgcPal.colorRed : qgcPal.colorGreen
                    }
                    QGCButton {
                        text:       "×"
                        visible:    RoomManager.lastError !== ""
                        onClicked:  RoomManager.clearError()
                    }
                }

                // Joining, creating and leaving rooms
                GridLayout {
                    Layout.fillWidth:   true
                    visible:            root._roomsOpen
                    columns:            3
                    columnSpacing:      ScreenTools.defaultFontPixelWidth * 1.5
                    rowSpacing:         _margins * 0.5

                    QGCLabel { text: qsTr("Join a room") }
                    QGCTextField {
                        id:                 joinTokenField
                        Layout.fillWidth:   true
                        placeholderText:    qsTr("Paste the room token from the admin (nxr1.…)")
                    }
                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Send request")
                        enabled:            joinTokenField.text.trim() !== ""
                        onClicked: {
                            RoomManager.joinRoom(joinTokenField.text)
                            joinTokenField.text = ""
                        }
                    }

                    QGCLabel {
                        text:       qsTr("New room")
                        visible:    RoomManager.serverAdmin
                    }
                    QGCTextField {
                        id:                 roomNameField
                        Layout.fillWidth:   true
                        visible:            RoomManager.serverAdmin
                        placeholderText:    qsTr("Room name, e.g. Room1")
                    }
                    QGCButton {
                        Layout.fillWidth:   true
                        visible:            RoomManager.serverAdmin
                        text:               qsTr("Create")
                        enabled:            roomNameField.text.trim() !== ""
                        onClicked: {
                            RoomManager.createRoom("", roomNameField.text)
                            roomNameField.text = ""
                        }
                    }

                    QGCLabel { text: qsTr("Server key") }
                    QGCTextField {
                        id:                 serverKeyField
                        Layout.fillWidth:   true
                        placeholderText:    RoomManager.serverAdmin ? qsTr("Added. Another server: paste its key (nxs1.…)")
                                                                    : qsTr("Admin only: the key printed when the server was installed (nxs1.…)")
                        echoMode:           TextInput.Password
                    }
                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Add server")
                        enabled:            serverKeyField.text.trim() !== ""
                        onClicked: {
                            RoomManager.addServerKey(serverKeyField.text)
                            serverKeyField.text = ""
                        }
                    }

                    QGCLabel { text: qsTr("This computer") }
                    QGCTextField {
                        Layout.fillWidth:   true
                        text:               RoomManager.deviceName
                        placeholderText:    qsTr("Name the admin sees")
                        onEditingFinished:  RoomManager.deviceName = text
                    }
                    QGCLabel {
                        text:       RoomManager.deviceCode !== "" ? qsTr("Code %1").arg(RoomManager.deviceCode) : ""
                        color:      qgcPal.colorGrey
                        font.family: ScreenTools.fixedFontFamily
                    }

                    Item { Layout.preferredHeight: 1; visible: root._inRoom }
                    QGCLabel {
                        Layout.fillWidth:   true
                        visible:            root._inRoom
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorGrey
                        font.pointSize:     ScreenTools.smallFontPointSize
                        text:               root._room.role === "admin"
                                                ? qsTr("If you leave the room, nobody will be able to manage it.")
                                                : qsTr("To come back you will need the room token again.")
                    }
                    QGCButton {
                        Layout.fillWidth:   true
                        visible:            root._inRoom
                        text:               qsTr("Leave room")
                        textColor:          qgcPal.warningText
                        onClicked: {
                            const room = root._room
                            QGroundControl.showMessageDialog(
                                root,
                                qsTr("Leave Room"),
                                qsTr("Leave room \"%1\"?").arg(room.name),
                                Dialog.Ok | Dialog.Cancel,
                                function () { RoomManager.leaveRoom(room.key) })
                        }
                    }
                }
            }
        }

        // ---- Waiting, rejected or turned off ----
        Rectangle {
            Layout.fillWidth:       true
            Layout.fillHeight:      true
            visible:                root._inRoom && RoomManager.currentRole === ""
            radius:                 _margins
            color:                  _cardColor
            border.color:           _cardBorder

            QGCLabel {
                anchors.centerIn:   parent
                width:              Math.min(parent.width - _margins * 4, ScreenTools.defaultFontPixelWidth * 70)
                horizontalAlignment: Text.AlignHCenter
                wrapMode:           Text.WordWrap
                color:              qgcPal.colorGrey
                text: {
                    if (root._room.status === "pending") {
                        return qsTr("The request to join \"%1\" was sent. The admin sees it in Access → Requests with this computer's name \"%2\" and code %3. The vehicles appear here once the admin lets you in.")
                                    .arg(root._room.name).arg(RoomManager.deviceName).arg(RoomManager.deviceCode)
                    }
                    if (root._room.status === "rejected") {
                        return qsTr("The admin rejected the request. You can send it again with the room token.")
                    }
                    return qsTr("The admin turned off your access to this room.")
                }
            }
        }

        // ---- Operator: the vehicles the admin gave this computer ----
        Rectangle {
            Layout.fillWidth:       true
            Layout.fillHeight:      true
            visible:                RoomManager.operatorMode
            radius:                 _margins
            color:                  _cardColor
            border.color:           _cardBorder

            ColumnLayout {
                anchors.fill:       parent
                anchors.margins:    _margins
                spacing:            _margins * 0.6

                QGCLabel {
                    text:           qsTr("Vehicles")
                    font.weight:    Font.ExtraBold
                    font.pointSize: ScreenTools.largeFontPointSize
                }
                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    color:              qgcPal.colorGrey
                    text:               qsTr("The admin of \"%1\" gave you these vehicles. Connect here or from the link menu on the main screen. Only one operator can drive a vehicle at a time.").arg(root._room.name ?? "")
                }

                QGCFlickable {
                    Layout.fillWidth:   true
                    Layout.fillHeight:  true
                    contentHeight:      boardColumn.height
                    clip:               true

                    Column {
                        id:         boardColumn
                        width:      parent.width
                        spacing:    _margins * 0.5

                        Repeater {
                            model: RoomManager.operatorMode ? RoomManager.boards : []

                            Rectangle {
                                id:         boardCard
                                width:      boardColumn.width
                                height:     boardRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 1.2
                                radius:     ScreenTools.defaultFontPixelHeight
                                color:      _active ? Qt.rgba(79 / 255, 209 / 255, 197 / 255, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                                border.color: _active ? qgcPal.buttonHighlight : _cardBorder

                                readonly property bool _active:     RoomManager.activeBoard === modelData.boardId
                                readonly property bool _connecting: RoomManager.connectingBoard === modelData.boardId
                                readonly property bool _taken:      modelData.occupiedBy !== "" && !modelData.occupiedByMe

                                RowLayout {
                                    id:                     boardRow
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
                                        color:                  boardCard._active ? qgcPal.colorGreen : (boardCard._taken ? qgcPal.colorOrange : Qt.rgba(1, 1, 1, 0.3))
                                    }

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
                                            color:          boardCard._taken ? qgcPal.colorOrange : qgcPal.colorGrey
                                            font.pointSize: ScreenTools.smallFontPointSize
                                            elide:          Text.ElideRight
                                            text: {
                                                if (boardCard._active)      return qsTr("Connected")
                                                if (boardCard._connecting)  return qsTr("Connecting…")
                                                if (boardCard._taken)       return qsTr("Occupied by %1").arg(modelData.occupiedBy)
                                                return qsTr("Free")
                                            }
                                        }
                                    }

                                    QGCButton {
                                        text:       boardCard._active ? qsTr("Disconnect") : qsTr("Connect")
                                        primary:    !boardCard._active
                                        enabled:    RoomManager.connectingBoard === ""
                                        onClicked: {
                                            if (boardCard._active) {
                                                RoomManager.disconnectBoard()
                                            } else {
                                                RoomManager.connectBoard(modelData.boardId)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        QGCLabel {
                            width:      parent.width
                            wrapMode:   Text.WordWrap
                            color:      qgcPal.colorGrey
                            visible:    RoomManager.boards.length === 0
                            text:       qsTr("No vehicles yet: the admin has to give you one.")
                        }
                    }
                }
            }
        }

        // ---- Own links, or the links of the room this computer administers ----
        ColumnLayout {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            visible:            !root._inRoom || RoomManager.currentRole === "admin"
            spacing:            _margins * 0.5

            QGCLabel {
                Layout.fillWidth:   true
                visible:            RoomManager.currentRole === "admin"
                wrapMode:           Text.WordWrap
                color:              qgcPal.colorGrey
                font.pointSize:     ScreenTools.smallFontPointSize
                text:               qsTr("Links of room \"%1\": each one is a vehicle of the room, sent to the server with its model and the server side of its tunnel. Give vehicles to operators in Access.").arg(root._room.name ?? "")
            }

            QGCTabBar {
                id:                 tabBar
                Layout.fillWidth:   true
                currentIndex:       0
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && !root._visitedTabs[currentIndex]) {
                        const visited = root._visitedTabs.slice()
                        visited[currentIndex] = true
                        root._visitedTabs = visited
                    }
                }

                QGCTabButton { text: qsTr("Links") }
                QGCTabButton { text: qsTr("Vehicle Models") }
                QGCTabButton { text: qsTr("WireGuard") }
            }

            StackLayout {
                Layout.fillWidth:   true
                Layout.fillHeight:  true
                currentIndex:       Math.max(0, tabBar.currentIndex)

                Item {
                    Loader {
                        anchors.fill:   parent
                        active:         root._visitedTabs[0]
                        source:         "qrc:/qml/QGroundControl/AppSettings/NxLinksSettings.qml"
                    }
                }
                Item {
                    Loader {
                        anchors.fill:   parent
                        active:         root._visitedTabs[1]
                        source:         "qrc:/qml/QGroundControl/AppSettings/VehicleModelsSettings.qml"
                    }
                }
                Item {
                    Loader {
                        anchors.fill:   parent
                        active:         root._visitedTabs[2]
                        source:         "qrc:/qml/QGroundControl/AppSettings/WireGuardSettings.qml"
                    }
                }
            }
        }
    }
}
