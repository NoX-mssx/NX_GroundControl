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
            : "qrc:/qml/QGroundControl/AppSettings/GeneralSettings.qml"
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

    // ---- Pages as chips across the top ----
    Flow {
        id:                 pageChips
        objectName:         "settings_buttonList"
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        anchors.margins:    _defaultTextHeight
        spacing:            _defaultTextWidth * 0.8

        Repeater {
            id:     buttonRepeater
            model:  settingsPagesModel

            Rectangle {
                id:         pageChip
                objectName: "settingsButton_" + (model.nameKey ?? pageName)
                height:     _defaultTextHeight * 2.3
                width:      chipRow.implicitWidth + _defaultTextWidth * 3
                radius:     height / 2
                visible:    pageName !== "Divider" && pageAvailable
                color:      isSelected ? qgcPal.buttonHighlight : (chipMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))

                required property int index
                required property var model

                property string pageName:       model.name ?? ""
                property string pageIconUrl:    model.iconUrl ?? ""
                property var    pageVisible:    model.pageVisible ?? function() { return true }
                property var    pageSections:   pageVisible() ? settingsView._pageSections(model) : []
                property bool   pageAvailable:  pageVisible() &&
                                                (pageSections.length === 0 || pageSections.some(function(section) { return section.visible }))
                property bool   isSelected:     settingsView._selectedPageIndex === index

                // Make the setter usable as a clickable control for tests and keyboard users
                function click() { settingsView._openPage(index) }

                onPageAvailableChanged: {
                    if (isSelected && !pageAvailable) {
                        settingsView._navigateToFirstAvailablePage()
                    }
                }

                Row {
                    id:                 chipRow
                    anchors.centerIn:   parent
                    spacing:            _defaultTextWidth * 0.7

                    QGCColoredImage {
                        anchors.verticalCenter: parent.verticalCenter
                        width:                  _defaultTextHeight
                        height:                 width
                        sourceSize.height:      height
                        source:                 pageChip.pageIconUrl
                        color:                  pageChip.isSelected ? qgcPal.buttonHighlightText : qgcPal.text
                        visible:                pageChip.pageIconUrl !== ""
                    }
                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text:                   pageChip.pageName
                        color:                  pageChip.isSelected ? qgcPal.buttonHighlightText : qgcPal.text
                        font.weight:            Font.Bold
                    }
                }

                MouseArea {
                    id:             chipMouse
                    anchors.fill:   parent
                    hoverEnabled:   true
                    onClicked:      settingsView._openPage(pageChip.index)
                }
            }
        }
    }

    // ---- The selected page in a card, with its sections as tabs ----
    Rectangle {
        id:                 pageCard
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        pageChips.bottom
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
                anchors.left:   parent.left
                anchors.bottom: parent.bottom
                height:         parent.height
                spacing:        _defaultTextWidth * 3

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
