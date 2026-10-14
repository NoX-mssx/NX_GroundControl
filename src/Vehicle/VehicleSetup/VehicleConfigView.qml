import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:             vehicleConfigView
    objectName:     "vehicleConfig_root"
    color:          qgcPal.window
    z:      QGroundControl.zOrderTopMost

    // This need to block click event leakage to underlying map.
    DeadMouseArea {
        anchors.fill: parent
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    readonly property real      _defaultTextHeight: ScreenTools.defaultFontPixelHeight
    readonly property real      _defaultTextWidth:  ScreenTools.defaultFontPixelWidth
    readonly property real      _horizontalMargin:  _defaultTextWidth / 2
    readonly property real      _verticalMargin:    _defaultTextHeight / 2
    readonly property real      _buttonWidth:       _defaultTextWidth * 18
    readonly property string    _armedVehicleText:  qsTr("This operation cannot be performed while the vehicle is armed.")

    property var    _activeVehicle:                 QGroundControl.multiVehicleManager.activeVehicle
    property bool   _vehicleArmed:                  _activeVehicle ? _activeVehicle.armed : false
    property string _messagePanelText:              qsTr("missing message panel text")
    property bool   _fullParameterVehicleAvailable: _activeVehicle && QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable && !_activeVehicle.parameterManager.missingParameters
    property var    _corePlugin:                    QGroundControl.corePlugin

    // Tree view state
    property int    _selectedComponentIndex: -1     // -1 = summary or special button
    property int    _selectedSectionIndex:   -1
    property string _selectedSpecial:        ""     // "summary", "parameters", "firmware", "opticalflow"
    property bool   _showingPrereqMessage:   false  // Panel area shows prerequisite message instead of selected component
    property var    _expandedComponents:     ({})
    property int    _expandedRevision:       0
    property string _searchQuery:            ""

    function _setExpanded(compIndex, value) {
        _expandedComponents[compIndex] = value
        _expandedRevision++
    }

    function _isExpanded(compIndex) {
        void _expandedRevision
        return !!_expandedComponents[compIndex]
    }

    /// Translated display name for a section ID. JSON-driven components translate via the JSON
    /// filename context; hand-coded components provide sectionDisplayName().
    function _sectionDisplayName(component, sectionId) {
        let context = _translationContext(component)
        if (context) {
            return qsTranslate(context, sectionId)
        }
        if (component && typeof component.sectionDisplayName === "function") {
            return component.sectionDisplayName(sectionId)
        }
        return sectionId
    }

    /// Get the section ID for a sidebar entry.
    function _sectionId(compIndex, sectionIndex) {
        if (sectionIndex < 0 || !_fullParameterVehicleAvailable) return ""
        var components = _activeVehicle.autopilotPlugin.vehicleComponents
        if (compIndex < 0 || compIndex >= components.length) return ""
        var secs = components[compIndex].sectionIds
        if (sectionIndex < secs.length) return secs[sectionIndex]
        return ""
    }

    /// Extract the translation context (JSON filename) from a component.
    function _translationContext(component) {
        if (!component || !component.vehicleConfigJson) return ""
        var path = component.vehicleConfigJson.toString()
        var slash = path.lastIndexOf("/")
        return slash >= 0 ? path.substring(slash + 1) : path
    }

    function _componentMatchesSearch(component) {
        if (_searchQuery.trim() === "") return true
        var query = _searchQuery.toLowerCase().trim()
        if (component.name.toLowerCase().indexOf(query) !== -1) return true
        var context = _translationContext(component)
        var secs = component.sectionIds
        if (secs) {
            for (var i = 0; i < secs.length; i++) {
                if (secs[i].toLowerCase().indexOf(query) !== -1) return true
                let displayName = _sectionDisplayName(component, secs[i])
                if (displayName !== secs[i] && displayName.toLowerCase().indexOf(query) !== -1) {
                    return true
                }
            }
        }
        var keywords = component.sectionKeywords
        if (keywords) {
            for (var key in keywords) {
                var terms = keywords[key]
                for (var j = 0; j < terms.length; j++) {
                    if (terms[j].toLowerCase().indexOf(query) !== -1) return true
                    if (context && qsTranslate(context, terms[j]).toLowerCase().indexOf(query) !== -1) return true
                }
            }
        }
        return false
    }

    function _sectionMatchesSearch(component, sectionId) {
        if (_searchQuery.trim() === "") return true
        var query = _searchQuery.toLowerCase().trim()
        if (sectionId.toLowerCase().indexOf(query) !== -1) return true
        var context = _translationContext(component)
        let displayName = _sectionDisplayName(component, sectionId)
        if (displayName !== sectionId && displayName.toLowerCase().indexOf(query) !== -1) {
            return true
        }
        var keywords = component.sectionKeywords
        if (keywords && keywords[sectionId]) {
            var terms = keywords[sectionId]
            for (var i = 0; i < terms.length; i++) {
                if (terms[i].toLowerCase().indexOf(query) !== -1) return true
                if (context && qsTranslate(context, terms[i]).toLowerCase().indexOf(query) !== -1) return true
            }
        }
        return false
    }

    function _componentVisible(component) {
        if (!component || component.setupSource.toString() === "") {
            return false
        }
        return _searchQuery.trim() === "" || _componentMatchesSearch(component)
    }

    function _anyComponentVisible() {
        if (!_fullParameterVehicleAvailable) {
            return false
        }
        return _activeVehicle.autopilotPlugin.vehicleComponents.some(_componentVisible)
    }

    function showSummaryPanel() {
        if (mainWindow.allowViewSwitch()) {
            _showSummaryPanel()
        }
    }

    function _showSummaryPanel() {
        _selectedSpecial = "summary"
        _selectedComponentIndex = -1
        _selectedSectionIndex = -1
        if (_fullParameterVehicleAvailable) {
            if (_activeVehicle.autopilotPlugin.vehicleComponents.length === 0) {
                panelLoader.setSourceComponent(noComponentsVehicleSummaryComponent)
            } else {
                panelLoader.setSource("qrc:/qml/QGroundControl/VehicleSetup/VehicleSummary.qml")
            }
        } else if (QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable) {
            panelLoader.setSourceComponent(missingParametersVehicleSummaryComponent)
        } else {
            panelLoader.setSourceComponent(disconnectedVehicleAndParamsSummaryComponent)
        }
    }

    function showPanel(specialName, qmlSource) {
        if (mainWindow.allowViewSwitch()) {
            _selectedSpecial = specialName
            _selectedComponentIndex = -1
            _selectedSectionIndex = -1
            panelLoader.setSource(qmlSource)
        }
    }

    function _navigateToComponent(compIndex, sectionIndex) {
        if (!mainWindow.allowViewSwitch()) return
        if (!_fullParameterVehicleAvailable) return

        var components = _activeVehicle.autopilotPlugin.vehicleComponents
        if (compIndex < 0 || compIndex >= components.length) return
        var vehicleComponent = components[compIndex]

        _selectedSpecial = ""

        // If component opts in and root was clicked, auto-select first section
        if (sectionIndex < 0 && vehicleComponent.showFirstSectionOnRootClick && vehicleComponent.sectionIds.length > 0) {
            sectionIndex = 0
        }
        _selectedSectionIndex = sectionIndex

        var autopilotPlugin = _activeVehicle.autopilotPlugin
        var prereq = autopilotPlugin.prerequisiteSetup(vehicleComponent)
        if (prereq !== "") {
            // Selection state still updates so the tree expands and highlights
            // normally; only the panel area shows the prerequisite message
            _selectedComponentIndex = compIndex
            _showingPrereqMessage = true
            _messagePanelText = qsTr("%1 setup must be completed prior to %2 setup.").arg(prereq).arg(vehicleComponent.name)
            panelLoader.setSourceComponent(messagePanelComponent)
            return
        }

        if (_selectedComponentIndex !== compIndex || _showingPrereqMessage) {
            _selectedComponentIndex = compIndex
            _showingPrereqMessage = false
            panelLoader.setSource(vehicleComponent.setupSource, vehicleComponent)
        }

        // Apply section filter
        if (panelLoader.item && typeof panelLoader.item.sectionIdFilter !== "undefined") {
            panelLoader.item.sectionIdFilter = _sectionId(compIndex, sectionIndex)
        }
    }

    function showParametersPanel() {
        showPanel("parameters", "qrc:/qml/QGroundControl/VehicleSetup/SetupParameterEditor.qml")
    }

    function showVehicleComponentPanel(vehicleComponent) {
        if (!mainWindow.allowViewSwitch()) return
        if (!_fullParameterVehicleAvailable) return

        var components = _activeVehicle.autopilotPlugin.vehicleComponents
        for (var i = 0; i < components.length; i++) {
            if (components[i] === vehicleComponent) {
                _navigateToComponent(i, -1)
                return
            }
        }
    }

    Component.onCompleted: _showSummaryPanel()

    Connections {
        target: QGroundControl.corePlugin
        function onShowAdvancedUIChanged(showAdvancedUI) {
            if (!showAdvancedUI) {
                _showSummaryPanel()
            }
        }
    }

    Connections {
        target: QGroundControl.multiVehicleManager
        function onParameterReadyVehicleAvailableChanged(parametersReady) {
            if (parametersReady || _selectedSpecial === "summary" || _selectedSpecial !== "firmware") {
                _showSummaryPanel()
            }
        }
    }

    Connections {
        target: panelLoader
        function onLoaded() {
            if (panelLoader.item && typeof panelLoader.item.sectionIdFilter !== "undefined") {
                panelLoader.item.sectionIdFilter = _sectionId(_selectedComponentIndex, _selectedSectionIndex)
            }
        }
    }

    Component {
        id: noComponentsVehicleSummaryComponent
        Rectangle {
            color: qgcPal.windowShade
            QGCLabel {
                anchors.margins:        _defaultTextWidth * 2
                anchors.fill:           parent
                verticalAlignment:      Text.AlignVCenter
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.mediumFontPointSize
                text:                   qsTr("%1 does not currently support configuration of your vehicle. ").arg(QGroundControl.appName) +
                                        "If your vehicle is already configured you can still Fly."
            }
        }
    }

    Component {
        id: disconnectedVehicleAndParamsSummaryComponent
        Rectangle {
            id: disconnectedRect
            color: qgcPal.windowShade
            Column {
                anchors.centerIn:   parent
                spacing:            ScreenTools.defaultFontPixelHeight
                QGCLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width:              disconnectedRect.width - _defaultTextWidth * 4
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode:           Text.WordWrap
                    font.pointSize:     ScreenTools.largeFontPointSize
                    text:               !_activeVehicle
                                            ? qsTr("Vehicle configuration pages will display after you connect your vehicle and parameters have been downloaded.")
                                            : (_activeVehicle.parameterManager.parameterDownloadSkipped
                                                ? qsTr("Parameter download was skipped because the vehicle is flying. Configuration pages will be available after parameters are downloaded.")
                                                : qsTr("Waiting for vehicle parameters to download…"))
                }
                QGCButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text:       qsTr("Download Parameters")
                    visible:    _activeVehicle && _activeVehicle.parameterManager.parameterDownloadSkipped
                    enabled:    _activeVehicle && _activeVehicle.parameterManager.parameterDownloadSkipped && _activeVehicle.parameterManager.loadProgress === 0
                    onClicked:  _activeVehicle.parameterManager.refreshAllParameters()
                }
            }
        }
    }

    Component {
        id: missingParametersVehicleSummaryComponent

        Rectangle {
            color: qgcPal.windowShade

            QGCLabel {
                anchors.margins:        _defaultTextWidth * 2
                anchors.fill:           parent
                verticalAlignment:      Text.AlignVCenter
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.mediumFontPointSize
                text:                   qsTr("Vehicle did not return the full parameter list. ") +
                                        qsTr("As a result, the configuration pages are not available.")
            }
        }
    }

    Component {
        id: messagePanelComponent

        Item {
            objectName: "vehicleConfig_messagePanel"

            QGCLabel {
                anchors.margins:        _defaultTextWidth * 2
                anchors.fill:           parent
                verticalAlignment:      Text.AlignVCenter
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.mediumFontPointSize
                text:                   _messagePanelText
            }
        }
    }

    /// One line of the table of contents on the left; a sub line is a section of the selected page
    component TocItem: Rectangle {
        id:     tocItem
        height: _defaultTextHeight * 2.4
        radius: _defaultTextHeight * 0.4
        color:  checked ? qgcPal.windowShade : (tocMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent")

        property bool   checked:        false
        property bool   sub:            false
        property string text
        property bool   setupComplete:  true

        signal clicked()

        Rectangle {
            anchors.left:           parent.left
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.topMargin:      _defaultTextHeight * 0.3
            anchors.bottomMargin:   _defaultTextHeight * 0.3
            width:                  _defaultTextWidth * 0.5
            radius:                 width / 2
            color:                  qgcPal.buttonHighlight
            visible:                tocItem.checked && !tocItem.sub
        }

        QGCLabel {
            anchors.left:           parent.left
            anchors.leftMargin:     _defaultTextWidth * (tocItem.sub ? 4 : 2)
            anchors.right:          statusDot.left
            anchors.verticalCenter: parent.verticalCenter
            text:                   tocItem.text
            elide:                  Text.ElideRight
            font.weight:            tocItem.checked && !tocItem.sub ? Font.Bold : Font.Normal
            color:                  (tocItem.checked || !tocItem.sub) ? qgcPal.text : qgcPal.colorGrey
        }

        // Setup still needed
        Rectangle {
            id:                     statusDot
            anchors.right:          parent.right
            anchors.rightMargin:    _defaultTextWidth
            anchors.verticalCenter: parent.verticalCenter
            width:                  _defaultTextWidth
            height:                 width
            radius:                 width / 2
            color:                  qgcPal.colorOrange
            visible:                !tocItem.setupComplete
        }

        MouseArea {
            id:             tocMouse
            anchors.fill:   parent
            hoverEnabled:   true
            onClicked:      tocItem.clicked()
        }
    }

    // ---- Table of contents on the left ----
    Rectangle {
        id:                 toc
        objectName:         "vehicleConfig_sidebarFlickable"
        anchors.left:       parent.left
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _defaultTextHeight
        width:              _defaultTextWidth * 30
        radius:             _defaultTextHeight * 1.1
        color:              Qt.rgba(1, 1, 1, 0.04)
        border.color:       Qt.rgba(1, 1, 1, 0.10)

        QGCTextField {
            id:                 searchField
            anchors.left:       parent.left
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.margins:    _defaultTextWidth
            placeholderText:    qsTr("Search")
            visible:            _fullParameterVehicleAvailable
            onTextChanged:      vehicleConfigView._searchQuery = text
        }

        Flickable {
            anchors.left:       parent.left
            anchors.right:      parent.right
            anchors.top:        searchField.visible ? searchField.bottom : parent.top
            anchors.bottom:     parent.bottom
            anchors.margins:    _defaultTextWidth
            contentHeight:      tocColumn.height
            clip:               true

            ScrollBar.vertical: ScrollBar { }

            Column {
                id:         tocColumn
                width:      parent.width - _defaultTextWidth
                spacing:    _defaultTextHeight * 0.2

                TocItem {
                    id:         summaryButton
                    objectName: "vehicleConfig_summary"
                    width:      tocColumn.width
                    text:       qsTr("Summary")
                    checked:    vehicleConfigView._selectedSpecial === "summary"
                    onClicked:  showSummaryPanel()
                }

                Repeater {
                    id:     componentRepeater
                    model:  _fullParameterVehicleAvailable ? _activeVehicle.autopilotPlugin.vehicleComponents : 0

                    Column {
                        id:         compColumn
                        width:      tocColumn.width
                        spacing:    tocColumn.spacing
                        visible:    vehicleConfigView._componentVisible(modelData)

                        required property int index
                        required property var modelData

                        readonly property bool _selected: vehicleConfigView._selectedComponentIndex === index && vehicleConfigView._selectedSpecial === ""

                        TocItem {
                            objectName:     "vehicleConfig_comp_" + (compColumn.modelData ? compColumn.modelData.name.replace(/ /g, "") : "")
                            width:          compColumn.width
                            text:           compColumn.modelData ? compColumn.modelData.name : ""
                            setupComplete:  compColumn.modelData ? compColumn.modelData.setupComplete : true
                            checked:        compColumn._selected

                            // The page shows all of its sections one below the other; a component that
                            // wants to start on its first section opens on it
                            onClicked:      vehicleConfigView._navigateToComponent(compColumn.index, -1)
                        }

                        // Sections of the selected page, to jump to one
                        Repeater {
                            model: compColumn._selected && compColumn.modelData.sectionIds.length > 1 && !vehicleConfigView._showingPrereqMessage ? compColumn.modelData.sectionIds : []

                            TocItem {
                                id:         sectionItem
                                objectName: "vehicleConfig_section_" + modelData.replace(/ /g, "")
                                width:      compColumn.width
                                sub:        true
                                text:       vehicleConfigView._sectionDisplayName(compColumn.modelData, modelData)
                                checked:    vehicleConfigView._selectedSectionIndex === index
                                visible: {
                                    if (!vehicleConfigView._sectionMatchesSearch(compColumn.modelData, modelData)) return false
                                    if (!panelLoader.item || typeof panelLoader.item.sectionVisible !== "function") return true
                                    return panelLoader.item.sectionVisible(modelData)
                                }
                                setupComplete: {
                                    void compColumn.modelData.setupComplete
                                    return typeof compColumn.modelData.sectionSetupComplete === "function"
                                               ? compColumn.modelData.sectionSetupComplete(modelData) : true
                                }

                                required property string modelData
                                required property int    index

                                onClicked: vehicleConfigView._navigateToComponent(compColumn.index, (vehicleConfigView._selectedSectionIndex === index) ? -1 : index)
                            }
                        }
                    }
                }

                Rectangle {
                    width:      tocColumn.width
                    height:     1
                    color:      Qt.rgba(1, 1, 1, 0.10)
                    visible:    opticalFlowButton.visible || parametersButton.visible || firmwareButton.visible
                }

                TocItem {
                    id:         opticalFlowButton
                    width:      tocColumn.width
                    visible:    _activeVehicle ? _activeVehicle.flowImageIndex > 0 : false
                    text:       qsTr("Optical Flow")
                    checked:    vehicleConfigView._selectedSpecial === "opticalflow"
                    onClicked:  showPanel("opticalflow", "qrc:/qml/QGroundControl/VehicleSetup/OpticalFlowSensor.qml")
                }

                TocItem {
                    id:         parametersButton
                    objectName: "vehicleConfig_parametersButton"
                    width:      tocColumn.width
                    visible:    QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable &&
                                !_activeVehicle.usingHighLatencyLink &&
                                _corePlugin.showAdvancedUI
                    text:       qsTr("Parameters")
                    checked:    vehicleConfigView._selectedSpecial === "parameters"
                    onClicked:  showPanel("parameters", "qrc:/qml/QGroundControl/VehicleSetup/SetupParameterEditor.qml")
                }

                TocItem {
                    id:         firmwareButton
                    objectName: "vehicleConfig_firmwareButton"
                    width:      tocColumn.width
                    visible:    !ScreenTools.isMobile && _corePlugin.options.showFirmwareUpgrade
                    text:       qsTr("Firmware")
                    checked:    vehicleConfigView._selectedSpecial === "firmware"
                    onClicked:  showPanel("firmware", "qrc:/qml/QGroundControl/VehicleSetup/FirmwareUpgrade.qml")
                }
            }
        }
    }

    // ---- The selected page ----
    Rectangle {
        id:                 pageCard
        anchors.left:       toc.right
        anchors.right:      parent.right
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _defaultTextHeight
        radius:             _defaultTextHeight * 1.1
        color:              Qt.rgba(1, 1, 1, 0.04)
        border.color:       Qt.rgba(1, 1, 1, 0.10)

        Loader {
            id:                     panelLoader
            objectName:             "vehicleConfig_panelLoader"
            anchors.fill:           parent
            anchors.margins:        _defaultTextHeight * 0.6

            function setSource(source, vehicleComponent) {
                panelLoader.source = ""
                panelLoader.vehicleComponent = vehicleComponent
                panelLoader.source = source
            }

            function setSourceComponent(sourceComponent, vehicleComponent) {
                panelLoader.sourceComponent = undefined
                panelLoader.vehicleComponent = vehicleComponent
                panelLoader.sourceComponent = sourceComponent
            }

            property var vehicleComponent
        }
    }
}
