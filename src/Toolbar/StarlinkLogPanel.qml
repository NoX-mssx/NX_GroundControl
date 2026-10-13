import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Starlink log panel. Talks to the log service on the server end of the vehicle's tunnel; the service
/// picks the vehicle from the address of this computer, so the operator only sees their own vehicle.
///   Problems: explicit events (connection lost, reboot, obstructions ...)
///   Details:  per-minute deviations from normal
Rectangle {
    id: root

    /// Server address derived from the pinged router, tried first. Empty: only the fallbacks are tried.
    property string preferredHost:  ""
    property int    apiPort:        8099
    property var    fallbackHosts:  ["10.30.1.1", "10.30.2.1", "10.30.3.1", "10.30.4.1", "10.30.5.1"]

    property string baseUrl:        ""
    property string mode:           "problems"  // "problems" | "detail"
    property string statusKind:     ""          // "", "ok", "warn", "error"
    property string statusText:     ""
    property bool   busy:           false
    property int    probeIndex:     0
    property var    currentXhr:     null
    property var    timeoutHandler: null

    readonly property var _hosts: preferredHost !== "" ? [preferredHost].concat(fallbackHosts.filter(h => h !== preferredHost)) : fallbackHosts
    readonly property var _periods: [
        { label: qsTr("30 min"),    hours: 0.5 },
        { label: qsTr("1 hour"),    hours: 1 },
        { label: qsTr("3 hours"),   hours: 3 },
        { label: qsTr("6 hours"),   hours: 6 },
        { label: qsTr("24 hours"),  hours: 24 },
        { label: root.mode === "detail" ? qsTr("Custom") : qsTr("3 days"), hours: root.mode === "detail" ? -1 : 72 }
    ]
    readonly property bool _customPeriod: mode === "detail" && periodBox.currentIndex === 5

    color:          qgcPal.window
    radius:         ScreenTools.defaultFontPixelHeight / 2
    implicitWidth:  ScreenTools.defaultFontPixelWidth * 70
    implicitHeight: ScreenTools.defaultFontPixelHeight * 32

    QGCPalette { id: qgcPal }

    ListModel { id: logModel }

    Timer {
        id:         reqTimer
        repeat:     false
        onTriggered: if (root.timeoutHandler) root.timeoutHandler()
    }

    function request(url, timeoutMs, onOk, onFail) {
        if (currentXhr) {
            var old = currentXhr
            currentXhr = null
            old.abort()
        }
        var xhr = new XMLHttpRequest()
        currentXhr = xhr
        var finished = false
        function finish(ok, data, msg) {
            if (finished) {
                return
            }
            finished = true
            reqTimer.stop()
            if (currentXhr === xhr) {
                currentXhr = null
            }
            if (ok) {
                onOk(data)
            } else {
                onFail(msg)
            }
        }
        xhr.onreadystatechange = function () {
            if (xhr.readyState !== XMLHttpRequest.DONE) {
                return
            }
            if (xhr.status === 200) {
                var parsed = null
                try {
                    parsed = JSON.parse(xhr.responseText)
                } catch (e) {
                    finish(false, null, qsTr("invalid server response"))
                    return
                }
                finish(true, parsed, "")
            } else {
                var msg = xhr.status ? ("HTTP " + xhr.status) : qsTr("no connection")
                try {
                    var err = JSON.parse(xhr.responseText)
                    if (err.error) {
                        msg = err.error
                    }
                } catch (e2) {
                }
                finish(false, null, msg)
            }
        }
        timeoutHandler = function () {
            xhr.abort()
            finish(false, null, qsTr("timeout"))
        }
        reqTimer.interval = timeoutMs
        reqTimer.restart()
        xhr.open("GET", url)
        xhr.send()
    }

    function detect() {
        probeIndex = 0
        probeNext()
    }

    function probeNext() {
        if (probeIndex >= _hosts.length) {
            busy = false
            setStatus("error", qsTr("Log server is unreachable: check that the vehicle tunnel is up"))
            return
        }
        var base = "http://" + _hosts[probeIndex] + ":" + apiPort
        request(base + "/api/whoami", 1500,
                function () {
                    baseUrl = base
                    load()
                },
                function () {
                    probeIndex++
                    probeNext()
                })
    }

    function setStatus(kind, text) {
        statusKind = kind
        statusText = text
    }

    function parseLocal(s) {
        var m = /^(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2})$/.exec(s.trim())
        if (!m) {
            return NaN
        }
        return Math.floor(new Date(+m[1], +m[2] - 1, +m[3], +m[4], +m[5]).getTime() / 1000)
    }

    function load() {
        if (baseUrl === "") {
            busy = true
            setStatus("", qsTr("Looking for the log server..."))
            detect()
            return
        }
        var p = _periods[Math.max(0, periodBox.currentIndex)]
        var url = ""
        if (mode === "problems") {
            url = baseUrl + "/api/problems?hours=" + p.hours
        } else {
            var to = 0
            var from = 0
            if (p.hours > 0) {
                to = Math.floor(Date.now() / 1000)
                from = Math.floor(to - p.hours * 3600)
            } else {
                from = parseLocal(fromField.text)
                to = parseLocal(toField.text)
                if (isNaN(from) || isNaN(to) || to <= from) {
                    setStatus("error", qsTr("Invalid period. Format: 2026-10-11 11:00 (time of this computer)"))
                    return
                }
            }
            url = baseUrl + "/api/detail?from=" + from + "&to=" + to
        }
        busy = true
        setStatus("", qsTr("Loading..."))
        request(url, 20000, show, function (msg) {
            busy = false
            logModel.clear()
            setStatus("error", qsTr("Error: %1").arg(msg))
        })
    }

    function show(d) {
        busy = false
        logModel.clear()
        var items = d.items ? d.items : []
        for (var i = items.length - 1; i >= 0; --i) {
            var it = items[i]
            logModel.append({
                                "when":     String(it.time),
                                "dur":      String(it.duration_text),
                                "title":    String(it.title),
                                "details":  String(it.details),
                                "severity": String(it.severity)
                            })
        }
        var board = d.board ? (d.board + ": ") : ""
        if (d.status === "nodata") {
            setStatus("warn", board + qsTr("no data for the period"))
        } else if (items.length === 0) {
            setStatus("ok", board + (mode === "problems" ? qsTr("no problems found") : qsTr("no deviations from normal")))
        } else {
            var text = mode === "problems" ? qsTr("%1 events").arg(items.length) : qsTr("%1 deviations").arg(items.length)
            setStatus(d.status === "error" ? "error" : "warn", board + text)
        }
    }

    function switchMode(m) {
        mode = m
        periodBox.currentIndex = 1
        load()
    }

    Component.onCompleted: load()
    Component.onDestruction: {
        if (currentXhr) {
            currentXhr.abort()
        }
    }

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    ScreenTools.defaultFontPixelWidth
        spacing:            ScreenTools.defaultFontPixelHeight / 2

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                text:       qsTr("Problems")
                primary:    root.mode === "problems"
                onClicked:  root.switchMode("problems")
            }
            QGCButton {
                text:       qsTr("Details")
                primary:    root.mode === "detail"
                onClicked:  root.switchMode("detail")
            }
            QGCComboBox {
                id:                     periodBox
                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 16
                model:                  root._periods
                textRole:               "label"
                currentIndex:           1
                onActivated:            root.load()
            }
            QGCButton {
                text:       qsTr("Refresh")
                enabled:    !root.busy
                onClicked:  root.load()
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth
            visible:            root._customPeriod

            QGCLabel { text: qsTr("from") }
            QGCTextField {
                id:                 fromField
                Layout.fillWidth:   true
                placeholderText:    "2026-10-11 11:00"
            }
            QGCLabel { text: qsTr("to") }
            QGCTextField {
                id:                 toField
                Layout.fillWidth:   true
                placeholderText:    "2026-10-11 12:30"
                onAccepted:         root.load()
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               root.statusText
            wrapMode:           Text.Wrap
            font.bold:          true
            color:              root.statusKind === "ok" ? qgcPal.colorGreen
                              : root.statusKind === "warn" ? qgcPal.colorOrange
                              : root.statusKind === "error" ? qgcPal.colorRed : qgcPal.text
        }

        ListView {
            id:                 list
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            clip:               true
            spacing:            ScreenTools.defaultFontPixelHeight / 4
            model:              logModel
            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                id: card
                required property string when
                required property string dur
                required property string title
                required property string details
                required property string severity

                width:  ListView.view.width - ScreenTools.defaultFontPixelWidth
                height: col.implicitHeight + ScreenTools.defaultFontPixelHeight / 2
                radius: ScreenTools.defaultFontPixelHeight / 4
                color:  card.severity === "error" ? Qt.rgba(qgcPal.colorRed.r, qgcPal.colorRed.g, qgcPal.colorRed.b, 0.25)
                      : card.severity === "warn" ? Qt.rgba(qgcPal.colorOrange.r, qgcPal.colorOrange.g, qgcPal.colorOrange.b, 0.25)
                      : qgcPal.windowShade

                ColumnLayout {
                    id:                 col
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    ScreenTools.defaultFontPixelHeight / 4
                    spacing:            2

                    RowLayout {
                        Layout.fillWidth: true
                        QGCLabel { text: card.when; font.bold: true }
                        QGCLabel { text: card.dur; opacity: 0.7 }
                    }
                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               card.title
                        wrapMode:           Text.Wrap
                        font.bold:          true
                    }
                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               card.details
                        wrapMode:           Text.Wrap
                        opacity:            0.8
                        visible:            card.details.length > 0
                    }
                }
            }
        }
    }
}
