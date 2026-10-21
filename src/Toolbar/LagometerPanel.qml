import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Lagometer panel. Two sources:
///   Server: state and problem log from the lagometer service on the server end of the vehicle's tunnel
///           (vehicle -> server and server -> operator). The service picks the vehicle from the address of this
///           computer, so the operator only sees their own vehicle.
///   NX:     losses and delay spikes measured by this program on the way to the vehicle's router
Rectangle {
    id: root

    /// Server address derived from the pinged router, tried first. Empty: only the fallbacks are tried.
    property string preferredHost:  ""
    property int    apiPort:        8091
    property var    fallbackHosts:  ["10.30.1.1", "10.30.2.1", "10.30.3.1", "10.30.4.1", "10.30.5.1"]

    property string baseUrl:        ""
    property string source:         "server"    // "server" | "nx"
    property string statusKind:     ""          // "", "ok", "warn", "error"
    property string statusText:     ""
    property string statusDetails:  ""
    property var    cameraLines:    []
    property bool   busy:           false
    property int    probeIndex:     0
    property var    currentXhr:     null
    property var    timeoutHandler: null
    property string shownSignature: ""

    readonly property var _hosts: preferredHost !== "" ? [preferredHost].concat(fallbackHosts.filter(h => h !== preferredHost)) : fallbackHosts
    readonly property var _periods: [
        { label: qsTr("10 min"),    minutes: 10 },
        { label: qsTr("30 min"),    minutes: 30 },
        { label: qsTr("1 hour"),    minutes: 60 },
        { label: qsTr("3 hours"),   minutes: 180 }
    ]

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

    // The server updates its data every 5 s
    Timer {
        interval:   5000
        repeat:     true
        running:    root.source === "server" && root.baseUrl !== ""
        onTriggered: if (!root.busy) root.refresh()
    }

    Connections {
        target:         LinkQualityMonitor
        function onEventsChanged() { if (root.source === "nx") root.showLocal() }
        function onPingChanged()   { if (root.source === "nx") root.showLocalStatus() }
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

    function setStatus(kind, text, details) {
        statusKind = kind
        statusText = text
        statusDetails = details ? details : ""
    }

    // ----- server data -----

    function detect() {
        probeIndex = 0
        probeNext()
    }

    function probeNext() {
        if (probeIndex >= _hosts.length) {
            busy = false
            cameraLines = []
            setStatus("error", qsTr("Lagometer server is unreachable: check that the vehicle tunnel is up"))
            return
        }
        var base = "http://" + _hosts[probeIndex] + ":" + apiPort
        request(base + "/", 1500,
                function () {
                    baseUrl = base
                    refresh()
                },
                function () {
                    probeIndex++
                    probeNext()
                })
    }

    function refresh() {
        if (source === "nx") {
            showLocalStatus()
            showLocal()
            return
        }
        if (baseUrl === "") {
            busy = true
            setStatus("", qsTr("Looking for the lagometer server..."))
            detect()
            return
        }
        busy = true
        request(baseUrl + "/v1/status", 8000, function (st) {
            showStatus(st)
            var minutes = _periods[Math.max(0, periodBox.currentIndex)].minutes
            request(baseUrl + "/v1/timeline?minutes=" + minutes, 15000, function (tl) {
                busy = false
                showTimeline(tl)
            }, function (msg) {
                busy = false
                setStatus("error", qsTr("Error: %1").arg(msg))
            })
        }, function (msg) {
            busy = false
            // Forget the address so the next refresh looks for the server again
            baseUrl = ""
            cameraLines = []
            setStatus("error", qsTr("Error: %1").arg(msg))
        })
    }

    function codeText(code) {
        switch (code) {
        case "ok":          return qsTr("All is well")
        case "board_lag":   return qsTr("Delay from the vehicle (Starlink or the vehicle)")
        case "board_loss":  return qsTr("Starlink lost packets, but no delay built up")
        case "viewer_bad":  return qsTr("The channel to the operator cannot cope")
        case "viewer_warn": return qsTr("Small losses on the operator's channel")
        case "offline":     return qsTr("No stream from the vehicle")
        case "meter_down":  return qsTr("The lagometer is not recording")
        case "no_data":     return qsTr("No lagometer data for this vehicle")
        default:            return ""
        }
    }

    function showStatus(st) {
        var kind = st.level === 0 ? "ok" : (st.level === 1 ? "warn" : "error")
        var text = codeText(st.code)
        if (text === "") {
            text = st.text ? String(st.text) : String(st.code)
        }
        var ongoing = st.ongoing && st.ongoing.length > 0
        if (ongoing) {
            text += ". " + qsTr("The problem is going on right now")
        }
        var lines = []
        var cams = st.cameras ? st.cameras : []
        for (var i = 0; i < cams.length; ++i) {
            var c = cams[i]
            var parts = [qsTr("online %1%").arg(Math.round(c.online_pct))]
            if (c.lag_max_s !== null && c.lag_max_s !== undefined) {
                parts.push(qsTr("delay %1 s").arg(c.lag_max_s))
            }
            if (c.rtt_avg_ms !== null && c.rtt_avg_ms !== undefined) {
                parts.push(qsTr("%1 ms").arg(c.rtt_avg_ms))
            }
            if (c.mbps !== null && c.mbps !== undefined) {
                parts.push(qsTr("%1 Mbit/s").arg(c.mbps))
            }
            if (c.srt_not_recovered > 0) {
                parts.push(qsTr("unrecovered packets %1").arg(c.srt_not_recovered))
            }
            lines.push(String(c.label) + ": " + parts.join(", "))
        }
        cameraLines = lines
        setStatus(kind, text)
    }

    /// "2026-10-21T07:19:00" -> seconds. Both ends are server local time, so only differences are used.
    function isoSeconds(s) {
        var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})/.exec(String(s))
        return m ? Math.floor(Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6]) / 1000) : NaN
    }

    function durationText(sec) {
        var min = Math.max(1, Math.round(sec / 60))
        if (min < 60) {
            return qsTr("%1 min").arg(min)
        }
        return qsTr("%1 h %2 min").arg(Math.floor(min / 60)).arg(min % 60)
    }

    function eventTitle(e) {
        switch (e.kind) {
        case "lag":      return qsTr("Delay from the vehicle up to %1 s").arg(e.lag_max_s)
        case "freeze":   return qsTr("The stream froze (up to %1 s without frames)").arg(e.frozen_max_s)
        case "drops":    return qsTr("Starlink lost packets (up to %1 per minute)").arg(e.drops_per_min_max)
        case "nostream": return qsTr("No stream from the vehicle")
        case "viewer":   return qsTr("Operator channel: losses up to %1%, queue up to %2 KB, %3 frames dropped")
                                 .arg(e.loss_max_pct).arg(e.queue_max_kb).arg(e.frames_dropped)
        default:         return e.text ? String(e.text) : String(e.kind)
        }
    }

    function eventWhere(e) {
        if (e.kind === "viewer") {
            return qsTr("Channel server - operator")
        }
        return qsTr("Starlink or the vehicle")
    }

    function showTimeline(tl) {
        var events = tl.events ? tl.events : []
        var now = isoSeconds(tl.time)
        var items = []
        for (var i = events.length - 1; i >= 0; --i) {
            var e = events[i]
            var start = isoSeconds(e.start)
            var end = isoSeconds(e.end)
            var ago = isNaN(now) || isNaN(start) ? "" : qsTr("%1 ago").arg(durationText(now - start))
            items.push({
                           "when":     String(e.start).replace("T", " ").substring(0, 16),
                           "dur":      (isNaN(end - start) ? "" : durationText(end - start + 1)) + (ago !== "" ? " · " + ago : ""),
                           "title":    eventTitle(e),
                           "details":  eventWhere(e),
                           "severity": e.level >= 2 ? "error" : "warn"
                       })
        }
        fillModel(items)
        if (tl.code === "no_data") {
            setStatus("warn", codeText("no_data"))
        }
    }

    // ----- data of this program -----

    function showLocalStatus() {
        cameraLines = []
        if (!LinkQualityMonitor.monitoring) {
            setStatus("warn", qsTr("Ping to the router is not set up for this link"))
        } else if (LinkQualityMonitor.pingMs < 0) {
            setStatus("error", qsTr("No replies from %1").arg(LinkQualityMonitor.pingAddress))
        } else {
            setStatus("ok", qsTr("Ping to %1: %2 ms").arg(LinkQualityMonitor.pingAddress).arg(LinkQualityMonitor.pingMs))
        }
    }

    function showLocal() {
        var events = LinkQualityMonitor.events
        var items = []
        for (var i = 0; i < events.length; ++i) {
            var e = events[i]
            var durSec = Math.max(1, Math.round((e.endMs - e.startMs) / 1000))
            var loss = e.kind === "loss"
            items.push({
                           "when":     Qt.formatDateTime(new Date(e.startMs), "yyyy-MM-dd HH:mm:ss"),
                           "dur":      (e.ongoing ? qsTr("going on now") + " · " : "") + qsTr("%1 s").arg(durSec),
                           "title":    loss ? qsTr("No replies from the router: %1 pings in a row").arg(e.count)
                                            : qsTr("Slow replies from the router: up to %1 ms").arg(e.maxMs),
                           "details":  qsTr("Measured by NX on this computer, after the server"),
                           "severity": loss || e.maxMs >= 1500 ? "error" : "warn"
                       })
        }
        fillModel(items)
        if (items.length === 0) {
            setStatus(LinkQualityMonitor.pingMs < 0 && LinkQualityMonitor.monitoring ? "error" : "ok",
                      statusText, qsTr("No losses since NX was started"))
        } else {
            statusDetails = qsTr("%1 events since NX was started").arg(items.length)
        }
    }

    function fillModel(items) {
        var sig = JSON.stringify(items)
        if (sig === shownSignature) {
            return
        }
        shownSignature = sig
        logModel.clear()
        for (var i = 0; i < items.length; ++i) {
            logModel.append(items[i])
        }
    }

    function switchSource(s) {
        source = s
        shownSignature = ""
        logModel.clear()
        refresh()
    }

    Component.onCompleted: refresh()
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
                text:       qsTr("Server")
                primary:    root.source === "server"
                onClicked:  root.switchSource("server")
            }
            QGCButton {
                text:       qsTr("NX (this computer)")
                primary:    root.source === "nx"
                onClicked:  root.switchSource("nx")
            }
            QGCComboBox {
                id:                     periodBox
                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 14
                visible:                root.source === "server"
                model:                  root._periods
                textRole:               "label"
                currentIndex:           2
                onActivated:            { root.shownSignature = ""; root.refresh() }
            }
            QGCButton {
                text:       qsTr("Refresh")
                enabled:    !root.busy
                onClicked:  root.refresh()
            }
            Item { Layout.fillWidth: true }
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

        QGCLabel {
            Layout.fillWidth:   true
            visible:            text.length > 0
            text:               root.statusDetails
            wrapMode:           Text.Wrap
            opacity:            0.8
        }

        Repeater {
            model: root.cameraLines
            QGCLabel {
                required property string modelData
                Layout.fillWidth:   true
                text:               modelData
                wrapMode:           Text.Wrap
                opacity:            0.8
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            logModel.count === 0 && root.statusKind !== "error"
            text:               root.source === "server" ? qsTr("No problems for the period") : ""
            opacity:            0.7
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
