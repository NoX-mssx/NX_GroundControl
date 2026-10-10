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

    /// A page chip across the top (same look as the application settings)
    component PageChip: Rectangle {
        id:         chip
        height:     _defaultTextHeight * 2.3
        width:      chipRow.implicitWidth + _defaultTextWidth * 3
        radius:     height / 2
        color:      checked ? qgcPal.buttonHighlight : (chipMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))

        property bool   checked:        false
        property string text
        property string iconSource
        property bool   setupComplete:  true

        signal clicked()

        Row {
            id:                 chipRow
            anchors.centerIn:   parent
            spacing:            _defaultTextWidth * 0.7

            QGCColoredImage {
                anchors.verticalCenter: parent.verticalCenter
                width:                  _defaultTextHeight
                height:                 width
                sourceSize.height:      height
                source:                 chip.iconSource
                color:                  chip.checked ? qgcPal.buttonHighlightText : qgcPal.text
                visible:                chip.iconSource !== ""
            }
            QGCLabel {
                anchors.verticalCenter: parent.verticalCenter
                text:                   chip.text
                color:                  chip.checked ? qgcPal.buttonHighlightText : qgcPal.text
                font.weight:            Font.Bold
            }
            // Setup still needed
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width:                  _defaultTextWidth
                height:                 width
                radius:                 width / 2
                color:                  qgcPal.colorOrange
                visible:                !chip.setupComplete
            }
        }

        MouseArea {
            id:             chipMouse
            anchors.fill:   parent
            hoverEnabled:   true
            onClicked:      chip.clicked()
        }
    }

    // ---- Pages as chips across the top ----
    Flow {
        id:                 pageChips
        objectName:         "vehicleConfig_sidebarFlickable"
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        anchors.margins:    _defaultTextHeight
        spacing:            _defaultTextWidth * 0.8

        PageChip {
            id:         summaryButton
            objectName: "vehicleConfig_summary"
            text:       qsTr("Summary")
            checked:    vehicleConfigView._selectedSpecial === "summary"
            onClicked:  showSummaryPanel()
        }

        Repeater {
            id:     componentRepeater
            model:  _fullParameterVehicleAvailable ? _activeVehicle.autopilotPlugin.vehicleComponents : 0

            PageChip {
                id:             compChip
                objectName:     "vehicleConfig_comp_" + (modelData ? modelData.name.replace(/ /g, "") : "")
                visible:        vehicleConfigView._componentVisible(modelData)
                text:           modelData ? modelData.name : ""
                iconSource:     modelData ? modelData.iconResource : ""
                setupComplete:  modelData ? modelData.setupComplete : true
                checked:        vehicleConfigView._selectedComponentIndex === index && vehicleConfigView._selectedSpecial === ""

                required property int index
                required property var modelData

                // A component with several sections opens on its first one
                onClicked: vehicleConfigView._navigateToComponent(index, (modelData && modelData.sectionIds.length > 1) ? 0 : -1)
            }
        }

        PageChip {
            id:         opticalFlowButton
            visible:    _activeVehicle ? _activeVehicle.flowImageIndex > 0 : false
            text:       qsTr("Optical Flow")
            checked:    vehicleConfigView._selectedSpecial === "opticalflow"
            onClicked:  showPanel("opticalflow", "qrc:/qml/QGroundControl/VehicleSetup/OpticalFlowSensor.qml")
        }

        PageChip {
            id:         parametersButton
            objectName: "vehicleConfig_parametersButton"
            visible:    QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable &&
                        !_activeVehicle.usingHighLatencyLink &&
                        _corePlugin.showAdvancedUI
            text:       qsTr("Parameters")
            checked:    vehicleConfigView._selectedSpecial === "parameters"
            onClicked:  showPanel("parameters", "qrc:/qml/QGroundControl/VehicleSetup/SetupParameterEditor.qml")
        }

        PageChip {
            id:         firmwareButton
            objectName: "vehicleConfig_firmwareButton"
            visible:    !ScreenTools.isMobile && _corePlugin.options.showFirmwareUpgrade
            text:       qsTr("Firmware")
            checked:    vehicleConfigView._selectedSpecial === "firmware"
            onClicked:  showPanel("firmware", "qrc:/qml/QGroundControl/VehicleSetup/FirmwareUpgrade.qml")
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

        readonly property var _component: (_fullParameterVehicleAvailable && vehicleConfigView._selectedSpecial === "" &&
                                           vehicleConfigView._selectedComponentIndex >= 0)
                                          ? _activeVehicle.autopilotPlugin.vehicleComponents[vehicleConfigView._selectedComponentIndex] : null
        readonly property var _sectionIds: _component ? _component.sectionIds : []

        Item {
            id:                     sectionTabs
            anchors.left:           parent.left
            anchors.right:          parent.right
            anchors.top:            parent.top
            anchors.leftMargin:     _defaultTextWidth * 3
            anchors.rightMargin:    _defaultTextWidth * 3
            height:                 visible ? _defaultTextHeight * 2.8 : 0
            visible:                pageCard._sectionIds.length > 1 && !vehicleConfigView._showingPrereqMessage

            Row {
                anchors.horizontalCenter:   parent.horizontalCenter
                anchors.bottom:             parent.bottom
                height:                     parent.height
                spacing:                    _defaultTextWidth * 3

                Repeater {
                    model: pageCard._sectionIds

                    Item {
                        id:         sectionTab
                        objectName: "vehicleConfig_section_" + modelData.replace(/ /g, "")
                        width:      sectionRow.implicitWidth
                        height:     parent.height
                        visible: {
                            if (!panelLoader.item || typeof panelLoader.item.sectionVisible !== "function") return true
                            return panelLoader.item.sectionVisible(modelData)
                        }

                        required property string modelData
                        required property int    index

                        readonly property bool _checked: vehicleConfigView._selectedSectionIndex === index
                        readonly property bool _setupComplete: {
                            if (!pageCard._component) return true
                            void pageCard._component.setupComplete
                            return typeof pageCard._component.sectionSetupComplete === "function"
                                       ? pageCard._component.sectionSetupComplete(modelData) : true
                        }

                        Row {
                            id:                     sectionRow
                            anchors.verticalCenter: parent.verticalCenter
                            spacing:                _defaultTextWidth * 0.5

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width:                  _defaultTextWidth
                                height:                 width
                                radius:                 width / 2
                                color:                  qgcPal.colorOrange
                                visible:                !sectionTab._setupComplete
                            }
                            QGCLabel {
                                text:           vehicleConfigView._sectionDisplayName(pageCard._component, sectionTab.modelData)
                                font.weight:    Font.Bold
                                color:          sectionTab._checked ? qgcPal.text : qgcPal.colorGrey
                            }
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
                            onClicked:      vehicleConfigView._navigateToComponent(vehicleConfigView._selectedComponentIndex, sectionTab.index)
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

        Loader {
            id:                     panelLoader
            objectName:             "vehicleConfig_panelLoader"
            anchors.left:           parent.left
            anchors.right:          parent.right
            anchors.top:            sectionTabs.bottom
            anchors.bottom:         parent.bottom
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
