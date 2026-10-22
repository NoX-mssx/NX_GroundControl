import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactControls
import QGroundControl.Controls

/// First start: step 1 the measurement units, Next, step 2 the autopilot firmware. NX only drives rovers, so the
/// vehicle type is not asked and is always set to Rover.
FirstRunPrompt {
    id:         prompt
    title:      _step === 0 ? qsTr("Measurement Units") : qsTr("Autopilot Firmware")
    promptId:   QGroundControl.corePlugin.initialSetupPromptId
    buttons:    0     // no buttons in the title bar: Back / Next / Done are at the bottom

    property int  _step:             0
    property bool _multipleFirmware: !QGroundControl.singleFirmwareSupport
    property var  _appSettings:      QGroundControl.settingsManager.appSettings
    property var  _unitsSettings:    QGroundControl.settingsManager.unitsSettings
    property var  _rgFacts:          [ _unitsSettings.horizontalDistanceUnits, _unitsSettings.verticalDistanceUnits, _unitsSettings.areaUnits, _unitsSettings.speedUnits, _unitsSettings.temperatureUnits ]
    property var  _rgLabels:         [ qsTr("Horizontal Distance"), qsTr("Vertical Distance"), qsTr("Area"), qsTr("Speed"), qsTr("Temperature") ]
    property real _contentWidth:     ScreenTools.defaultFontPixelWidth * 56

    readonly property int _firmwareArduPilot:   3
    readonly property int _firmwarePX4:         12
    readonly property int _vehicleRover:        10

    function changeSystemOfUnits(metric) {
        unitComboBoxRepeater.model = 0
        unitComboBoxRepeater.model = _rgFacts.length

        if (_unitsSettings.horizontalDistanceUnits.userVisible) {
            _unitsSettings.horizontalDistanceUnits.value = metric ? UnitsSettings.HorizontalDistanceUnitsMeters : UnitsSettings.HorizontalDistanceUnitsFeet
        }
        if (_unitsSettings.verticalDistanceUnits.userVisible) {
            _unitsSettings.verticalDistanceUnits.value = metric ? UnitsSettings.VerticalDistanceUnitsMeters : UnitsSettings.VerticalDistanceUnitsFeet
        }
        if (_unitsSettings.areaUnits.userVisible) {
            _unitsSettings.areaUnits.value = metric ? UnitsSettings.AreaUnitsSquareMeters : UnitsSettings.AreaUnitsSquareFeet
        }
        if (_unitsSettings.speedUnits.userVisible) {
            _unitsSettings.speedUnits.value = metric ? UnitsSettings.SpeedUnitsMetersPerSecond : UnitsSettings.SpeedUnitsFeetPerSecond
        }
        if (_unitsSettings.temperatureUnits.userVisible) {
            _unitsSettings.temperatureUnits.value = metric ? UnitsSettings.TemperatureUnitsCelsius : UnitsSettings.TemperatureUnitsFarenheit
        }
    }

    function _finish() {
        if (_appSettings.preferredFirmwareClass.rawValue === 0) {
            _appSettings.preferredFirmwareClass.rawValue = _firmwareArduPilot
        }
        _appSettings.offlineEditingFirmwareClass.rawValue = _appSettings.preferredFirmwareClass.rawValue
        _appSettings.preferredVehicleClass.rawValue = _vehicleRover
        _appSettings.offlineEditingVehicleClass.rawValue = _vehicleRover
        prompt.close()
    }

    Component.onCompleted: {
        if (_appSettings.preferredFirmwareClass.rawValue === 0) {
            _appSettings.preferredFirmwareClass.rawValue = _firmwareArduPilot
        }
    }

    ColumnLayout {
        width:      prompt._contentWidth
        spacing:    ScreenTools.defaultFontPixelHeight

        QGCLabel {
            Layout.fillWidth:   true
            text:               prompt._multipleFirmware ? qsTr("Step %1 of 2").arg(prompt._step + 1) : ""
            color:              QGroundControl.globalPalette.colorGrey
            visible:            text !== ""
        }

        // ---- Step 1: units ----
        ColumnLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelHeight * 0.6
            visible:            prompt._step === 0

            QGCLabel {
                Layout.fillWidth:   true
                text:               qsTr("Choose the measurement units. You can change them later in the general settings.")
                wrapMode:           Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth * 2

                QGCLabel {
                    Layout.fillWidth:   true
                    text:               qsTr("System of units")
                }

                QGCComboBox {
                    sizeToContents: true
                    model:          [ qsTr("Metric System"), qsTr("Imperial System") ]
                    currentIndex:   prompt._unitsSettings.horizontalDistanceUnits.value === UnitsSettings.HorizontalDistanceUnitsMeters ? 0 : 1
                    onActivated:    (index) => { prompt.changeSystemOfUnits(currentIndex === 0) }
                }
            }

            Repeater {
                id:     unitComboBoxRepeater
                model:  prompt._rgFacts.length

                LabelledFactComboBox {
                    Layout.fillWidth:   true
                    label:              prompt._rgLabels[index]
                    fact:               prompt._rgFacts[index]
                    indexModel:         false
                    visible:            prompt._rgFacts[index].userVisible
                }
            }
        }

        // ---- Step 2: firmware ----
        ColumnLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelHeight * 0.6
            visible:            prompt._step === 1

            QGCLabel {
                Layout.fillWidth:   true
                text:               qsTr("Choose the firmware of the autopilots on your vehicles.")
                wrapMode:           Text.WordWrap
            }

            Repeater {
                model: [
                    { value: prompt._firmwareArduPilot, title: qsTr("ArduPilot"),   description: qsTr("ArduRover") },
                    { value: prompt._firmwarePX4,       title: qsTr("PX4 Pro"),     description: qsTr("PX4 rover") }
                ]

                Rectangle {
                    Layout.fillWidth:   true
                    implicitHeight:     optionColumn.implicitHeight + ScreenTools.defaultFontPixelHeight
                    radius:             ScreenTools.defaultFontPixelHeight * 0.5
                    color:              _selected ? Qt.rgba(QGroundControl.globalPalette.buttonHighlight.r, QGroundControl.globalPalette.buttonHighlight.g, QGroundControl.globalPalette.buttonHighlight.b, 0.18)
                                                  : QGroundControl.globalPalette.windowShade
                    border.color:       _selected ? QGroundControl.globalPalette.buttonHighlight : Qt.rgba(1, 1, 1, 0.12)
                    border.width:       _selected ? 2 : 1

                    required property var modelData

                    readonly property bool _selected: prompt._appSettings.preferredFirmwareClass.rawValue === modelData.value

                    ColumnLayout {
                        id:                 optionColumn
                        anchors.left:       parent.left
                        anchors.right:      parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins:    ScreenTools.defaultFontPixelHeight * 0.6
                        spacing:            0

                        QGCLabel {
                            text:           modelData.title
                            font.pointSize: ScreenTools.mediumFontPointSize
                            font.weight:    Font.DemiBold
                        }
                        QGCLabel {
                            text:   modelData.description
                            color:  QGroundControl.globalPalette.colorGrey
                        }
                    }

                    MouseArea {
                        anchors.fill:   parent
                        onClicked:      prompt._appSettings.preferredFirmwareClass.rawValue = modelData.value
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                text:       qsTr("Back")
                visible:    prompt._step > 0
                onClicked:  prompt._step = 0
            }

            Item { Layout.fillWidth: true }

            QGCButton {
                text:       (prompt._step === 0 && prompt._multipleFirmware) ? qsTr("Next") : qsTr("Done")
                primary:    true
                onClicked: {
                    if (prompt._step === 0 && prompt._multipleFirmware) {
                        prompt._step = 1
                    } else {
                        prompt._finish()
                    }
                }
            }
        }
    }
}
