import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Table of the current room's vehicles, kept on the room server: date, drone name and number, Starlink number and
/// activation date, notes, and a tag (garage, working, broken, written off). The admin may always edit it and opens
/// it to operators in Access, for viewing or for editing. A cell is saved when it loses focus or on Enter.
Item {
    id: root

    readonly property var   _table:     RoomManager.table
    readonly property bool  _canEdit:   _table.canEdit === true
    readonly property real  _cw:        ScreenTools.defaultFontPixelWidth
    readonly property real  _lh:        ScreenTools.defaultFontPixelHeight
    readonly property real  _rowHeight: _lh * 2.1
    readonly property color _cardBorder: Qt.rgba(1, 1, 1, 0.10)

    readonly property var _tags: [
        { key: "garage",  label: qsTr("Garage"),      color: "#5B8DEF" },
        { key: "working", label: qsTr("Working"),     color: "#3CB371" },
        { key: "broken",  label: qsTr("Broken"),      color: "#DB6536" },
        { key: "retired", label: qsTr("Written off"), color: "#8A8F98" }
    ]

    // Columns before the tag; notes take the room left
    readonly property var _columns: [
        { key: "date",              title: qsTr("Date"),               width: _cw * 12, date: true },
        { key: "droneName",         title: qsTr("Drone"),              width: _cw * 18, date: false },
        { key: "droneNumber",       title: qsTr("Drone No."),          width: _cw * 10, date: false },
        { key: "starlinkNumber",    title: qsTr("Starlink No."),       width: _cw * 17, date: false },
        { key: "starlinkActivated", title: qsTr("Starlink activated"), width: _cw * 14, date: true },
        { key: "notes",             title: qsTr("Notes"),              width: 0,        date: false }
    ]
    readonly property real _tagWidth:       _cw * 15
    readonly property real _deleteWidth:    _lh * 1.8
    readonly property real _notesMinWidth:  _cw * 24
    readonly property real _fixedWidth: {
        let width = _tagWidth + _deleteWidth
        for (let i = 0; i < _columns.length; i++) {
            width += _columns[i].width
        }
        return width
    }

    /// Tags shown; empty: every row
    property var    _filter:        []
    /// Row whose cell is being edited: its values are not replaced from the server meanwhile
    property string _editingRow:    ""
    property bool   _syncPending:   false
    /// Bumped after each sync, for the counts (a changed tag does not change the number of rows)
    property int    _revision:      0

    function _tag(key) {
        for (let i = 0; i < _tags.length; i++) {
            if (_tags[i].key === key) {
                return _tags[i]
            }
        }
        return null
    }

    function _shown(tag) {
        return _filter.length === 0 || _filter.indexOf(tag) >= 0
    }

    function _toggleFilter(key) {
        const filter = _filter.slice()
        const at = filter.indexOf(key)
        if (at >= 0) {
            filter.splice(at, 1)
        } else {
            filter.push(key)
        }
        _filter = filter
    }

    function _shownCount() {
        let count = 0
        for (let i = 0; i < rowModel.count; i++) {
            if (_shown(rowModel.get(i).tag)) {
                count++
            }
        }
        return count
    }

    function _time(seconds) {
        return seconds > 0 ? new Date(seconds * 1000).toLocaleString(Qt.locale(), "dd.MM.yyyy HH:mm") : ""
    }

    /// Keeps the rows on screen in step with the server without rebuilding them, so a cell keeps its focus
    function _sync() {
        if (_editingRow !== "") {
            _syncPending = true
            return
        }
        _syncPending = false
        const rows = _table.rows || []
        const ids = rows.map(row => row.rowId)
        for (let i = rowModel.count - 1; i >= 0; i--) {
            if (ids.indexOf(rowModel.get(i).rowId) < 0) {
                rowModel.remove(i)
            }
        }
        for (let i = 0; i < rows.length; i++) {
            let at = -1
            for (let j = i; j < rowModel.count; j++) {
                if (rowModel.get(j).rowId === rows[i].rowId) {
                    at = j
                    break
                }
            }
            if (at < 0) {
                rowModel.insert(i, rows[i])
                continue
            }
            if (at !== i) {
                rowModel.move(at, i, 1)
            }
            rowModel.set(i, rows[i])
        }
        _revision++
    }

    function _setEditing(rowId, editing) {
        if (editing) {
            _editingRow = rowId
        } else if (_editingRow === rowId) {
            _editingRow = ""
            if (_syncPending) {
                Qt.callLater(_sync)
            }
        }
    }

    Component.onCompleted: {
        _sync()
        RoomManager.refreshTable()
    }

    Connections {
        target:                 RoomManager
        function onTableChanged() { root._sync() }
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ListModel { id: rowModel }

    // A cell: text that turns into an input on click when the table may be edited. Dates are shown as dd.mm.yyyy
    // and kept as yyyy-mm-dd; a date that cannot be read puts the old value back.
    component TableCell: Item {
        id: cell

        property string value
        property bool   editable
        property bool   isDate
        property color  textColor
        property color  focusColor
        signal committed(string newValue)
        signal editing(bool active)

        function display(text) {
            if (!isDate) {
                return text
            }
            const parts = /^(\d{4})-(\d{2})-(\d{2})$/.exec(text)
            return parts ? parts[3] + "." + parts[2] + "." + parts[1] : text
        }

        // yyyy-mm-dd, "" for an empty field, null when it is not a date
        function parse(text) {
            const trimmed = text.trim()
            if (!isDate || trimmed === "") {
                return trimmed
            }
            let parts = /^(\d{1,2})[.\/-](\d{1,2})[.\/-](\d{2}|\d{4})$/.exec(trimmed)
            let day, month, year
            if (parts) {
                day = Number(parts[1]); month = Number(parts[2]); year = Number(parts[3])
                if (year < 100) {
                    year += 2000
                }
            } else {
                parts = /^(\d{4})-(\d{1,2})-(\d{1,2})$/.exec(trimmed)
                if (!parts) {
                    return null
                }
                year = Number(parts[1]); month = Number(parts[2]); day = Number(parts[3])
            }
            const date = new Date(year, month - 1, day)
            if (date.getFullYear() !== year || date.getMonth() !== month - 1 || date.getDate() !== day) {
                return null
            }
            return year + "-" + String(month).padStart(2, "0") + "-" + String(day).padStart(2, "0")
        }

        QGCLabel {
            anchors.fill:       parent
            leftPadding:        ScreenTools.defaultFontPixelWidth * 0.8
            rightPadding:       ScreenTools.defaultFontPixelWidth * 0.4
            verticalAlignment:  Text.AlignVCenter
            visible:            !cell.editable
            text:               cell.display(cell.value)
            elide:              Text.ElideRight
            color:              cell.textColor
        }

        TextField {
            id:                 field
            anchors.fill:       parent
            anchors.margins:    2
            visible:            cell.editable
            text:               cell.display(cell.value)
            placeholderText:    cell.isDate && activeFocus ? qsTr("dd.mm.yyyy") : ""
            color:              cell.textColor
            placeholderTextColor: Qt.rgba(1, 1, 1, 0.25)
            font.pointSize:     ScreenTools.defaultFontPointSize
            font.family:        ScreenTools.normalFontFamily
            selectByMouse:      true
            leftPadding:        ScreenTools.defaultFontPixelWidth * 0.6
            rightPadding:       ScreenTools.defaultFontPixelWidth * 0.4
            verticalAlignment:  TextInput.AlignVCenter

            background: Rectangle {
                radius:         ScreenTools.defaultFontPixelHeight * 0.3
                color:          field.activeFocus ? Qt.rgba(1, 1, 1, 0.08) : (fieldHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
                border.width:   field.activeFocus ? 1 : 0
                border.color:   cell.focusColor
            }

            HoverHandler { id: fieldHover }

            onActiveFocusChanged: cell.editing(activeFocus)
            onEditingFinished: {
                const parsed = cell.parse(text)
                if (parsed === null) {
                    text = cell.display(cell.value)
                    return
                }
                if (parsed !== cell.value) {
                    cell.committed(parsed)
                }
                text = Qt.binding(function () { return cell.display(cell.value) })
            }
            Keys.onEscapePressed: {
                text = Qt.binding(function () { return cell.display(cell.value) })
                focus = false
            }
        }
    }

    ColumnLayout {
        anchors.fill:   parent
        spacing:        _lh * 0.6

        // ---- Filter, count and new row ----
        RowLayout {
            Layout.fillWidth:   true
            spacing:            _cw

            QGCLabel {
                text:           qsTr("Tags")
                color:          qgcPal.colorGrey
            }

            Repeater {
                model: root._tags

                Rectangle {
                    id:                     chip
                    Layout.preferredHeight: _lh * 1.7
                    Layout.preferredWidth:  chipLabel.implicitWidth + _cw * 3
                    radius:                 height / 2
                    color:                  _on ? Qt.rgba(chipColor.r, chipColor.g, chipColor.b, 0.22) : (chipMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
                    border.color:           _on ? chipColor : root._cardBorder

                    required property var modelData
                    readonly property color chipColor:  modelData.color
                    readonly property bool  _on:        root._filter.indexOf(modelData.key) >= 0

                    QGCLabel {
                        id:                 chipLabel
                        anchors.centerIn:   parent
                        text:               chip.modelData.label
                        color:              chip._on ? qgcPal.text : qgcPal.colorGrey
                        font.weight:        chip._on ? Font.Bold : Font.Normal
                    }
                    MouseArea {
                        id:             chipMouse
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      root._toggleFilter(chip.modelData.key)
                    }
                }
            }

            QGCButton {
                text:       qsTr("All")
                visible:    root._filter.length > 0
                onClicked:  root._filter = []
            }

            QGCLabel {
                Layout.fillWidth:   true
                horizontalAlignment: Text.AlignRight
                color:              qgcPal.colorGrey
                text: {
                    void root._revision
                    void root._filter
                    return root._filter.length > 0 ? qsTr("%1 of %2 rows").arg(root._shownCount()).arg(rowModel.count)
                                                   : qsTr("%1 rows").arg(rowModel.count)
                }
            }

            QGCButton {
                text:       qsTr("Add row")
                primary:    true
                visible:    root._canEdit
                onClicked:  RoomManager.addTableRow()
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            root._table.canView === true && !root._canEdit
            wrapMode:           Text.WordWrap
            color:              qgcPal.colorGrey
            font.pointSize:     ScreenTools.smallFontPointSize
            text:               qsTr("View only: the admin has not let you edit the table.")
        }

        // ---- The table ----
        Rectangle {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            radius:             _lh * 0.8
            color:              Qt.rgba(1, 1, 1, 0.03)
            border.color:       root._cardBorder
            clip:               true

            QGCFlickable {
                id:                 tableFlick
                anchors.fill:       parent
                anchors.margins:    _lh * 0.4
                contentWidth:       tableColumn.width
                contentHeight:      tableColumn.height
                flickableDirection: Flickable.AutoFlickIfNeeded
                visible:            root._table.loaded === true && root._table.supported === true && root._table.canView === true

                Column {
                    id:     tableColumn
                    width:  Math.max(tableFlick.width, root._fixedWidth + root._notesMinWidth)

                    // Header; long titles take two lines
                    Row {
                        height: _lh * 2.6

                        Repeater {
                            model: root._columns

                            QGCLabel {
                                required property var modelData
                                width:              modelData.width > 0 ? modelData.width : tableColumn.width - root._fixedWidth
                                height:             parent.height
                                leftPadding:        _cw * 0.8
                                rightPadding:       _cw * 0.4
                                verticalAlignment:  Text.AlignVCenter
                                text:               modelData.title
                                wrapMode:           Text.WordWrap
                                maximumLineCount:   2
                                elide:              Text.ElideRight
                                color:              qgcPal.colorGrey
                                font.weight:        Font.Bold
                            }
                        }
                        QGCLabel {
                            width:              root._tagWidth
                            height:             parent.height
                            leftPadding:        _cw * 0.8
                            verticalAlignment:  Text.AlignVCenter
                            text:               qsTr("Tag")
                            color:              qgcPal.colorGrey
                            font.weight:        Font.Bold
                        }
                    }

                    Rectangle {
                        width:  tableColumn.width
                        height: 1
                        color:  root._cardBorder
                    }

                    Repeater {
                        model: rowModel

                        Rectangle {
                            id:         rowItem
                            width:      tableColumn.width
                            height:     visible ? root._rowHeight : 0
                            visible:    root._shown(model.tag)
                            color:      rowHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : (index % 2 ? Qt.rgba(1, 1, 1, 0.025) : "transparent")

                            required property int index
                            required property var model

                            readonly property var _tagInfo: root._tag(model.tag)

                            HoverHandler { id: rowHover }

                            ToolTip.visible:    rowHover.hovered && model.updatedBy !== "" && root._editingRow === ""
                            ToolTip.delay:      1200
                            ToolTip.text:       qsTr("Changed by %1, %2").arg(model.updatedBy).arg(root._time(model.updatedAt))

                            Row {
                                anchors.fill: parent

                                Repeater {
                                    model: root._columns

                                    TableCell {
                                        required property var modelData
                                        width:      modelData.width > 0 ? modelData.width : tableColumn.width - root._fixedWidth
                                        height:     rowItem.height
                                        value:      rowItem.model[modelData.key] ?? ""
                                        editable:   root._canEdit
                                        isDate:     modelData.date
                                        textColor:  qgcPal.text
                                        focusColor: qgcPal.buttonHighlight
                                        onEditing: (active) => root._setEditing(rowItem.model.rowId, active)
                                        onCommitted: (newValue) => {
                                            const values = {}
                                            values[modelData.key] = newValue
                                            RoomManager.updateTableRow(rowItem.model.rowId, values)
                                        }
                                    }
                                }

                                // Tag: a coloured pill; a click picks another one
                                Item {
                                    width:  root._tagWidth
                                    height: rowItem.height

                                    Rectangle {
                                        anchors.left:           parent.left
                                        anchors.leftMargin:     _cw * 0.6
                                        anchors.verticalCenter: parent.verticalCenter
                                        height:                 _lh * 1.5
                                        width:                  Math.min(parent.width - _cw * 1.2, tagLabel.implicitWidth + _cw * 2.4)
                                        radius:                 height / 2
                                        color:                  rowItem._tagInfo ? Qt.rgba(tagColor.r, tagColor.g, tagColor.b, 0.22) : "transparent"
                                        border.color:           rowItem._tagInfo ? tagColor : (root._canEdit ? root._cardBorder : "transparent")

                                        readonly property color tagColor: rowItem._tagInfo ? rowItem._tagInfo.color : "transparent"

                                        QGCLabel {
                                            id:                 tagLabel
                                            anchors.centerIn:   parent
                                            text:               rowItem._tagInfo ? rowItem._tagInfo.label : (root._canEdit ? qsTr("Set tag") : "")
                                            color:              rowItem._tagInfo ? qgcPal.text : qgcPal.colorGrey
                                            font.pointSize:     ScreenTools.smallFontPointSize
                                            font.weight:        rowItem._tagInfo ? Font.Bold : Font.Normal
                                        }
                                        MouseArea {
                                            anchors.fill:   parent
                                            enabled:        root._canEdit
                                            cursorShape:    Qt.PointingHandCursor
                                            onClicked:      tagMenu.popup()
                                        }
                                    }

                                    QGCMenu {
                                        id: tagMenu

                                        function pick(key) {
                                            if (rowItem.model.tag !== key) {
                                                RoomManager.updateTableRow(rowItem.model.rowId, { tag: key })
                                            }
                                        }

                                        QGCMenuItem { text: root._tags[0].label; onTriggered: tagMenu.pick(root._tags[0].key) }
                                        QGCMenuItem { text: root._tags[1].label; onTriggered: tagMenu.pick(root._tags[1].key) }
                                        QGCMenuItem { text: root._tags[2].label; onTriggered: tagMenu.pick(root._tags[2].key) }
                                        QGCMenuItem { text: root._tags[3].label; onTriggered: tagMenu.pick(root._tags[3].key) }
                                        QGCMenuSeparator { }
                                        QGCMenuItem {
                                            text:           qsTr("No tag")
                                            enabled:        rowItem.model.tag !== ""
                                            onTriggered:    RoomManager.updateTableRow(rowItem.model.rowId, { tag: "" })
                                        }
                                    }
                                }

                                // Delete, after a question
                                Item {
                                    width:  root._deleteWidth
                                    height: rowItem.height

                                    QGCColoredImage {
                                        anchors.centerIn:   parent
                                        width:              _lh * 0.9
                                        height:             width
                                        sourceSize.height:  height
                                        source:             "/res/TrashDelete.svg"
                                        color:              deleteMouse.containsMouse ? qgcPal.colorRed : qgcPal.colorGrey
                                        visible:            root._canEdit && (rowHover.hovered || deleteMouse.containsMouse)
                                    }
                                    MouseArea {
                                        id:             deleteMouse
                                        anchors.fill:   parent
                                        enabled:        root._canEdit
                                        hoverEnabled:   true
                                        onClicked: {
                                            const rowId = rowItem.model.rowId
                                            const name = rowItem.model.droneName !== "" ? rowItem.model.droneName : qsTr("without a name")
                                            QGroundControl.showMessageDialog(
                                                root,
                                                qsTr("Delete Row"),
                                                qsTr("Delete the row of drone \"%1\"? Everyone in the room loses it.").arg(name),
                                                Dialog.Ok | Dialog.Cancel,
                                                function () { RoomManager.deleteTableRow(rowId) })
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                anchors.bottom: parent.bottom
                                height:         1
                                color:          Qt.rgba(1, 1, 1, 0.05)
                            }
                        }
                    }

                    QGCLabel {
                        width:                  tableFlick.width
                        topPadding:             _lh
                        horizontalAlignment:    Text.AlignHCenter
                        wrapMode:               Text.WordWrap
                        color:                  qgcPal.colorGrey
                        visible:                { void root._revision; return rowModel.count === 0 || root._shownCount() === 0 }
                        text: {
                            void root._revision
                            if (rowModel.count === 0) {
                                return root._canEdit ? qsTr("No rows yet. \"Add row\" starts the table.") : qsTr("The table is empty.")
                            }
                            return qsTr("No rows with these tags.")
                        }
                    }
                }
            }

            // Not loaded, a server without tables, or no access
            QGCLabel {
                anchors.centerIn:       parent
                width:                  Math.min(parent.width - _lh * 4, _cw * 70)
                visible:                !tableFlick.visible
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                color:                  qgcPal.colorGrey
                text: {
                    if (root._table.loaded !== true) {
                        return qsTr("Loading the table…")
                    }
                    if (root._table.supported !== true) {
                        return qsTr("The room server keeps no table yet: it has to be updated first.")
                    }
                    return qsTr("The admin has not opened the table to you.")
                }
            }
        }
    }
}
