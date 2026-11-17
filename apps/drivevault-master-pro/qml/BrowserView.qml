import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import DriveVault.Master.Backend

// The browser of the program (Qt WebEngine = Chromium). The user browses here like in any browser: address bar, back / forward / reload, and permissions that work
// like Chrome's - a site that wants the camera, the microphone, the location, notifications or the clipboard asks in a bar under the address bar (Allow / Block,
// remembered on this PC), and the lock button lists what a site may do and lets the user change or reset it. The AI uses the SAME view (Ai.browser = BrowserBridge):
// it can open a page of its work, read it and take a picture of it, but it cannot answer a permission question - only the user can.
// Downloads are not supported yet (a download is refused and the bar says so).
Item {
    id: root
    objectName: "browserPage"

    property var bridge: Ai.browser
    signal closeRequested()

    // ---- state ---------------------------------------------------------------------------------------------------------------------------
    property var pending: []                 // permission requests that wait for the user (WebEnginePermission values), oldest first
    property string notice: ""               // a short line under the address bar (downloads are refused, ...)
    property var consoleErrors: []           // errors the page logged since the last open (for the AI)
    property var failedLoads: []
    property int openId: 0                   // the AI command (id) that waits for a page to load, 0 = none
    readonly property bool loading: view.loading
    readonly property bool isStart: view.url.toString() === "" || view.url.toString() === "about:blank"      // nothing is open: the start page is shown

    // what each kind of permission is called (the enum values of QWebEnginePermission::PermissionType)
    readonly property var permissionTypes: [
        { type: WebEnginePermission.PermissionType.MediaAudioCapture, key: "perm_mic" },
        { type: WebEnginePermission.PermissionType.MediaVideoCapture, key: "perm_camera" },
        { type: WebEnginePermission.PermissionType.MediaAudioVideoCapture, key: "perm_camera_mic" },
        { type: WebEnginePermission.PermissionType.DesktopVideoCapture, key: "perm_screen" },
        { type: WebEnginePermission.PermissionType.DesktopAudioVideoCapture, key: "perm_screen_audio" },
        { type: WebEnginePermission.PermissionType.Geolocation, key: "perm_location" },
        { type: WebEnginePermission.PermissionType.Notifications, key: "perm_notifications" },
        { type: WebEnginePermission.PermissionType.ClipboardReadWrite, key: "perm_clipboard" },
        { type: WebEnginePermission.PermissionType.MouseLock, key: "perm_mouselock" },
        { type: WebEnginePermission.PermissionType.LocalFontsAccess, key: "perm_fonts" }
    ]
    function permissionKey(type) {
        for (const p of permissionTypes)
            if (p.type === type)
                return p.key
        return "perm_other"
    }
    function hostOf(url) {
        const s = url.toString()
        if (s.indexOf("file:") === 0)
            return Tr.s("br_local_file")
        const m = s.match(/^[a-z]+:\/\/([^\/?#]+)/i)
        return m ? m[1] : s
    }
    // what is typed in the address bar: an address, a host name, or words to search for
    function resolveInput(text) {
        const t = text.trim()
        if (t === "")
            return ""
        if (/^[a-z][a-z0-9+.-]*:\/\//i.test(t) || /^(file|about|data):/i.test(t))
            return t
        if (/^[^\s]+\.[^\s]{2,}(\/.*)?$/.test(t) || /^localhost(:\d+)?(\/.*)?$/i.test(t) || /^\d{1,3}(\.\d{1,3}){3}(:\d+)?(\/.*)?$/.test(t))
            return (/^(localhost|\d)/i.test(t) ? "http://" : "https://") + t
        return "https://duckduckgo.com/?q=" + encodeURIComponent(t)
    }
    function go(text) {
        const u = resolveInput(text)
        if (u !== "")
            view.url = u
    }

    WebEngineProfile {
        id: profile
        storageName: "drivevault-browser"
        persistentStoragePath: root.bridge.storagePath !== "" ? root.bridge.storagePath + "/profile" : ""
        cachePath: root.bridge.storagePath !== "" ? root.bridge.storagePath + "/cache" : ""
        persistentPermissionsPolicy: WebEngineProfile.StoreOnDisk       // what the user allowed or blocked is remembered, like in Chrome
        offTheRecord: root.bridge.storagePath === ""
        onDownloadRequested: download => {
            download.cancel()
            root.notice = Tr.s("br_no_downloads")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- the tabs of the program ------------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: "#17130E"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 14
                Text { text: Tr.s("tab_browser"); font.pixelSize: 16; font.weight: Font.DemiBold; color: Theme.text }
                Item { Layout.fillWidth: true }
                MButton { objectName: "brClose"; compact: true; text: "✕  " + Tr.s("br_close"); onClicked: root.closeRequested(); Accessible.name: Tr.s("br_close") }
            }
        }

        // ---- address bar ----------------------------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: Theme.backgroundAlt
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8
                MButton { objectName: "brBack"; variant: "ghost"; compact: true; text: "‹"; font.pixelSize: 18; enabled: view.canGoBack; onClicked: view.goBack(); Accessible.name: Tr.s("br_back") }
                MButton { objectName: "brForward"; variant: "ghost"; compact: true; text: "›"; font.pixelSize: 18; enabled: view.canGoForward; onClicked: view.goForward(); Accessible.name: Tr.s("br_forward") }
                MButton {
                    objectName: "brReload"
                    variant: "ghost"
                    compact: true
                    text: view.loading ? "✕" : "↻"
                    onClicked: view.loading ? view.stop() : view.reload()
                    Accessible.name: Tr.s(view.loading ? "br_stop" : "br_reload")
                }
                // the lock: what this site may do (permissions) - like the site information of Chrome
                MButton {
                    id: siteButton
                    objectName: "brSite"
                    variant: "secondary"
                    compact: true
                    text: (view.url.toString().indexOf("https:") === 0 ? "🔒 " : "ⓘ ") + root.hostOf(view.url)
                    visible: !root.isStart
                    Layout.maximumWidth: 240
                    onClicked: { siteMenu.refresh(); siteMenu.open() }
                    Accessible.name: Tr.s("br_site_settings")
                }
                MField {
                    id: addressField
                    objectName: "brAddress"
                    Layout.fillWidth: true
                    placeholderText: Tr.s("br_address_placeholder")
                    text: root.isStart ? "" : view.url.toString()
                    onAccepted: { root.go(text); view.forceActiveFocus() }
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    Connections {
                        target: view
                        function onUrlChanged() { if (!addressField.activeFocus) addressField.text = root.isStart ? "" : view.url.toString() }
                    }
                }
                MButton { objectName: "brAllSites"; variant: "ghost"; compact: true; text: Tr.s("br_all_sites"); onClicked: allSites.show() }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            Rectangle {                                                       // load progress
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width * view.loadProgress / 100
                visible: view.loading
                color: Theme.accent
            }
        }

        // ---- the permission question of a site (Chrome shows it under the address bar, too) ---------------------------------------------
        Rectangle {
            id: askBar
            objectName: "brPermissionBar"
            readonly property var current: root.pending.length > 0 ? root.pending[0] : null
            visible: current !== null
            Layout.fillWidth: true
            implicitHeight: visible ? 52 : 0
            color: "#2A2110"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.warning; opacity: 0.5 }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12
                Text { text: "🛡"; font.pixelSize: 18; color: Theme.warning }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    color: Theme.text
                    text: askBar.current ? Tr.s("br_wants").replace("%1", root.hostOf(askBar.current.origin)).replace("%2", Tr.s(root.permissionKey(askBar.current.permissionType))) : ""
                }
                MButton { objectName: "brAllow"; variant: "primary"; compact: true; text: Tr.s("br_allow"); onClicked: root.answer(true) }
                MButton { objectName: "brBlock"; variant: "secondary"; compact: true; text: Tr.s("br_block"); onClicked: root.answer(false) }
                MButton { objectName: "brLater"; variant: "ghost"; compact: true; text: "✕"; onClicked: root.dismissAsk(); Accessible.name: Tr.s("br_dismiss") }
            }
        }
        // a short line: downloads are refused, ...
        Rectangle {
            visible: root.notice !== ""
            Layout.fillWidth: true
            implicitHeight: visible ? 34 : 0
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                Text { Layout.fillWidth: true; text: root.notice; font.pixelSize: 12; color: Theme.textSecondary; elide: Text.ElideRight }
                MButton { variant: "ghost"; compact: true; text: "✕"; onClicked: root.notice = "" }
            }
        }
        // the AI is using this view (it opened or looks at a page): the user sees what it sees
        Rectangle {
            visible: root.aiBusy
            Layout.fillWidth: true
            implicitHeight: visible ? 30 : 0
            color: "#17130E"
            Text { anchors.verticalCenter: parent.verticalCenter; x: 16; text: Tr.s("br_ai_using"); font.pixelSize: 12; color: Theme.accent }
        }

        WebEngineView {
            id: view
            objectName: "brView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            profile: profile
            url: "about:blank"
            settings.javascriptCanOpenWindows: true
            settings.fullScreenSupportEnabled: false

            // the start page (nothing open yet): a native page in the style of the program instead of a white sheet
            Rectangle {
                objectName: "brStartPage"
                z: 10                                                     // above the web content (the white sheet of an empty page)
                anchors.fill: parent
                visible: root.isStart
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#17130E" }
                    GradientStop { position: 1.0; color: Theme.background }
                }
                Flickable {
                    anchors.fill: parent
                    contentHeight: startColumn.implicitHeight + 80
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    ColumnLayout {
                        id: startColumn
                        width: Math.min(parent.width - 48, 720)
                        x: (parent.width - width) / 2
                        y: Math.max(40, (parent.height - implicitHeight) / 3)
                        spacing: 22
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 72
                            height: 72
                            radius: 22
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.lighter(Theme.accent, 1.2) }
                                GradientStop { position: 1.0; color: Qt.darker(Theme.accent, 1.25) }
                            }
                            Text { anchors.centerIn: parent; text: "🌐"; font.pixelSize: 34 }
                        }
                        Text { Layout.alignment: Qt.AlignHCenter; text: Tr.s("br_start_title"); font.pixelSize: 30; font.weight: Font.Bold; color: Theme.text }
                        Text { Layout.alignment: Qt.AlignHCenter; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                               text: Tr.s("br_start_sub"); font.pixelSize: 14; color: Theme.textSecondary }
                        MField {
                            id: startSearch
                            objectName: "brStartSearch"
                            Layout.fillWidth: true
                            implicitHeight: 52
                            font.pixelSize: 15
                            leftPadding: 20
                            placeholderText: Tr.s("br_address_placeholder")
                            onAccepted: root.go(text)
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 560 ? 3 : 2
                            columnSpacing: 12
                            rowSpacing: 12
                            Repeater {
                                model: [ { t: "DuckDuckGo", u: "https://duckduckgo.com", i: "🔎" }, { t: "Wikipedia", u: "https://www.wikipedia.org", i: "📚" },
                                         { t: "GitHub", u: "https://github.com", i: "🐙" }, { t: "OpenStreetMap", u: "https://www.openstreetmap.org", i: "🗺" },
                                         { t: "MDN Web Docs", u: "https://developer.mozilla.org", i: "🧩" }, { t: "Qt", u: "https://doc.qt.io", i: "📘" } ]
                                delegate: Rectangle {
                                    id: tile
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 64
                                    radius: 14
                                    color: tileMouse.containsMouse ? Theme.surfaceHover : Theme.surface
                                    border.width: 1
                                    border.color: tileMouse.containsMouse ? Theme.accent : Theme.border
                                    Behavior on color { ColorAnimation { duration: 140 } }
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 10
                                        Text { text: tile.modelData.i; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
                                        Text { text: tile.modelData.t; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                    MouseArea { id: tileMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.go(tile.modelData.u) }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: Tr.s("br_start_privacy")
                            font.pixelSize: 12
                            color: Theme.textDisabled
                        }
                    }
                }
            }

            onPermissionRequested: permission => {                if (permission.state === WebEnginePermission.State.Granted || permission.state === WebEnginePermission.State.Denied) {
                    permission.state === WebEnginePermission.State.Granted ? permission.grant() : permission.deny()   // already decided by the user earlier
                    return
                }
                const p = root.pending.slice()
                p.push(permission)
                root.pending = p                                              // the bar asks; nothing is granted before the user answers
            }
            onNewWindowRequested: request => { view.url = request.requestedUrl }   // no pop-up windows: the page opens here
            onJavaScriptConsoleMessage: (level, message, lineNumber, sourceID) => {
                if (level === WebEngineView.ErrorMessageLevel) {
                    const e = root.consoleErrors.slice(0, 19)
                    e.push((sourceID ? sourceID.split("/").pop() + ":" + lineNumber + " " : "") + message)
                    root.consoleErrors = e
                }
            }
            onLoadingChanged: loadingInfo => {
                if (loadingInfo.status === WebEngineView.LoadStartedStatus) {
                    root.consoleErrors = []
                } else if (loadingInfo.status === WebEngineView.LoadFailedStatus) {
                    if (root.openId !== 0)
                        root.finishOpen(false, loadingInfo.errorString)
                } else if (loadingInfo.status === WebEngineView.LoadSucceededStatus) {
                    if (root.openId !== 0)
                        settle.restart()
                }
            }
        }
    }

    // ---- permission answers ----------------------------------------------------------------------------------------------------------------
    function answer(allow) {
        if (root.pending.length === 0)
            return
        const p = root.pending[0]
        if (allow)
            p.grant()
        else
            p.deny()
        root.pending = root.pending.slice(1)
    }
    function dismissAsk() {                                                   // asked again the next time (nothing is remembered)
        root.pending = root.pending.slice(1)
    }

    // ---- the lock: permissions of the site that is open ------------------------------------------------------------------------------------
    Popup {
        id: siteMenu
        objectName: "brSiteMenu"
        x: 120
        y: 116
        width: 420
        padding: 16
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property var rows: []
        function refresh() {
            const r = []
            for (const p of root.permissionTypes) {
                const q = profile.queryPermission(view.url, p.type)
                r.push({ type: p.type, key: p.key, state: q.state, permission: q })
            }
            rows = r
        }
        background: Rectangle { radius: 14; color: Theme.surface; border.width: 1; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: root.hostOf(view.url); font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
            Text { text: Tr.s("br_site_what"); font.pixelSize: 12; color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Repeater {
                model: siteMenu.rows
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Text { Layout.fillWidth: true; text: Tr.s(modelData.key); font.pixelSize: 13; color: Theme.text }
                    PermissionChoice {
                        permission: modelData.permission
                        shown: modelData.state
                        onModified: siteMenu.refresh()
                    }
                }
            }
            MButton {
                objectName: "brResetSite"
                Layout.alignment: Qt.AlignRight
                compact: true
                text: Tr.s("br_reset_site")
                onClicked: { for (const r of siteMenu.rows) r.permission.reset(); siteMenu.refresh() }
            }
        }
    }

    // Ask | Allow | Block for one permission (a row of the lists)
    component PermissionChoice: Row {
        id: choice
        property var permission
        property int shown: WebEnginePermission.State.Ask
        signal modified()
        spacing: 4
        Repeater {
            model: [ { key: "br_ask", st: WebEnginePermission.State.Ask }, { key: "br_allow", st: WebEnginePermission.State.Granted }, { key: "br_block", st: WebEnginePermission.State.Denied } ]
            delegate: MButton {
                required property var modelData
                compact: true
                variant: choice.shown === modelData.st || (modelData.st === WebEnginePermission.State.Ask && choice.shown === WebEnginePermission.State.Invalid) ? "primary" : "secondary"
                text: Tr.s(modelData.key)
                onClicked: {
                    if (modelData.st === WebEnginePermission.State.Granted) choice.permission.grant()
                    else if (modelData.st === WebEnginePermission.State.Denied) choice.permission.deny()
                    else choice.permission.reset()
                    choice.modified()
                }
            }
        }
    }

    // ---- all sites that have something remembered (Chrome: Settings > Privacy > Site settings) ---------------------------------------------------
    Popup {
        id: allSites
        objectName: "brAllSitesDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 640)
        height: Math.min(root.height - 48, 520)
        padding: 18
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property var items: []
        function show() { items = profile.listAllPermissions(); open() }
        background: Rectangle { radius: 14; color: Theme.surface; border.width: 1; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text { text: Tr.s("br_all_sites_title"); font.pixelSize: 16; font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true }
                MButton { compact: true; variant: "ghost"; text: "✕"; onClicked: allSites.close() }
            }
            Text { text: Tr.s("br_all_sites_help"); font.pixelSize: 12; color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Text { visible: allSites.items.length === 0; text: Tr.s("br_all_sites_none"); font.pixelSize: 13; color: Theme.textDisabled }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: allSites.items
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width - 10
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: root.hostOf(modelData.origin); font.pixelSize: 13; font.weight: Font.Medium; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: Tr.s(root.permissionKey(modelData.permissionType)); font.pixelSize: 12; color: Theme.textSecondary }
                    }
                    PermissionChoice { permission: modelData; shown: modelData.state; onModified: allSites.items = profile.listAllPermissions() }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }

    // ---- commands of the AI (BrowserBridge): open | read | look ---------------------------------------------------------------------------
    property bool aiBusy: false
    Timer { id: busyOff; interval: 4000; onTriggered: root.aiBusy = false }

    // the page is "ready" a moment after the load ended (fonts, images, scripts that build the page)
    Timer { id: settle; interval: 900; onTriggered: root.finishOpen(true, "") }
    Timer { id: openLimit; interval: 25000; onTriggered: if (root.openId !== 0) root.finishOpen(true, "", true) }
    function finishOpen(ok, error, slow) {
        const id = root.openId
        root.openId = 0
        settle.stop()
        openLimit.stop()
        if (id === 0)
            return
        if (!ok) {
            root.bridge.reply(id, { ok: false, error: "The page could not be loaded: " + error })
            return
        }
        root.bridge.reply(id, { ok: true, text: "Opened " + view.url + " (title: " + view.title + ")" + (slow ? ". The page was still loading after 25 seconds." : ".") })
    }

    // fixed scripts only: the AI never sends code into a page
    readonly property string readScript: "(function(){var b=document.body;var t=b?b.innerText:'';var imgs=[].slice.call(document.images);"
        + "var bad=imgs.filter(function(i){return i.complete&&i.naturalWidth===0}).map(function(i){return i.getAttribute('src')||''}).slice(0,10);"
        + "var css=[].slice.call(document.querySelectorAll('link[rel=stylesheet]')).filter(function(l){try{return l.sheet===null||l.sheet.cssRules.length===0}catch(e){return false}}).map(function(l){return l.getAttribute('href')||''}).slice(0,10);"
        + "var hs=[].slice.call(document.querySelectorAll('h1,h2,h3')).map(function(h){return h.tagName+': '+h.innerText.trim().slice(0,80)}).slice(0,25);"
        + "var cs=b?getComputedStyle(b):null;"
        + "return JSON.stringify({title:document.title,text:t.slice(0,4000),textLength:t.length,headings:hs,sections:document.querySelectorAll('section,header,footer,nav,main,article').length,"
        + "images:imgs.length,brokenImages:bad,badStylesheets:css,links:document.links.length,buttons:document.querySelectorAll('button,input[type=submit],a.btn').length,forms:document.forms.length,"
        + "scripts:document.scripts.length,styleSheets:document.styleSheets.length,inlineStyles:document.querySelectorAll('[style]').length,pageHeight:document.documentElement.scrollHeight,"
        + "viewport:window.innerHeight,viewportWidth:window.innerWidth,horizontalScroll:document.documentElement.scrollWidth>window.innerWidth+1,font:cs?cs.fontFamily:'',background:cs?cs.backgroundColor:''})})()"
    readonly property string posScript: "JSON.stringify({y:Math.round(window.scrollY),h:document.documentElement.scrollHeight,v:window.innerHeight})"

    function describeRead(r) {
        const o = JSON.parse(r)
        let s = "Page: " + o.title + " (" + view.url + ")\n"
        s += "Size: " + o.viewportWidth + " px wide window, the page is " + o.pageHeight + " px high" + (o.horizontalScroll ? " and scrolls sideways (something is wider than the window)" : "") + ".\n"
        s += "Counts: " + o.headings.length + " headings, " + o.sections + " sections (header, footer, nav, main, section, article), " + o.images + " images, " + o.links + " links, "
             + o.buttons + " buttons, " + o.forms + " forms, " + o.scripts + " scripts.\n"
        if (o.styleSheets === 0 && o.inlineStyles === 0)
            s += "WARNING: the page has NO styles at all (no stylesheet, no style attribute): it shows as plain unstyled text. Add the CSS.\n"
        else
            s += "Styles: " + o.styleSheets + " stylesheets, " + o.inlineStyles + " elements with a style attribute. Body font: " + o.font + ", background " + o.background + ".\n"
        if (o.badStylesheets.length > 0)
            s += "WARNING: stylesheets that did not load: " + o.badStylesheets.join(", ") + "\n"
        if (o.brokenImages.length > 0)
            s += "WARNING: images that did not load: " + o.brokenImages.join(", ") + "\n"
        if (root.consoleErrors.length > 0)
            s += "Errors the page logged:\n- " + root.consoleErrors.join("\n- ") + "\n"
        if (o.headings.length > 0)
            s += "Headings:\n" + o.headings.join("\n") + "\n"
        s += "Visible text (" + o.textLength + " characters" + (o.textLength > 4000 ? ", cut at 4000" : "") + "):\n" + o.text
        return s
    }

    function run(id, op, args) {
        root.aiBusy = true
        busyOff.restart()
        if (op === "open") {
            root.openId = id
            openLimit.restart()
            if (view.url.toString() === args.url)
                view.reload()
            else
                view.url = args.url
        } else if (op === "read") {
            view.runJavaScript(root.readScript, function(r) {
                if (r === undefined || r === null || r === "")
                    root.bridge.reply(id, { ok: false, error: "no page is open yet: use browser_open first" })
                else
                    root.bridge.reply(id, { ok: true, text: root.describeRead(r) })
            })
        } else if (op === "look") {
            view.runJavaScript(root.posScript, function(p) {
                if (p === undefined || p === null) {
                    root.bridge.reply(id, { ok: false, error: "no page is open yet: use browser_open first" })
                    return
                }
                const pos = JSON.parse(p)
                let y = pos.y
                const step = Math.max(200, Math.round(pos.v * 0.9))
                if (args.scroll === "top") y = 0
                else if (args.scroll === "bottom") y = Math.max(0, pos.h - pos.v)
                else if (args.scroll === "up") y = Math.max(0, pos.y - step)
                else y = Math.min(Math.max(0, pos.h - pos.v), pos.y + step)
                view.runJavaScript("window.scrollTo(0," + y + ");" + root.posScript, function(q) {
                    lookDelay.pendingId = id
                    lookDelay.pos = JSON.parse(q)
                    lookDelay.restart()
                })
            })
        }
    }
    Timer {
        id: lookDelay
        interval: 450                                                        // the page paints the new part
        property int pendingId: 0
        property var pos: ({ y: 0, h: 0, v: 0 })
        onTriggered: {
            const id = pendingId
            pendingId = 0
            view.grabToImage(function(result) {
                const data = root.bridge.encodeImage(result.image, 1024)
                if (data === "") {
                    root.bridge.reply(id, { ok: false, error: "the picture of the page could not be taken" })
                    return
                }
                const last = Math.max(1, Math.ceil(lookDelay.pos.h / Math.max(1, lookDelay.pos.v)))
                const here = Math.min(last, Math.floor(lookDelay.pos.y / Math.max(1, lookDelay.pos.v)) + 1)
                root.bridge.reply(id, { ok: true, image: data,
                                        text: "Picture of the page, part " + here + " of about " + last + " (from " + lookDelay.pos.y + " px of " + lookDelay.pos.h + " px). "
                                              + (lookDelay.pos.y + lookDelay.pos.v >= lookDelay.pos.h - 2 ? "This is the end of the page." : "Use browser_look with scroll down for the next part.") })
            })
        }
    }
    Connections {
        target: root.bridge
        function onCommand(id, op, args) { root.run(id, op, args) }
    }
    Component.onCompleted: root.bridge.viewReady = true
    Component.onDestruction: root.bridge.viewReady = false
}
