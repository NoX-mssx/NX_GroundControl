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
    /// The token panel can be collapsed to the room name; the choice is remembered
    property bool   _tokenOpen:     QGroundControl.loadBoolGlobalSetting(_tokenOpenKey, true)

    readonly property string _tokenOpenKey: "NxAccessTokenOpen"

    function _setTokenOpen(open) {
        _tokenOpen = open
        QGroundControl.saveBoolGlobalSetting(_tokenOpenKey, open)
    }
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
                    QGCButton {
                        text:       root._tokenOpen ? qsTr("Collapse") : qsTr("Room token…")
                        onClicked:  root._setTokenOpen(!root._tokenOpen)
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth
                    visible:            root._tokenOpen

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

                // Errors and confirmations stay visible with the panel collapsed; the hint only with it open
                QGCLabel {
                    Layout.fillWidth:   true
                    visible:            root._tokenOpen || RoomManager.lastError !== "" || root._info !== ""
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
            QGCTabButton { text: qsTr("Radio") }
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

            // ---- Radio: the playlist the room server plays to the operators ----
            Item {
                id: radioTab

                readonly property var  _radio:      RoomManager.radio
                readonly property var  _tracks:     _radio.tracks || []
                readonly property var  _queue:      _radio.queue || []
                readonly property var  _now:        _radio.now || null
                readonly property var  _next:       _radio.next || null
                readonly property var  _leveling:   _radio.leveling || null
                readonly property var  _upload:     RoomManager.radioUpload
                readonly property bool _supported:  _radio.supported === true
                /// Shuffle, the queue, renaming and leveling: a hub from before them has none of these
                readonly property bool _tools:      _radio.playlistTools === true
                readonly property int  _unleveled:  _tracks.filter(track => track.leveled === false).length
                /// Height of a track card that is not opened, for dragging cards over each other
                readonly property real _rowHeight:  ScreenTools.defaultFontPixelHeight * 2.6

                property string _selectedTrack: ""
                property string _renamingTrack: ""

                function _duration(seconds) {
                    const total = Math.round(seconds || 0)
                    const minutes = Math.floor(total / 60)
                    const rest = total % 60
                    return minutes + ":" + (rest < 10 ? "0" : "") + rest
                }

                function _megabytes(bytes) {
                    return Math.round((bytes || 0) / (1024 * 1024))
                }

                function _rename(trackId, name) {
                    if (name.trim() !== "") {
                        RoomManager.renameRadioTrack(trackId, name)
                    }
                    _renamingTrack = ""
                }

                // What plays and how far, refreshed while the tab is open; more often while tracks are being leveled
                Timer {
                    interval:           radioTab._leveling ? 2000 : 5000
                    repeat:             true
                    running:            radioTab.visible
                    triggeredOnStart:   true
                    onTriggered:        RoomManager.refreshRadio()
                }

                FileDialog {
                    id:             trackDialog
                    title:          qsTr("Add Tracks")
                    fileMode:       FileDialog.OpenFiles
                    nameFilters:    radioTab._radio.convert ? [ qsTr("Audio (*.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav *.wma)"), qsTr("All files (*)") ]
                                                            : [ qsTr("MP3 (*.mp3)") ]
                    onAccepted:     RoomManager.uploadRadioTracks(selectedFiles)
                }

                QGCPopupDialogFactory {
                    id:                 trackConfirmFactory
                    dialogComponent:    trackConfirmComponent
                }

                Component {
                    id: trackConfirmComponent

                    NxConfirmDialog { }
                }

                QGCLabel {
                    anchors.centerIn:   parent
                    width:              Math.min(parent.width - _margins * 2, ScreenTools.defaultFontPixelWidth * 70)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode:           Text.WordWrap
                    color:              qgcPal.colorGrey
                    visible:            !radioTab._supported
                    text:               qsTr("The room server does not have the radio yet. Install the new version of NX Hub on the VPS.")
                }

                ColumnLayout {
                    anchors.fill:   parent
                    spacing:        _margins * 0.6
                    visible:        radioTab._supported

                    // On air, in order or shuffled, what plays, what comes next, how many listen
                    Rectangle {
                        Layout.fillWidth:       true
                        Layout.preferredHeight: airRow.implicitHeight + _margins
                        radius:                 _margins
                        color:                  _cardColor
                        border.color:           radioTab._radio.onAir ? qgcPal.buttonHighlight : _cardBorder

                        RowLayout {
                            id:                     airRow
                            anchors.left:           parent.left
                            anchors.right:          parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin:     _margins
                            anchors.rightMargin:    _margins
                            spacing:                ScreenTools.defaultFontPixelWidth * 2

                            QGCCheckBoxSlider {
                                text:       qsTr("On air")
                                checked:    radioTab._radio.onAir === true
                                onClicked:  RoomManager.setRadioOnAir(checked)
                            }

                            // Shuffled: rounds in which every track plays once, each round in a new order
                            QGCCheckBoxSlider {
                                text:       qsTr("Shuffle")
                                visible:    radioTab._tools
                                checked:    radioTab._radio.shuffle === true
                                onClicked:  RoomManager.setRadioShuffle(checked)
                            }

                            Column {
                                Layout.fillWidth: true

                                QGCLabel {
                                    width:          parent.width
                                    elide:          Text.ElideRight
                                    font.weight:    Font.DemiBold
                                    text: {
                                        if (!radioTab._radio.onAir) {
                                            return qsTr("The radio is off. Operators do not see it.")
                                        }
                                        if (!radioTab._now) {
                                            return qsTr("On air, but the playlist is empty")
                                        }
                                        return qsTr("Now: %1 (%2 of %3)").arg(radioTab._now.name)
                                                    .arg(radioTab._duration(Math.min(radioTab._now.elapsed, radioTab._now.duration)))
                                                    .arg(radioTab._duration(radioTab._now.duration))
                                    }
                                }
                                QGCLabel {
                                    width:          parent.width
                                    elide:          Text.ElideRight
                                    color:          qgcPal.colorGrey
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    visible:        radioTab._radio.onAir === true
                                    text: {
                                        const parts = []
                                        if (radioTab._next && radioTab._tracks.length > 1) {
                                            parts.push(qsTr("Next: %1").arg(radioTab._next.name))
                                        }
                                        parts.push(qsTr("Listening now: %1").arg(radioTab._radio.listeners || 0))
                                        return parts.join("   ·   ")
                                    }
                                }
                            }

                            QGCButton {
                                text:       qsTr("Next track")
                                enabled:    radioTab._radio.onAir === true && radioTab._tracks.length > 1
                                onClicked:  RoomManager.nextRadioTrack()
                            }
                        }
                    }

                    // Adding tracks
                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCButton {
                            text:       qsTr("Add tracks…")
                            primary:    true
                            onClicked:  trackDialog.open()
                        }

                        ColumnLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelHeight * 0.2
                            visible:            radioTab._upload.active === true

                            QGCLabel {
                                Layout.fillWidth:   true
                                elide:              Text.ElideRight
                                text: (radioTab._upload.progress || 0) < 0.9
                                          ? qsTr("Uploading %1 of %2: %3").arg(radioTab._upload.index).arg(radioTab._upload.count).arg(radioTab._upload.name)
                                          : qsTr("Converting %1 of %2 on the server: %3").arg(radioTab._upload.index).arg(radioTab._upload.count).arg(radioTab._upload.name)
                            }
                            Rectangle {
                                Layout.fillWidth:       true
                                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 0.3
                                radius:                 height / 2
                                color:                  Qt.rgba(1, 1, 1, 0.12)

                                Rectangle {
                                    width:  parent.width * Math.min(1, radioTab._upload.progress || 0)
                                    height: parent.height
                                    radius: parent.radius
                                    color:  qgcPal.buttonHighlight
                                }
                            }
                        }

                        QGCButton {
                            text:       qsTr("Cancel")
                            visible:    radioTab._upload.active === true
                            onClicked:  RoomManager.cancelRadioUpload()
                        }

                        Item {
                            Layout.fillWidth:   true
                            visible:            radioTab._upload.active !== true
                        }

                        QGCLabel {
                            color:          qgcPal.colorGrey
                            font.pointSize: ScreenTools.smallFontPointSize
                            text:           qsTr("%1 MB of %2 MB").arg(radioTab._megabytes(radioTab._radio.bytes)).arg(radioTab._megabytes(radioTab._radio.maxBytes))
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorGrey
                        font.pointSize:     ScreenTools.smallFontPointSize
                        text:               radioTab._radio.convert
                                                ? qsTr("Audio files of any common format, or whole folders: drop them here or use Add tracks. The server converts them to MP3 128 kbit/s at the same loudness. Click a track for its actions, drag ⠿ to move it. Every operator hears the same moment of the radio.")
                                                : qsTr("Only MP3 files, or folders with them: ffmpeg is not installed on the server. Drop them here or use Add tracks. Click a track for its actions, drag ⠿ to move it. Every operator hears the same moment of the radio.")
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        wrapMode:           Text.WordWrap
                        color:              qgcPal.colorOrange
                        font.pointSize:     ScreenTools.smallFontPointSize
                        visible:            !radioTab._tools
                        text:               qsTr("Shuffle, playing a track next, renaming and the same loudness for every track need the new version of NX Hub on the VPS.")
                    }

                    // Tracks from before loudness leveling: leveled on the server on request
                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth
                        visible:            radioTab._tools && radioTab._radio.convert === true && (radioTab._leveling !== null || radioTab._unleveled > 0)

                        QGCLabel {
                            Layout.fillWidth:   true
                            wrapMode:           Text.WordWrap
                            text: radioTab._leveling
                                      ? qsTr("Leveling the loudness of the older tracks: %1 of %2").arg(radioTab._leveling.done).arg(radioTab._leveling.total)
                                      : qsTr("Tracks added before loudness leveling: %1. They may play louder or quieter than the others.").arg(radioTab._unleveled)
                        }
                        QGCButton {
                            text:       qsTr("Level loudness")
                            visible:    radioTab._leveling === null
                            onClicked:  RoomManager.levelRadioTracks()
                        }
                    }

                    // The playlist
                    QGCFlickable {
                        Layout.fillWidth:   true
                        Layout.fillHeight:  true
                        contentHeight:      trackColumn.height
                        clip:               true

                        Column {
                            id:         trackColumn
                            width:      parent.width
                            spacing:    _margins * 0.3

                            Repeater {
                                model: radioTab._tracks

                                Rectangle {
                                    id:             trackCard
                                    width:          trackColumn.width
                                    height:         trackCardColumn.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.6
                                    radius:         ScreenTools.defaultFontPixelHeight * 0.6
                                    color:          _playing ? root._selectedColor : ((_selected || cardMouse.containsMouse) ? Qt.rgba(1, 1, 1, 0.07) : root._cardColor)
                                    border.color:   (_playing || _selected) ? qgcPal.buttonHighlight : root._cardBorder
                                    z:              trackDrag.active ? 10 : 0
                                    opacity:        trackDrag.active ? 0.85 : 1
                                    transform:      Translate { y: trackDrag.active ? trackDrag.activeTranslation.y : 0 }

                                    readonly property bool _playing:    radioTab._now !== null && radioTab._now.trackId === modelData.trackId
                                    readonly property bool _selected:   radioTab._selectedTrack === modelData.trackId
                                    readonly property bool _renaming:   radioTab._renamingTrack === modelData.trackId
                                    /// Place in the queue of tracks asked to play next, -1 when not asked for
                                    readonly property int  _queued:     radioTab._queue.indexOf(modelData.trackId)

                                    // A click opens the track's actions below it, another one closes them
                                    MouseArea {
                                        id:             cardMouse
                                        anchors.fill:   parent
                                        hoverEnabled:   true
                                        onClicked: {
                                            radioTab._renamingTrack = ""
                                            radioTab._selectedTrack = trackCard._selected ? "" : modelData.trackId
                                        }
                                    }

                                    Column {
                                        id:                     trackCardColumn
                                        anchors.left:           parent.left
                                        anchors.right:          parent.right
                                        anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 0.8
                                        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 1.2
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing:                ScreenTools.defaultFontPixelHeight * 0.4

                                        RowLayout {
                                            width:      parent.width
                                            height:     Math.max(implicitHeight, ScreenTools.defaultFontPixelHeight * 2)
                                            spacing:    ScreenTools.defaultFontPixelWidth

                                            // Hold and drag up or down to move the track; the list order decides where it lands
                                            QGCLabel {
                                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 2.4
                                                horizontalAlignment:    Text.AlignHCenter
                                                text:                   "⠿"
                                                color:                  trackDrag.active ? qgcPal.buttonHighlight : qgcPal.colorGrey
                                                font.pointSize:         ScreenTools.mediumFontPointSize

                                                HoverHandler { cursorShape: Qt.SizeVerCursor }

                                                DragHandler {
                                                    id:                 trackDrag
                                                    target:             null
                                                    xAxis.enabled:      false
                                                    grabPermissions:    PointerHandler.CanTakeOverFromAnything

                                                    property real lastY: 0

                                                    onActiveTranslationChanged: if (active) lastY = activeTranslation.y
                                                    onActiveChanged: {
                                                        if (active) {
                                                            radioTab._selectedTrack = ""
                                                            radioTab._renamingTrack = ""
                                                            return
                                                        }
                                                        const steps = Math.round(lastY / (radioTab._rowHeight + trackColumn.spacing))
                                                        const target = Math.max(0, Math.min(radioTab._tracks.length - 1, index + steps))
                                                        lastY = 0
                                                        if (target !== index) {
                                                            RoomManager.moveRadioTrack(modelData.trackId, target)
                                                        }
                                                    }
                                                }
                                            }
                                            QGCLabel {
                                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 3
                                                text:                   index + 1
                                                color:                  qgcPal.colorGrey
                                            }
                                            QGCLabel {
                                                Layout.fillWidth:   true
                                                visible:            !trackCard._renaming
                                                elide:              Text.ElideRight
                                                text:               modelData.name
                                                font.weight:        trackCard._playing ? Font.ExtraBold : Font.Normal
                                            }
                                            QGCTextField {
                                                id:                 renameField
                                                Layout.fillWidth:   true
                                                visible:            trackCard._renaming

                                                function _begin() {
                                                    text = modelData.name
                                                    forceActiveFocus()
                                                    selectAll()
                                                }
                                                onVisibleChanged:       if (visible) _begin()
                                                Component.onCompleted:  if (visible) _begin()
                                                onAccepted:             radioTab._rename(modelData.trackId, text)
                                                Keys.onEscapePressed:   radioTab._renamingTrack = ""
                                            }
                                            // Asked to play next: the first of the queue, or its place in it
                                            Rectangle {
                                                visible:                trackCard._queued >= 0
                                                Layout.preferredWidth:  queueLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 1.6
                                                Layout.preferredHeight: queueLabel.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.25
                                                radius:                 height / 2
                                                color:                  Qt.rgba(219 / 255, 101 / 255, 54 / 255, 0.18)
                                                border.color:           "#DB6536"

                                                QGCLabel {
                                                    id:                 queueLabel
                                                    anchors.centerIn:   parent
                                                    font.pointSize:     ScreenTools.smallFontPointSize
                                                    text:               trackCard._queued === 0 ? qsTr("Next") : qsTr("In queue: %1").arg(trackCard._queued + 1)
                                                }
                                            }
                                            QGCLabel {
                                                text:   radioTab._duration(modelData.duration)
                                                color:  qgcPal.colorGrey
                                            }
                                        }

                                        // Actions of the opened track
                                        RowLayout {
                                            width:      parent.width
                                            visible:    trackCard._selected
                                            spacing:    ScreenTools.defaultFontPixelWidth

                                            Item { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 4.4 }

                                            QGCButton {
                                                text:       trackCard._queued >= 0 ? qsTr("Remove from queue") : qsTr("Play next")
                                                visible:    radioTab._tools && !trackCard._renaming
                                                onClicked: {
                                                    if (trackCard._queued >= 0) {
                                                        RoomManager.unqueueRadioTrack(modelData.trackId)
                                                    } else {
                                                        RoomManager.queueRadioTrack(modelData.trackId, false)
                                                    }
                                                }
                                            }
                                            QGCButton {
                                                text:       qsTr("Play now")
                                                visible:    radioTab._tools && !trackCard._renaming
                                                enabled:    radioTab._radio.onAir === true
                                                onClicked:  RoomManager.queueRadioTrack(modelData.trackId, true)
                                            }
                                            QGCButton {
                                                text:       trackCard._renaming ? qsTr("Save") : qsTr("Rename")
                                                primary:    trackCard._renaming
                                                visible:    radioTab._tools
                                                onClicked: {
                                                    if (trackCard._renaming) {
                                                        radioTab._rename(modelData.trackId, renameField.text)
                                                    } else {
                                                        radioTab._renamingTrack = modelData.trackId
                                                    }
                                                }
                                            }
                                            QGCButton {
                                                text:       qsTr("Cancel")
                                                visible:    trackCard._renaming
                                                onClicked:  radioTab._renamingTrack = ""
                                            }
                                            Item { Layout.fillWidth: true }
                                            QGCButton {
                                                text:       qsTr("Delete")
                                                textColor:  qgcPal.warningText
                                                visible:    !trackCard._renaming
                                                onClicked: {
                                                    const track = modelData
                                                    trackConfirmFactory.open({
                                                        title:      qsTr("Delete Track"),
                                                        message:    qsTr("Delete \"%1\" from the room's playlist?").arg(track.name),
                                                        action:     function () { RoomManager.deleteRadioTrack(track.trackId) }
                                                    })
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
                                visible:    radioTab._tracks.length === 0
                                text:       qsTr("No tracks yet. Add your own music, or drop audio files and folders here: the room plays it to its operators.")
                            }
                        }
                    }
                }

                // Audio files and folders dropped from Explorer are added to the playlist
                DropArea {
                    id:             trackDropArea
                    anchors.fill:   parent
                    enabled:        radioTab._supported

                    onEntered: (drag) => { drag.accepted = drag.hasUrls }
                    onDropped: (drop) => {
                        if (drop.hasUrls) {
                            RoomManager.uploadRadioTracks(drop.urls)
                            drop.acceptProposedAction()
                        }
                    }
                }

                Rectangle {
                    anchors.fill:   parent
                    visible:        trackDropArea.containsDrag
                    radius:         _margins
                    color:          Qt.rgba(219 / 255, 101 / 255, 54 / 255, 0.10)
                    border.color:   "#DB6536"
                    border.width:   2

                    QGCLabel {
                        anchors.centerIn:   parent
                        text:               qsTr("Drop to add the tracks")
                        font.weight:        Font.ExtraBold
                        font.pointSize:     ScreenTools.largeFontPointSize
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

            // Off: the operator keeps his place in the room but cannot connect. Turning it off asks first.
            QGCCheckBoxSlider {
                id:                 accessSwitch
                Layout.fillWidth:   true
                text:               qsTr("Access to the room")
                enabled:            _member && !_member.isMe
                checked:            _member ? _member.enabled : false
                onClicked: {
                    const member = _member
                    if (checked) {
                        RoomManager.setMemberEnabled(member.deviceId, true)
                        return
                    }
                    // Stays on until confirmed; the room state turns it off
                    accessSwitch.checked = Qt.binding(function () { return _member ? _member.enabled : false })
                    revokeDialogFactory.open({ member: member })
                }
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
                    text:       qsTr("Video settings…")
                    visible:    _member && !_member.isMe
                    onClicked:  videoDialogFactory.open({ member: _member })
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
                                    color:              modelData.hasLink && modelData.tunnelFound ? qgcPal.colorGrey : qgcPal.colorOrange
                                    text: {
                                        let parts = []
                                        // First, so the line never cuts it off: operators cannot connect
                                        if (!modelData.tunnelFound) parts.push(qsTr("tunnel not found on the server"))
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

    QGCPopupDialogFactory {
        id:                 revokeDialogFactory
        dialogComponent:    revokeDialogComponent
    }

    QGCPopupDialogFactory {
        id:                 videoDialogFactory
        dialogComponent:    videoDialogComponent
    }

    // Video settings sent to one operator: his NX Rover applies them once, and he may change them afterwards.
    // The editors start from what he uses now (or from this computer's settings when he never reported).
    Component {
        id: videoDialogComponent

        QGCPopupDialog {
            id:         videoDialog
            title:      qsTr("Video Settings for %1").arg(member ? member.name : "")
            buttons:    Dialog.NoButton

            property var    member:     null
            property var    _draft:     ({})
            property var    _invalid:   ({})
            property bool   _ready:     false
            property bool   _fromOperator: false

            readonly property var  _info:       RoomManager.memberVideo
            readonly property bool _loaded:     member !== null && _info.deviceId === member.deviceId && _info.loading !== true
            readonly property bool _waiting:    _loaded && _info.pending !== null && _info.pending !== undefined && !_info.applied

            function _time(seconds) {
                return Qt.formatTime(new Date(seconds * 1000), "HH:mm")
            }

            function _fill() {
                if (_ready || !_loaded || (_info.error !== undefined && _info.error !== "")) {
                    return
                }
                const reported = _info.reported
                _fromOperator = reported !== null && reported !== undefined && Object.keys(reported).length > 0
                _draft = Object.assign({}, _fromOperator ? reported : RoomManager.localVideoSettings())
                _ready = true
            }

            function _set(key, value) {
                const draft = Object.assign({}, _draft)
                draft[key] = value
                _draft = draft
            }

            function _setInvalid(key, invalid) {
                const list = Object.assign({}, _invalid)
                if (invalid) {
                    list[key] = true
                } else {
                    delete list[key]
                }
                _invalid = list
            }

            Component.onCompleted: {
                RoomManager.loadMemberVideo(member.deviceId)
                _fill()
            }

            Connections {
                target:                         RoomManager
                function onMemberVideoChanged() { videoDialog._fill() }
            }

            // While sent settings wait for the operator, the status follows them
            Timer {
                interval:       5000
                repeat:         true
                running:        videoDialog._waiting
                onTriggered:    RoomManager.loadMemberVideo(videoDialog.member.deviceId)
            }

            ColumnLayout {
                width:      ScreenTools.defaultFontPixelWidth * 62
                spacing:    ScreenTools.defaultFontPixelHeight / 2

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    color:              videoDialog._info.error ? qgcPal.colorRed : qgcPal.text
                    text: {
                        if (!videoDialog._loaded) {
                            return qsTr("Loading…")
                        }
                        if (videoDialog._info.error) {
                            return videoDialog._info.error
                        }
                        return videoDialog._fromOperator
                            ? qsTr("The settings %1 uses now. Change what you need and send: his NX Rover applies them once, and he can still change them himself.").arg(videoDialog.member.name)
                            : qsTr("%1's NX Rover has not reported its settings yet; these are yours. Change what you need and send: his NX Rover applies them once, and he can still change them himself.").arg(videoDialog.member.name)
                    }
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.WordWrap
                    visible:            videoDialog._loaded && videoDialog._info.pending !== null && videoDialog._info.pending !== undefined
                    color:              videoDialog._info.applied ? qgcPal.colorGreen : qgcPal.colorOrange
                    text:               videoDialog._info.applied
                                            ? qsTr("Last sent at %1, applied at %2.").arg(videoDialog._time(videoDialog._info.pendingAt)).arg(videoDialog._time(videoDialog._info.appliedAt))
                                            : qsTr("Last sent at %1, not applied yet: his NX Rover applies them within 15 seconds once it is online.").arg(videoDialog._time(videoDialog._info.pendingAt))
                }

                QGCFlickable {
                    Layout.fillWidth:       true
                    Layout.preferredHeight: Math.min(settingsColumn.height, ScreenTools.defaultFontPixelHeight * 24)
                    contentHeight:          settingsColumn.height
                    clip:                   true
                    visible:                videoDialog._ready

                    ColumnLayout {
                        id:         settingsColumn
                        width:      parent.width
                        spacing:    ScreenTools.defaultFontPixelHeight * 0.3

                        Repeater {
                            model: RoomManager.videoSettingKeys

                            Item {
                                id:                     settingRow
                                Layout.fillWidth:       true
                                Layout.preferredHeight: rowLoader.item ? rowLoader.item.implicitHeight : 0
                                visible:                fact !== null && fact !== undefined && videoDialog._draft[modelData] !== undefined

                                required property string modelData
                                readonly property var    fact:   QGroundControl.settingsManager.videoSettings[modelData]
                                readonly property var    value:  videoDialog._draft[modelData]

                                Loader {
                                    id:                 rowLoader
                                    width:              parent.width
                                    active:             settingRow.visible
                                    sourceComponent:    settingRow.fact && settingRow.fact.typeIsBool ? boolRow
                                                        : (settingRow.fact && settingRow.fact.enumStrings.length > 0 ? enumRow : numberRow)
                                }

                                Component {
                                    id: boolRow

                                    QGCCheckBoxSlider {
                                        text:       settingRow.fact.label
                                        checked:    settingRow.value === true
                                        onClicked:  videoDialog._set(settingRow.modelData, checked)
                                    }
                                }

                                Component {
                                    id: enumRow

                                    RowLayout {
                                        QGCLabel {
                                            Layout.fillWidth:   true
                                            text:               settingRow.fact.label
                                        }
                                        QGCComboBox {
                                            sizeToContents: true
                                            model:          settingRow.fact.enumStrings
                                            currentIndex:   settingRow.fact.enumValues.indexOf(settingRow.value)
                                            onActivated:    (index) => videoDialog._set(settingRow.modelData, settingRow.fact.enumValues[index])
                                        }
                                    }
                                }

                                Component {
                                    id: numberRow

                                    RowLayout {
                                        QGCLabel {
                                            Layout.fillWidth:   true
                                            text:               settingRow.fact.label
                                        }
                                        QGCTextField {
                                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 14
                                            text:                   settingRow.value !== undefined ? String(settingRow.value) : ""
                                            showUnits:              true
                                            unitsLabel:             settingRow.fact.units
                                            numericValuesOnly:      true
                                            validationError:        videoDialog._invalid[settingRow.modelData] === true
                                            onEditingFinished: {
                                                const number = Number(text.replace(",", "."))
                                                const fact = settingRow.fact
                                                const invalid = text.trim() === "" || isNaN(number)
                                                                || (!fact.minIsDefaultForType && number < fact.min)
                                                                || (!fact.maxIsDefaultForType && number > fact.max)
                                                videoDialog._setInvalid(settingRow.modelData, invalid)
                                                if (!invalid) {
                                                    videoDialog._set(settingRow.modelData, number)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        text:       qsTr("Use mine")
                        visible:    videoDialog._ready
                        onClicked: {
                            videoDialog._draft = Object.assign({}, RoomManager.localVideoSettings())
                            videoDialog._invalid = ({})
                        }
                    }
                    Item { Layout.fillWidth: true }
                    QGCButton {
                        text:       qsTr("Close")
                        onClicked:  videoDialog.close()
                    }
                    QGCButton {
                        text:               qsTr("Send")
                        enabled:            videoDialog._ready && Object.keys(videoDialog._invalid).length === 0
                        backgroundColor:    "#DB6536"
                        textColor:          "#0B1F1D"
                        fontWeight:         Font.DemiBold
                        onClicked:          RoomManager.sendMemberVideo(videoDialog.member.deviceId, videoDialog._draft)
                    }
                }
            }
        }
    }

    // Own buttons instead of OK/Cancel, so the choice reads plainly; Enter does nothing here
    Component {
        id: revokeDialogComponent

        QGCPopupDialog {
            id:         revokeDialog
            title:      qsTr("Room Access")
            buttons:    Dialog.NoButton

            property var member: null

            ColumnLayout {
                spacing: ScreenTools.defaultFontPixelHeight

                QGCLabel {
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 50
                    wrapMode:               Text.WordWrap
                    text:                   qsTr("Do you really want to take away access to the room from operator %1?").arg(revokeDialog.member ? revokeDialog.member.name : "")
                }

                RowLayout {
                    Layout.alignment:   Qt.AlignRight
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        text:       qsTr("Keep")
                        onClicked:  revokeDialog.close()
                    }
                    QGCButton {
                        text:               qsTr("Take away")
                        backgroundColor:    "#DB6536"
                        textColor:          "#0B1F1D"
                        fontWeight:         Font.DemiBold
                        onClicked: {
                            RoomManager.setMemberEnabled(revokeDialog.member.deviceId, false)
                            revokeDialog.close()
                        }
                    }
                }
            }
        }
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
