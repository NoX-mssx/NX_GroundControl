import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

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
    property string _selectedSpecial:        ""     // "summary" (readiness), "parameters", "firmware"
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
                panelLoader.setSourceComponent(readinessPageComponent)
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
            // The page title follows the menu, which names the pages for a ground vehicle
            if (panelLoader.item && typeof panelLoader.item.pageName !== "undefined" && _selectedSpecial === "") {
                const title = _titleForComponent(_selectedComponentIndex)
                if (title !== "") {
                    panelLoader.item.pageName = title
                }
            }
        }
    }

    function _titleForComponent(compIndex) {
        if (compIndex < 0 || compIndex >= _components.length) {
            return ""
        }
        const cls = _components[compIndex].className
        for (let g = 0; g < _menuGroups.length; g++) {
            for (let i = 0; i < _menuGroups[g].items.length; i++) {
                const item = _menuGroups[g].items[i]
                for (let p = 0; p < item.parts.length; p++) {
                    if (item.parts[p].cls === cls) {
                        return item.parts.length > 1 ? item.title + " · " + item.parts[p].title : item.title
                    }
                }
            }
        }
        for (let sp = 0; sp < _specialistParts.length; sp++) {
            if (_specialistParts[sp].cls === cls) {
                return _specialistParts[sp].title
            }
        }
        return ""
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

    // ---- Grouping of the setup pages ----
    // Pages are found by their C++ class name; one menu entry can hold several pages, which then appear as its
    // sub entries. Pages not listed here and not hidden end up in "For specialists".

    readonly property var _menuGroups: [
        { title: qsTr("SETUP"), items: [
            { key: "sensors",  title: qsTr("Sensors"),             parts: [ { cls: "APMSensorsComponent",     title: "" } ] },
            { key: "power",    title: qsTr("Power"),               parts: [ { cls: "APMPowerComponent",       title: "" } ] },
            { key: "outputs",  title: qsTr("Motors and outputs"),  parts: [ { cls: "APMServoComponent",       title: qsTr("Outputs") },
                                                                           { cls: "APMMotorComponent",       title: qsTr("Motor test") },
                                                                           { cls: "APMESCComponent",         title: qsTr("ESC") } ] },
            { key: "rc",       title: qsTr("Radio and modes"),     parts: [ { cls: "APMRadioComponent",       title: qsTr("Radio calibration") },
                                                                           { cls: "APMFlightModesComponent", title: qsTr("Drive modes") } ] },
            { key: "joystick", title: qsTr("Joystick"),            parts: [ { cls: "JoystickComponent",       title: "" } ] }
        ] },
        { title: qsTr("SAFETY"), items: [
            { key: "safety",   title: qsTr("Safety"),              parts: [ { cls: "APMFailsafesComponent",   title: qsTr("Failsafes") },
                                                                           { cls: "APMFlightSafetyComponent", title: qsTr("Arming checks") } ] }
        ] }
    ]

    readonly property var _specialistParts: [
        { key: "frame", cls: "APMAirframeComponent", title: qsTr("Frame") },
        { key: "",      cls: "APMLoggingComponent",  title: qsTr("Logging") },
        { key: "",      cls: "ScriptingComponent",   title: qsTr("Scripting") }
    ]

    /// Pages that have nothing for a ground vehicle with NX cameras
    readonly property var _hiddenClasses: [ "APMTuningComponent", "APMGimbalComponent", "APMRemoteSupportComponent", "APMFollowComponent" ]

    readonly property var _components: _fullParameterVehicleAvailable ? _activeVehicle.autopilotPlugin.vehicleComponents : []

    property bool _specialistOpen: false

    function _compIndex(cls) {
        for (let i = 0; i < _components.length; i++) {
            if (_components[i].className === cls) {
                return i
            }
        }
        return -1
    }

    /// Parts of a menu entry that exist on this vehicle: [{ index, title }]
    function _resolveParts(parts) {
        const resolved = []
        for (let i = 0; i < parts.length; i++) {
            const index = _compIndex(parts[i].cls)
            if (index >= 0 && _components[index].setupSource.toString() !== "") {
                resolved.push({ index: index, title: parts[i].title !== "" ? parts[i].title : _components[index].name, key: parts[i].key || "" })
            }
        }
        return resolved
    }

    /// Specialist parts plus every page that is neither grouped nor hidden
    function _specialistResolved() {
        const used = _hiddenClasses.slice()
        for (let g = 0; g < _menuGroups.length; g++) {
            for (let i = 0; i < _menuGroups[g].items.length; i++) {
                for (let p = 0; p < _menuGroups[g].items[i].parts.length; p++) {
                    used.push(_menuGroups[g].items[i].parts[p].cls)
                }
            }
        }
        const parts = _specialistParts.slice()
        for (let s = 0; s < _specialistParts.length; s++) {
            used.push(_specialistParts[s].cls)
        }
        for (let c = 0; c < _components.length; c++) {
            if (used.indexOf(_components[c].className) === -1) {
                parts.push({ key: "", cls: _components[c].className, title: "" })
            }
        }
        return _resolveParts(parts)
    }

    function _matchesSearch(title, resolved) {
        if (_searchQuery.trim() === "") {
            return true
        }
        if (title.toLowerCase().indexOf(_searchQuery.toLowerCase().trim()) !== -1) {
            return true
        }
        for (let i = 0; i < resolved.length; i++) {
            if (resolved[i].title.toLowerCase().indexOf(_searchQuery.toLowerCase().trim()) !== -1 || _componentMatchesSearch(_components[resolved[i].index])) {
                return true
            }
        }
        return false
    }

    function _partSelected(resolved) {
        if (_selectedSpecial !== "") {
            return false
        }
        for (let i = 0; i < resolved.length; i++) {
            if (resolved[i].index === _selectedComponentIndex) {
                return true
            }
        }
        return false
    }

    /// "ok", "warn" or "" for a menu key, from the readiness checks
    function _menuStatus(key) {
        if (key === "" || !readinessLoader.item) {
            return ""
        }
        const rows = readinessLoader.item.rows
        let found = false
        for (let i = 0; i < rows.length; i++) {
            if (rows[i].menu === key) {
                found = true
                if (!rows[i].ok) {
                    return "warn"
                }
            }
        }
        return found ? "ok" : ""
    }

    readonly property int _problemCount: readinessLoader.item ? readinessLoader.item.rows.filter(r => !r.ok).length : 0

    // ---- Readiness checks, shared by the menu marks and the readiness page ----
    Loader {
        id:                 readinessLoader
        active:             _fullParameterVehicleAvailable
        sourceComponent:    readinessComponent
    }

    Component {
        id: readinessComponent

        Item {
            FactPanelController { id: controller }

            function _fact(name) {
                return controller.parameterExists(-1, name) ? controller.getParameterFact(-1, name, false) : null
            }

            function _outputFor(functionValue) {
                for (let i = 1; i <= 32; i++) {
                    const fact = _fact("SERVO" + i + "_FUNCTION")
                    if (!fact) {
                        if (i > 16) {
                            break
                        }
                        continue
                    }
                    if (fact.rawValue === functionValue) {
                        return i
                    }
                }
                return 0
            }

            /// Outputs given to a Lua script (SERVOn_FUNCTION Script1..Script16 = 94..109), as "1, 2 (Script1)"
            function _scriptOutputs() {
                const outputs = []
                for (let i = 1; i <= 32; i++) {
                    const fact = _fact("SERVO" + i + "_FUNCTION")
                    if (!fact) {
                        if (i > 16) {
                            break
                        }
                        continue
                    }
                    if (fact.rawValue >= 94 && fact.rawValue <= 109) {
                        outputs.push(i + " (" + fact.enumStringValue + ")")
                    }
                }
                return outputs.join(", ")
            }

            readonly property var _battMonitor:     _fact("BATT_MONITOR")
            readonly property var _battLowAction:   _fact("BATT_FS_LOW_ACT")
            readonly property var _gcsEnable:       _fact("FS_GCS_ENABLE")
            readonly property var _fsAction:        _fact("FS_ACTION")
            readonly property var _thrEnable:       _fact("FS_THR_ENABLE")
            readonly property var _modeChannel:     _fact("MODE_CH")
            readonly property var _scrEnable:       _fact("SCR_ENABLE")

            /// [{ key, menu, cls, title, ok, detail }]: what is checked before the vehicle goes out
            readonly property var rows: {
                const list = []
                function add(key, menu, cls, title, ok, detail) {
                    list.push({ key: key, menu: menu, cls: cls, title: title, ok: ok, detail: detail })
                }
                function component(cls) {
                    const index = vehicleConfigView._compIndex(cls)
                    return index >= 0 ? vehicleConfigView._components[index] : null
                }

                const frame = component("APMAirframeComponent")
                if (frame) {
                    add("frame", "frame", "APMAirframeComponent", qsTr("Frame"), frame.setupComplete,
                        frame.setupComplete ? qsTr("Frame type is set") : qsTr("Frame type is not set"))
                }

                const sensors = component("APMSensorsComponent")
                if (sensors) {
                    add("sensors", "sensors", "APMSensorsComponent", qsTr("Sensors"), sensors.setupComplete,
                        sensors.setupComplete ? qsTr("Compass and accelerometer are calibrated") : qsTr("The compass and accelerometer need calibration"))
                }

                if (_battMonitor) {
                    const monitorSet = _battMonitor.rawValue !== 0
                    add("power", "power", "APMPowerComponent", qsTr("Power"), monitorSet,
                        monitorSet ? qsTr("Battery monitor: %1").arg(_battMonitor.enumStringValue) : qsTr("Battery monitor is not set"))
                }

                const left = _outputFor(73)
                const right = _outputFor(74)
                const throttle = _outputFor(70)
                const steering = _outputFor(26)
                const scriptOutputs = _scriptOutputs()
                if (left > 0 && right > 0) {
                    add("outputs", "outputs", "APMServoComponent", qsTr("Motors and outputs"), true,
                        qsTr("Left motor: output %1, right motor: output %2").arg(left).arg(right))
                } else if (throttle > 0 && steering > 0) {
                    add("outputs", "outputs", "APMServoComponent", qsTr("Motors and outputs"), true,
                        qsTr("Throttle: output %1, steering: output %2").arg(throttle).arg(steering))
                } else if (scriptOutputs !== "") {
                    // Motors driven by a Lua script only move while scripting is on
                    const scriptingOn = !_scrEnable || _scrEnable.rawValue !== 0
                    add("outputs", "outputs", "APMServoComponent", qsTr("Motors and outputs"), scriptingOn,
                        scriptingOn ? qsTr("Script outputs: %1").arg(scriptOutputs)
                                    : qsTr("Outputs %1 are given to a script, but scripting is off (SCR_ENABLE)").arg(scriptOutputs))
                } else {
                    add("outputs", "outputs", "APMServoComponent", qsTr("Motors and outputs"), false,
                        qsTr("Motor outputs are not assigned"))
                }

                const radio = component("APMRadioComponent")
                if (radio) {
                    add("radio", "rc", "APMRadioComponent", qsTr("RC radio"), radio.setupComplete,
                        radio.setupComplete ? qsTr("The radio is calibrated") : qsTr("The radio is not calibrated: the channels have default values"))
                }

                if (_modeChannel && component("APMFlightModesComponent")) {
                    add("modes", "rc", "APMFlightModesComponent", qsTr("Drive modes"), true,
                        qsTr("Mode switch on channel %1").arg(_modeChannel.rawValue))
                }

                const joystick = joystickManager.activeJoystick
                const joystickCalibrated = joystick ? joystick.settings.calibrated.rawValue : false
                const joystickOn = joystickManager.activeJoystickEnabledForActiveVehicle
                add("joystick", "joystick", "JoystickComponent", qsTr("Joystick"), !!joystick && joystickCalibrated && joystickOn,
                    !joystick ? qsTr("The joystick is not connected")
                              : (!joystickCalibrated ? qsTr("The joystick is not calibrated")
                                                     : (!joystickOn ? qsTr("Joystick control is off") : qsTr("Connected, calibrated, control is on"))))

                if (_gcsEnable) {
                    const monitored = _gcsEnable.rawValue !== 0
                    const action = _fsAction ? _fsAction.rawValue : 1
                    add("gcs", "safety", "APMFailsafesComponent", qsTr("Loss of link with NX"), monitored && action !== 0,
                        !monitored ? qsTr("Loss of link with NX is not monitored")
                                   : (action === 0 ? qsTr("No action is set: the vehicle keeps doing its last command")
                                                   : qsTr("On link loss: %1").arg(_fsAction ? _fsAction.enumStringValue : "")))
                }

                if (_thrEnable) {
                    const monitored = _thrEnable.rawValue !== 0
                    add("rcloss", "safety", "APMFailsafesComponent", qsTr("RC signal loss"), monitored,
                        monitored ? qsTr("RC signal loss is monitored") : qsTr("RC signal loss is not monitored"))
                }

                if (_battMonitor && _battMonitor.rawValue !== 0 && _battLowAction) {
                    const acting = _battLowAction.rawValue !== 0
                    add("battery", "safety", "APMFailsafesComponent", qsTr("Low battery"), acting,
                        acting ? qsTr("On low battery: %1").arg(_battLowAction.enumStringValue) : qsTr("No action on low battery"))
                }
                return list
            }
        }
    }

    // ---- Readiness page: what still needs doing, then what is ready ----
    Component {
        id: readinessPageComponent

        QGCFlickable {
            id:             readinessFlickable
            contentHeight:  readinessColumn.height + _defaultTextHeight * 2
            clip:           true

            readonly property var _rows: readinessLoader.item ? readinessLoader.item.rows : []
            readonly property var _problems: _rows.filter(r => !r.ok)
            readonly property var _ready: _rows.filter(r => r.ok)

            ColumnLayout {
                id:         readinessColumn
                x:          _defaultTextHeight
                y:          _defaultTextHeight
                width:      Math.min(readinessFlickable.width - _defaultTextHeight * 2, _defaultTextWidth * 110)
                spacing:    _defaultTextHeight

                ColumnLayout {
                    Layout.fillWidth:   true
                    spacing:            _defaultTextHeight * 0.3

                    QGCLabel {
                        text:           qsTr("Vehicle readiness")
                        font.pointSize: ScreenTools.largeFontPointSize
                        font.weight:    Font.DemiBold
                    }
                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               qsTr("What to check before the vehicle goes out. Open an item to go to its settings.")
                        color:              qgcPal.colorGrey
                        wrapMode:           Text.WordWrap
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            _defaultTextWidth * 2

                    Rectangle {
                        Layout.fillWidth:   true
                        implicitHeight:     _defaultTextHeight * 0.5
                        radius:             height / 2
                        color:              Qt.rgba(1, 1, 1, 0.10)

                        Rectangle {
                            width:  readinessFlickable._rows.length > 0 ? parent.width * readinessFlickable._ready.length / readinessFlickable._rows.length : 0
                            height: parent.height
                            radius: height / 2
                            color:  qgcPal.colorGreen
                        }
                    }
                    QGCLabel {
                        text:           qsTr("%1 of %2 ready").arg(readinessFlickable._ready.length).arg(readinessFlickable._rows.length)
                        font.weight:    Font.DemiBold
                    }
                }

                ConfigSection {
                    Layout.fillWidth:   true
                    visible:            readinessFlickable._problems.length > 0
                    heading:            qsTr("Needs attention: %1").arg(readinessFlickable._problems.length)

                    Repeater {
                        model: readinessFlickable._problems
                        delegate: readinessRow
                    }
                }

                ConfigSection {
                    Layout.fillWidth:   true
                    visible:            readinessFlickable._ready.length > 0
                    heading:            qsTr("Ready: %1").arg(readinessFlickable._ready.length)

                    Repeater {
                        model: readinessFlickable._ready
                        delegate: readinessRow
                    }
                }
            }

            Component {
                id: readinessRow

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            _defaultTextWidth * 1.5

                    required property var modelData

                    QGCColoredImage {
                        Layout.preferredWidth:  _defaultTextHeight * 1.2
                        Layout.preferredHeight: Layout.preferredWidth
                        sourceSize.height:      Layout.preferredHeight
                        source:                 modelData.ok ? "/qmlimages/checkbox-check.svg" : "/res/nx/Warning.svg"
                        color:                  modelData.ok ? qgcPal.colorGreen : qgcPal.colorOrange
                    }

                    ColumnLayout {
                        Layout.fillWidth:   true
                        spacing:            0

                        QGCLabel {
                            text:           modelData.title
                            font.weight:    modelData.ok ? Font.Normal : Font.DemiBold
                        }
                        QGCLabel {
                            Layout.fillWidth:   true
                            text:               modelData.detail
                            color:              qgcPal.colorGrey
                            wrapMode:           Text.WordWrap
                        }
                    }

                    QGCButton {
                        Layout.preferredWidth:  _defaultTextWidth * 18
                        text:                   modelData.ok ? qsTr("Open") : qsTr("Fix")
                        primary:                !modelData.ok
                        enabled:                vehicleConfigView._compIndex(modelData.cls) >= 0
                        onClicked:              vehicleConfigView._navigateToComponent(vehicleConfigView._compIndex(modelData.cls), -1)
                    }
                }
            }
        }
    }

    /// One line of the menu on the left: level 0 is a page, 1 a part of it, 2 a section of that part
    component TocItem: Rectangle {
        id:     tocItem
        height: _defaultTextHeight * (level === 0 ? 2.4 : 2.0)
        radius: _defaultTextHeight * 0.4
        color:  (checked && level === 0) ? qgcPal.buttonHighlight
                                         : (checked ? qgcPal.windowShade : (tocMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"))

        property bool   checked:    false
        property int    level:      0
        property string text
        property string status:     ""      // "ok", "warn" or ""
        property int    badge:      0
        property bool   chevron:    false
        property bool   expanded:   false

        readonly property bool _onAccent: checked && level === 0

        signal clicked()

        QGCLabel {
            anchors.left:           parent.left
            anchors.leftMargin:     _defaultTextWidth * (1.5 + tocItem.level * 2)
            anchors.right:          statusRow.left
            anchors.rightMargin:    _defaultTextWidth * 0.5
            anchors.verticalCenter: parent.verticalCenter
            text:                   tocItem.text
            elide:                  Text.ElideRight
            font.weight:            tocItem._onAccent ? Font.DemiBold : Font.Normal
            color:                  tocItem._onAccent ? qgcPal.buttonHighlightText
                                                      : ((tocItem.level > 0 && !tocItem.checked) ? qgcPal.colorGrey : qgcPal.text)
        }

        Row {
            id:                     statusRow
            anchors.right:          parent.right
            anchors.rightMargin:    _defaultTextWidth
            anchors.verticalCenter: parent.verticalCenter
            spacing:                _defaultTextWidth * 0.6

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible:                tocItem.badge > 0
                width:                  Math.max(height, badgeLabel.implicitWidth + _defaultTextWidth)
                height:                 _defaultTextHeight * 1.2
                radius:                 height / 2
                color:                  tocItem._onAccent ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(1, 0.62, 0.26, 0.25)

                QGCLabel {
                    id:                 badgeLabel
                    anchors.centerIn:   parent
                    text:               tocItem.badge
                    color:              "#FFB27A"
                    font.weight:        Font.DemiBold
                }
            }

            QGCColoredImage {
                anchors.verticalCenter: parent.verticalCenter
                visible:                tocItem.status !== ""
                width:                  _defaultTextHeight
                height:                 width
                sourceSize.height:      height
                source:                 tocItem.status === "ok" ? "/qmlimages/checkbox-check.svg" : "/res/nx/Warning.svg"
                color:                  tocItem._onAccent ? qgcPal.buttonHighlightText
                                                          : (tocItem.status === "ok" ? qgcPal.colorGreen : qgcPal.colorOrange)
            }

            QGCColoredImage {
                anchors.verticalCenter: parent.verticalCenter
                visible:                tocItem.chevron
                width:                  _defaultTextHeight * 0.9
                height:                 width
                sourceSize.height:      height
                source:                 "/res/nx/ChevronDown.svg"
                rotation:               tocItem.expanded ? 0 : -90
                color:                  qgcPal.colorGrey
            }
        }

        MouseArea {
            id:             tocMouse
            anchors.fill:   parent
            hoverEnabled:   true
            onClicked:      tocItem.clicked()
        }
    }

    /// Group title in the menu
    component TocGroupLabel: QGCLabel {
        leftPadding:    _defaultTextWidth * 1.5
        topPadding:     _defaultTextHeight * 0.6
        bottomPadding:  _defaultTextHeight * 0.2
        color:          qgcPal.colorGrey
        font.pointSize: ScreenTools.smallFontPointSize
        font.weight:    Font.DemiBold
    }

    /// A menu entry with its parts (several pages) or its sections (one page with several sections)
    component TocEntry: Column {
        id:         entry
        spacing:    tocColumn.spacing

        property string title
        property string menuKey
        property var    resolved: []

        readonly property bool _selected:   vehicleConfigView._partSelected(resolved)
        readonly property var  _single:     resolved.length === 1 ? vehicleConfigView._components[resolved[0].index] : null

        visible: resolved.length > 0 && vehicleConfigView._matchesSearch(title, resolved)

        TocItem {
            width:      entry.width
            text:       entry.title
            status:     vehicleConfigView._menuStatus(entry.menuKey)
            checked:    entry._selected
            onClicked:  vehicleConfigView._navigateToComponent(entry.resolved[0].index, -1)
        }

        // Several pages under one entry
        Repeater {
            model: entry._selected && entry.resolved.length > 1 ? entry.resolved : []

            Column {
                width:      entry.width
                spacing:    tocColumn.spacing

                required property var modelData

                readonly property bool _partChecked: vehicleConfigView._selectedComponentIndex === modelData.index

                TocItem {
                    width:      parent.width
                    level:      1
                    text:       modelData.title
                    checked:    parent._partChecked
                    onClicked:  vehicleConfigView._navigateToComponent(modelData.index, -1)
                }

                TocSections {
                    width:      parent.width
                    level:      2
                    compIndex:  modelData.index
                    shown:      parent._partChecked
                }
            }
        }

        // Sections of a single page
        TocSections {
            width:      entry.width
            level:      1
            compIndex:  entry.resolved.length === 1 ? entry.resolved[0].index : -1
            shown:      entry._selected && entry.resolved.length === 1
        }
    }

    /// Sections of one page, to jump to one of them
    component TocSections: Column {
        id:         sections
        spacing:    tocColumn.spacing

        property int    compIndex:  -1
        property int    level:      1
        property bool   shown:      false

        readonly property var _component: compIndex >= 0 ? vehicleConfigView._components[compIndex] : null

        Repeater {
            model: sections.shown && sections._component && sections._component.sectionIds.length > 1 && !vehicleConfigView._showingPrereqMessage
                   ? sections._component.sectionIds : []

            TocItem {
                width:      sections.width
                level:      sections.level
                text:       vehicleConfigView._sectionDisplayName(sections._component, modelData)
                checked:    vehicleConfigView._selectedSectionIndex === index
                visible: {
                    if (!vehicleConfigView._sectionMatchesSearch(sections._component, modelData)) return false
                    if (!panelLoader.item || typeof panelLoader.item.sectionVisible !== "function") return true
                    return panelLoader.item.sectionVisible(modelData)
                }

                required property string modelData
                required property int    index

                onClicked: vehicleConfigView._navigateToComponent(sections.compIndex, (vehicleConfigView._selectedSectionIndex === index) ? -1 : index)
            }
        }
    }

    // ---- Menu on the left ----
    Rectangle {
        id:                 toc
        objectName:         "vehicleConfig_sidebarFlickable"
        anchors.left:       parent.left
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.margins:    _defaultTextHeight
        width:              _defaultTextWidth * 32
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
                    text:       qsTr("Vehicle readiness")
                    badge:      vehicleConfigView._problemCount
                    checked:    vehicleConfigView._selectedSpecial === "summary"
                    onClicked:  showSummaryPanel()
                }

                Repeater {
                    model: _fullParameterVehicleAvailable ? vehicleConfigView._menuGroups : []

                    Column {
                        width:      tocColumn.width
                        spacing:    tocColumn.spacing

                        required property var modelData

                        TocGroupLabel {
                            text: modelData.title
                        }

                        Repeater {
                            model: modelData.items

                            TocEntry {
                                width:      tocColumn.width
                                title:      modelData.title
                                menuKey:    modelData.key
                                resolved:   vehicleConfigView._resolveParts(modelData.parts)

                                required property var modelData
                            }
                        }
                    }
                }

                Rectangle {
                    width:      tocColumn.width
                    height:     1
                    color:      Qt.rgba(1, 1, 1, 0.10)
                }

                // ---- For specialists: folded unless something in it is open ----
                Column {
                    id:         specialistColumn
                    width:      tocColumn.width
                    spacing:    tocColumn.spacing

                    readonly property var  _resolved:   _fullParameterVehicleAvailable ? vehicleConfigView._specialistResolved() : []
                    readonly property bool _inside:     vehicleConfigView._partSelected(_resolved) || vehicleConfigView._selectedSpecial === "parameters" || vehicleConfigView._selectedSpecial === "firmware"
                    readonly property bool _open:       vehicleConfigView._specialistOpen || _inside || vehicleConfigView._searchQuery.trim() !== ""

                    TocItem {
                        width:      specialistColumn.width
                        text:       qsTr("For specialists")
                        status:     vehicleConfigView._menuStatus("frame") === "warn" ? "warn" : ""
                        chevron:    true
                        expanded:   specialistColumn._open
                        onClicked:  vehicleConfigView._specialistOpen = !vehicleConfigView._specialistOpen
                    }

                    Repeater {
                        model: specialistColumn._open ? specialistColumn._resolved : []

                        Column {
                            width:      specialistColumn.width
                            spacing:    tocColumn.spacing
                            visible:    vehicleConfigView._matchesSearch(modelData.title, [ modelData ])

                            required property var modelData

                            readonly property bool _checked: vehicleConfigView._selectedSpecial === "" && vehicleConfigView._selectedComponentIndex === modelData.index

                            TocItem {
                                width:      parent.width
                                level:      1
                                text:       modelData.title
                                status:     vehicleConfigView._menuStatus(modelData.key) === "warn" ? "warn" : ""
                                checked:    parent._checked
                                onClicked:  vehicleConfigView._navigateToComponent(modelData.index, -1)
                            }

                            TocSections {
                                width:      parent.width
                                level:      2
                                compIndex:  modelData.index
                                shown:      parent._checked
                            }
                        }
                    }

                    TocItem {
                        id:         parametersButton
                        objectName: "vehicleConfig_parametersButton"
                        width:      specialistColumn.width
                        level:      1
                        visible:    specialistColumn._open &&
                                    QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable &&
                                    !_activeVehicle.usingHighLatencyLink &&
                                    _corePlugin.showAdvancedUI
                        text:       qsTr("All parameters")
                        checked:    vehicleConfigView._selectedSpecial === "parameters"
                        onClicked:  showPanel("parameters", "qrc:/qml/QGroundControl/VehicleSetup/SetupParameterEditor.qml")
                    }

                    TocItem {
                        id:         firmwareButton
                        objectName: "vehicleConfig_firmwareButton"
                        width:      specialistColumn.width
                        level:      1
                        visible:    specialistColumn._open && !ScreenTools.isMobile && _corePlugin.options.showFirmwareUpgrade
                        text:       qsTr("Firmware")
                        checked:    vehicleConfigView._selectedSpecial === "firmware"
                        onClicked:  showPanel("firmware", "qrc:/qml/QGroundControl/VehicleSetup/FirmwareUpgrade.qml")
                    }
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
