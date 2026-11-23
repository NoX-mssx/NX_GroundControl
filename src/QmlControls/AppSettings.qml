import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.AppSettings

Rectangle {
    id:     settingsView
    color:  qgcPal.window
    z:      QGroundControl.zOrderTopMost

    readonly property real _defaultTextHeight:  ScreenTools.defaultFontPixelHeight
    readonly property real _defaultTextWidth:   ScreenTools.defaultFontPixelWidth
    readonly property real _horizontalMargin:   _defaultTextWidth / 2
    readonly property real _verticalMargin:     _defaultTextHeight / 2

    property bool _first: true
    property bool _commingFromRIDSettings: false
    property int  _selectedPageIndex: -1
    property int  _selectedSectionIndex: -1
    property var  _expandedPages: ({})  // pageIndex -> bool
    property int  _expandedRevision: 0  // bumped to trigger re-evaluation
    property string _searchQuery: ""

    function _setExpanded(pageIndex, value) {
        _expandedPages[pageIndex] = value
        _expandedRevision++
    }

    function _isExpanded(pageIndex) {
        void _expandedRevision  // create binding dependency
        return !!_expandedPages[pageIndex]
    }

    function _pageSections(entry) {
        return entry && typeof entry.sections === "function" ? entry.sections() : []
    }

    function _pageAvailable(entry) {
        if (!entry || entry.name === "Divider" ||
                (typeof entry.pageVisible === "function" && !entry.pageVisible())) {
            return false
        }

        var sections = _pageSections(entry)
        return sections.length === 0 || sections.some(function(section) { return section.visible })
    }

    function _sectionAvailable(sections, sectionIndex) {
        if (sectionIndex === -1) return true
        for (var i = 0; i < sections.length; i++) {
            if (sections[i].index === sectionIndex) return sections[i].visible
        }
        return false
    }

    // A divider shows only between two available pages. When consecutive dividers have no
    // available page between them, only the first one shows.
    function _dividerVisible(pageIndex) {
        if (_searchQuery.trim() !== "") {
            return false
        }

        let hasPageBefore = false
        for (let i = pageIndex - 1; i >= 0; i--) {
            let entry = settingsPagesModel.get(i)
            if (entry.name === "Divider") {
                return false
            }
            if (_pageAvailable(entry)) {
                hasPageBefore = true
                break
            }
        }
        if (!hasPageBefore) {
            return false
        }

        for (let j = pageIndex + 1; j < settingsPagesModel.count; j++) {
            if (_pageAvailable(settingsPagesModel.get(j))) {
                return true
            }
        }
        return false
    }

    // Search: returns array of matching section indices for a page, or empty if no match
    function _matchingSections(pageIndex) {
        var query = _searchQuery.toLowerCase().trim()
        if (query === "") return []  // empty = no filtering

        var entry = settingsPagesModel.get(pageIndex)
        if (!_pageAvailable(entry)) return []

        var sections = _pageSections(entry)
        var matches = []
        for (var i = 0; i < sections.length; i++) {
            if (!sections[i].visible) continue
            for (var j = 0; j < sections[i].searchTerms.length; j++) {
                if (sections[i].searchTerms[j].indexOf(query) !== -1) {
                    matches.push(sections[i].index)
                    break
                }
            }
        }
        return matches
    }

    // Does this page have any search matches? (or is search empty = show all)
    function _pageMatchesSearch(pageIndex) {
        if (_searchQuery.trim() === "") return true
        return _matchingSections(pageIndex).length > 0
    }

    function _navigateTo(pageIndex, sectionIndex) {
        var entry = settingsPagesModel.get(pageIndex)
        if (!entry || entry.name === "Divider") return
        if (!_pageAvailable(entry)) return
        if (!_sectionAvailable(_pageSections(entry), sectionIndex)) return

        var url = entry.url
        _selectedSectionIndex = sectionIndex

        if (_selectedPageIndex !== pageIndex) {
            _selectedPageIndex = pageIndex
            rightPanel.source = url
        }

        // Apply section filter after the page is loaded
        if (rightPanel.item && typeof rightPanel.item.sectionFilter !== "undefined") {
            rightPanel.item.sectionFilter = sectionIndex
        }
    }

    function _navigateToFirstAvailablePage() {
        for (var i = 0; i < settingsPagesModel.count; i++) {
            if (_pageAvailable(settingsPagesModel.get(i))) {
                _navigateTo(i, -1)
                return
            }
        }

        _selectedPageIndex = -1
        _selectedSectionIndex = -1
        rightPanel.source = ""
    }

    // settingsPage is the untranslated page name from SettingsPages.json
    function showSettingsPage(settingsPage) {
        for (var i = 0; i < settingsPagesModel.count; i++) {
            var entry = settingsPagesModel.get(i)
            if (entry && entry.nameKey === settingsPage) {
                _navigateTo(i, _firstSectionIndex(entry))
                break
            }
        }
    }

    // This need to block click event leakage to underlying map.
    DeadMouseArea {
        anchors.fill: parent
    }

    QGCPalette { id: qgcPal }

    Component.onCompleted: {
        // Find and select the default page
        var targetUrl = globals.commingFromRIDIndicator
            ? "qrc:/qml/QGroundControl/AppSettings/RemoteIDSettings.qml"
            : "qrc:/qml/QGroundControl/AppSettings/RoverSettings.qml"
        globals.commingFromRIDIndicator = false

        for (var i = 0; i < settingsPagesModel.count; i++) {
            var entry = settingsPagesModel.get(i)
            if (entry && entry.url === targetUrl) {
                _navigateTo(i, _firstSectionIndex(entry))
                break
            }
        }

        if (_selectedPageIndex === -1) {
            _navigateToFirstAvailablePage()
        }
    }

    Connections {
        target: rightPanel
        function onLoaded() {
            if (rightPanel.item && typeof rightPanel.item.sectionFilter !== "undefined") {
                rightPanel.item.sectionFilter = _selectedSectionIndex
            }
        }
    }

    SettingsPagesModel { id: settingsPagesModel }

    // First visible section of a page, so a page with several sections opens on its first tab
    function _firstSectionIndex(entry) {
        var sections = _pageSections(entry).filter(function(section) { return section.visible })
        return sections.length > 1 ? sections[0].index : -1
    }

    function _openPage(pageIndex) {
        if (!mainWindow.allowViewSwitch()) {
            return
        }
        _navigateTo(pageIndex, _firstSectionIndex(settingsPagesModel.get(pageIndex)))
    }

    // Open or hidden sidebar, kept between runs
    Settings {
        id:                     viewState
        category:               "SettingsView"
        property bool sidebarOpen: true
    }

    readonly property real _sidebarWidth: _defaultTextWidth * 26

    /// What each sidebar entry is: "divider", or whether the page is available. The entries report it themselves,
    /// so a divider's visibility does not read the model from inside a delegate binding.
    property var _entryState: []

    function _reportEntry(index, state) {
        if (_entryState[index] !== state) {
            const states = _entryState.slice()
            states[index] = state
            _entryState = states
        }
    }

    // Same rule as _dividerVisible: only between two available pages, and only the first of several in a row
    function _sidebarDividerShown(index) {
        const states = _entryState
        let before = false
        for (let i = index - 1; i >= 0; i--) {
            if (states[i] === "divider") {
                return false
            }
            if (states[i] === true) {
                before = true
                break
            }
        }
        if (!before) {
            return false
        }
        for (let j = index + 1; j < states.length; j++) {
            if (states[j] === true) {
                return true
            }
        }
        return false
    }

    // ---- Pages in a sidebar on the left, which can be hidden ----
    Rectangle {
        id:                 sidebar
        anchors.left:       parent.left
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _defaultTextHeight
        width:              viewState.sidebarOpen ? _sidebarWidth : _defaultTextHeight * 2.3
        radius:             _defaultTextHeight * 1.1
        color:              Qt.rgba(1, 1, 1, 0.04)
        border.color:       Qt.rgba(1, 1, 1, 0.10)
        clip:               true

        Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

        QGCLabel {
            anchors.left:           parent.left
            anchors.right:          sidebarToggle.left
            anchors.leftMargin:     _defaultTextWidth * 2
            anchors.verticalCenter: sidebarToggle.verticalCenter
            visible:                viewState.sidebarOpen
            text:                   qsTr("Settings")
            elide:                  Text.ElideRight
            color:                  qgcPal.colorGrey
            font.weight:            Font.Bold
        }

        // Hides the sidebar; when hidden the same button brings it back
        Rectangle {
            id:                 sidebarToggle
            objectName:         "settings_sidebarToggle"
            anchors.top:        parent.top
            anchors.right:      parent.right
            anchors.margins:    _defaultTextHeight * 0.4
            width:              _defaultTextHeight * 1.5
            height:             width
            radius:             width / 2
            color:              toggleMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"

            QGCColoredImage {
                anchors.centerIn:   parent
                width:              _defaultTextHeight
                height:             width
                sourceSize.height:  height
                source:             viewState.sidebarOpen ? "qrc:/InstrumentValueIcons/cheveron-left.svg" : "qrc:/InstrumentValueIcons/menu.svg"
                color:              qgcPal.text
            }

            MouseArea {
                id:             toggleMouse
                anchors.fill:   parent
                hoverEnabled:   true
                onClicked:      viewState.sidebarOpen = !viewState.sidebarOpen
            }
        }

        QGCFlickable {
            id:                 pageList
            objectName:         "settings_buttonList"
            anchors.left:       parent.left
            anchors.right:      parent.right
            anchors.top:        sidebarToggle.bottom
            anchors.bottom:     parent.bottom
            anchors.margins:    _defaultTextHeight * 0.4
            contentHeight:      pageColumn.height
            visible:            viewState.sidebarOpen
            clip:               true

            Column {
                id:         pageColumn
                width:      pageList.width
                spacing:    _defaultTextHeight * 0.2

                Repeater {
                    id:     buttonRepeater
                    model:  settingsPagesModel

                    Item {
                        id:         pageButton
                        objectName: "settingsButton_" + (model.nameKey ?? pageName)
                        width:      pageColumn.width
                        height:     pageName === "Divider" ? _defaultTextHeight * 0.8 : _defaultTextHeight * 2.2
                        visible:    pageName === "Divider" ? settingsView._sidebarDividerShown(index) : pageAvailable

                        required property int index
                        required property var model

                        property string pageName:       model.name ?? ""
                        property string pageIconUrl:    model.iconUrl ?? ""
                        property var    pageVisible:    model.pageVisible ?? function() { return true }
                        property var    pageSections:   pageVisible() ? settingsView._pageSections(model) : []
                        property bool   pageAvailable:  pageName !== "Divider" && pageVisible() &&
                                                        (pageSections.length === 0 || pageSections.some(function(section) { return section.visible }))
                        property bool   isSelected:     settingsView._selectedPageIndex === index

                        // Make the setter usable as a clickable control for tests and keyboard users
                        function click() { settingsView._openPage(index) }

                        onPageAvailableChanged: {
                            settingsView._reportEntry(index, pageName === "Divider" ? "divider" : pageAvailable)
                            if (isSelected && !pageAvailable) {
                                settingsView._navigateToFirstAvailablePage()
                            }
                        }
                        Component.onCompleted: settingsView._reportEntry(index, pageName === "Divider" ? "divider" : pageAvailable)

                        Rectangle {
                            anchors.left:           parent.left
                            anchors.right:          parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            height:                 1
                            color:                  Qt.rgba(1, 1, 1, 0.10)
                            visible:                pageButton.pageName === "Divider"
                        }

                        Rectangle {
                            anchors.fill:   parent
                            visible:        pageButton.pageName !== "Divider"
                            radius:         height / 2
                            color:          pageButton.isSelected ? qgcPal.buttonHighlight
                                                                  : (pageMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent")

                            Row {
                                anchors.left:           parent.left
                                anchors.right:          parent.right
                                anchors.leftMargin:     _defaultTextWidth * 1.5
                                anchors.rightMargin:    _defaultTextWidth
                                anchors.verticalCenter: parent.verticalCenter
                                spacing:                _defaultTextWidth * 0.9

                                QGCColoredImage {
                                    id:                     pageIcon
                                    anchors.verticalCenter: parent.verticalCenter
                                    width:                  _defaultTextHeight
                                    height:                 width
                                    sourceSize.height:      height
                                    source:                 pageButton.pageIconUrl
                                    color:                  pageButton.isSelected ? qgcPal.buttonHighlightText : qgcPal.text
                                    visible:                pageButton.pageIconUrl !== ""
                                }
                                QGCLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width:                  parent.width - (pageIcon.visible ? pageIcon.width + parent.spacing : 0)
                                    text:                   pageButton.pageName
                                    elide:                  Text.ElideRight
                                    color:                  pageButton.isSelected ? qgcPal.buttonHighlightText : qgcPal.text
                                    font.weight:            Font.Bold
                                }
                            }

                            MouseArea {
                                id:             pageMouse
                                anchors.fill:   parent
                                hoverEnabled:   true
                                onClicked:      settingsView._openPage(pageButton.index)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- The selected page in a card, with its sections as tabs ----
    Rectangle {
        id:                 pageCard
        anchors.left:       sidebar.right
        anchors.right:      parent.right
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _defaultTextHeight
        radius:             _defaultTextHeight * 1.1
        color:              Qt.rgba(1, 1, 1, 0.04)
        border.color:       Qt.rgba(1, 1, 1, 0.10)

        readonly property var _entry:           settingsView._selectedPageIndex >= 0 ? settingsPagesModel.get(settingsView._selectedPageIndex) : null
        readonly property var _visibleSections: _entry ? settingsView._pageSections(_entry).filter(function(section) { return section.visible }) : []

        Item {
            id:                 sectionTabs
            anchors.left:       parent.left
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.leftMargin: _defaultTextWidth * 3
            anchors.rightMargin: _defaultTextWidth * 3
            height:             visible ? _defaultTextHeight * 2.8 : 0
            visible:            pageCard._visibleSections.length > 1

            Row {
                anchors.horizontalCenter:   parent.horizontalCenter
                anchors.bottom:             parent.bottom
                height:                     parent.height
                spacing:                    _defaultTextWidth * 3

                Repeater {
                    model: pageCard._visibleSections

                    Item {
                        id:         sectionTab
                        width:      sectionLabel.implicitWidth
                        height:     parent.height

                        required property var modelData

                        readonly property bool _checked: settingsView._selectedSectionIndex === modelData.index

                        QGCLabel {
                            id:                     sectionLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text:                   sectionTab.modelData.name
                            font.weight:            Font.Bold
                            color:                  sectionTab._checked ? qgcPal.text : qgcPal.colorGrey
                        }
                        Rectangle {
                            anchors.left:   parent.left
                            anchors.right:  parent.right
                            anchors.bottom: parent.bottom
                            height:         2
                            color:          qgcPal.buttonHighlight
                            visible:        sectionTab._checked
                        }
                        MouseArea {
                            anchors.fill:   parent
                            onClicked: {
                                if (mainWindow.allowViewSwitch()) {
                                    settingsView._navigateTo(settingsView._selectedPageIndex, sectionTab.modelData.index)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.left:   parent.left
                anchors.right:  parent.right
                anchors.bottom: parent.bottom
                height:         1
                color:          Qt.rgba(1, 1, 1, 0.08)
            }
        }

        //-- Panel Contents
        Loader {
            id:                     rightPanel
            objectName:             "settings_rightPanel"
            anchors.left:           parent.left
            anchors.right:          parent.right
            anchors.top:            sectionTabs.bottom
            anchors.bottom:         parent.bottom
            anchors.margins:        _defaultTextHeight * 0.6
        }
    }
}
