import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import DriveVault.Master.Backend

// The browser of the program (Qt WebEngine = Chromium). The user browses here like in any browser: tabs, address bar, back / forward / reload, and permissions that work
// like Chrome's - a site that wants the camera, the microphone, the location, notifications or the clipboard asks in a bar under the address bar (Allow / Block,
// remembered on this PC), and the lock button lists what a site may do and lets the user change or reset it. The AI uses the SAME views (Ai.browser = BrowserBridge):
// it opens the page of its work (a tab of its own that FOLLOWS the files while the AI writes them), reads it, takes a picture of it and works the page with a visible
// cursor - click, type, scroll, drag - but only in pages of this computer; it cannot answer a permission question - only the user can.
// Downloads (1.28.0): a file the user's click started is saved in the Downloads folder (never overwriting a file); the bar says so and has "Show in folder".
Item {
    id: root
    objectName: "browserPage"

    property var bridge: Ai.browser
    signal closeRequested()
    signal expandToggled()
    property bool expanded: false

    // ---- state ---------------------------------------------------------------------------------------------------------------------------
    property var pending: []                 // permission requests that wait for the user (WebEnginePermission values), oldest first
    property string notice: ""               // a short line under the address bar (a download started / finished, ...)
    property string noticeFolder: ""         // when set, the bar has a "Show in folder" button for this folder
    // the AI tests a page at other screen sizes (browser_viewport): every view is then given this size (0 = the pane's own size), scaled down to fit the pane
    property int vpW: 0
    property int vpH: 0
    property string vpName: ""
    function setViewport(w, h, name) { vpW = w; vpH = h; vpName = name }
    property var fsView: null               // the web view that shows a page full screen (a video, a game), null = none
    property Item fsParent: null
    property int fsVisibility: Window.Windowed
    property int openId: 0                   // the AI command (id) that waits for a page to load, 0 = none
    property int openTab: -1                 // ... and the tab that loads it
    property int cur: 0                      // the tab that is shown
    // the tab that is shown (a WebEngineView); null only before the first tab exists
    readonly property var view: views.count > 0 && cur >= 0 && cur < views.count ? views.itemAt(cur) : null
    readonly property string curUrl: view ? view.url.toString() : ""
    readonly property bool loading: view ? view.loading : false
    readonly property bool isStart: curUrl === "" || curUrl === "about:blank"      // nothing is open in this tab: the start page is shown

    // a page asked for full screen (the full screen button of a video): its view fills the whole window until it asks to leave or the user presses Esc
    function setFullscreen(on, v) {
        const win = root.Window.window
        if (!win)
            return
        if (on && !fsView) {
            fsView = v
            fsParent = v.parent
            fsVisibility = win.visibility
            v.parent = win.contentItem
            v.z = 100000
            win.visibility = Window.FullScreen
        } else if (!on && fsView) {
            fsView.parent = fsParent
            fsView.z = 0
            win.visibility = fsVisibility === Window.FullScreen ? Window.Windowed : fsVisibility
            fsView = null
        }
    }
    Shortcut { sequence: "Esc"; enabled: root.fsView !== null; onActivated: root.fsView.fullScreenCancelled() }

    ListModel { id: tabs }                   // one row per tab: {title, address, busy, agent}

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
        if (u !== "" && view)
            view.url = u
    }

    // ---- tabs --------------------------------------------------------------------------------------------------------------------------------
    function newTab(address, activate, agent) {
        tabs.append({ title: "", address: address || "about:blank", busy: false, agent: agent === true })
        if (activate !== false)
            cur = tabs.count - 1
        return tabs.count - 1
    }
    function closeTab(i) {
        if (i < 0 || i >= tabs.count)
            return
        if (tabs.count === 1) {                                              // the last tab is never removed: it goes back to the start page
            views.itemAt(0).url = "about:blank"
            tabs.setProperty(0, "agent", false)
            return
        }
        const was = cur
        tabs.remove(i)
        cur = Math.max(0, Math.min(tabs.count - 1, i < was ? was - 1 : (i === was ? Math.min(was, tabs.count - 1) : was)))
    }
    function indexOfUrl(address) {
        for (let i = 0; i < views.count; ++i)
            if (views.itemAt(i) && views.itemAt(i).url.toString() === address)
                return i
        return -1
    }
    // the page the AI wrote: shown in a tab of its own, which follows the file (see liveFilesChanged)
    function showPreview(address) {
        if (indexOfUrl(address) >= 0)
            return
        for (let i = 0; i < tabs.count; ++i)
            if (tabs.get(i).agent === true) {                                // the AI's tab: it goes to the new page
                views.itemAt(i).url = address
                cur = i
                return
            }
        for (let i = 0; i < tabs.count; ++i)
            if (views.itemAt(i).url.toString() === "about:blank") {          // an empty tab is used before a new one is made
                views.itemAt(i).url = address
                tabs.setProperty(i, "agent", true)
                cur = i
                return
            }
        newTab(address, true, true)
    }
    function watchTab(i) {
        const f = bridge.toLocalFile(views.itemAt(i).url.toString())
        if (f !== "")
            bridge.watchFolder(f.replace(/[\\\/][^\\\/]*$/, ""))
    }
    Connections {
        target: root.bridge
        function onLiveFilesChanged(folder) {                                // a file of the page changed (the AI is writing): the tabs that show a page of this folder reload
            const dir = folder.replace(/\\/g, "/").toLowerCase()
            for (let i = 0; i < views.count; ++i) {
                const f = root.bridge.toLocalFile(views.itemAt(i).url.toString()).replace(/\\/g, "/").toLowerCase()
                if (f !== "" && f.indexOf(dir + "/") === 0)
                    views.itemAt(i).reload()
            }
        }
    }




    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- the tabs, with the expand and close buttons at the end (no title row: the page gets the room) ------------------------------
        Rectangle {
            objectName: "brTabs"
            Layout.fillWidth: true
            implicitHeight: 44
            color: "#0D0E11"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                anchors.topMargin: 6
                spacing: 4
                ListView {
                    id: tabStrip
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    spacing: 3
                    clip: true
                    model: tabs
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Item {
                        id: tab
                        required property int index
                        required property string title
                        required property string address
                        required property bool busy
                        required property bool agent
                        readonly property bool active: index === root.cur
                        width: Math.max(96, Math.min(210, (tabStrip.width - 8) / Math.max(1, tabs.count)))
                        height: tabStrip.height
                        // a tab like the tabs of a browser: round at the top, the shown one is the colour of the bar under it
                        Rectangle {
                            anchors.fill: parent
                            anchors.bottomMargin: -12
                            radius: 10
                            color: tab.active ? Theme.backgroundAlt : tabMouse.containsMouse ? "#17191F" : "transparent"
                        }
                        Rectangle { visible: tab.active; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 2; radius: 1; color: Theme.accent }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 6
                            spacing: 6
                            Text { visible: tab.agent; text: "✦"; font.pixelSize: 11; color: Theme.accent }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: (tab.busy ? "… " : "") + (tab.title !== "" ? tab.title : (tab.address === "about:blank" ? Tr.s("br_new_tab") : root.hostOf(tab.address)))
                                font.pixelSize: 12
                                color: tab.active ? Theme.text : Theme.textSecondary
                            }
                            Item {
                                implicitWidth: 18; implicitHeight: 18
                                Rectangle { anchors.fill: parent; radius: 9; color: closeMouse.containsMouse ? Theme.surfaceHover : "transparent" }
                                LineIcon { anchors.centerIn: parent; size: 10; kind: "close"; color: closeMouse.containsMouse ? Theme.text : Theme.textDisabled }
                                MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeTab(tab.index) }
                            }
                        }
                        MouseArea { id: tabMouse; anchors.fill: parent; z: -1; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.cur = tab.index }
                    }
                }
                IconButton { objectName: "brNewTab"; box: 28; kind: "plus"; tip: Tr.s("br_new_tab"); onClicked: root.newTab("about:blank", true, false) }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: Theme.border }
                IconButton { objectName: "brExpand"; box: 28; kind: root.expanded ? "shrink" : "expand"; tip: Tr.s(root.expanded ? "br_shrink" : "br_expand"); onClicked: root.expandToggled() }
                IconButton { objectName: "brClose"; box: 28; kind: "close"; tip: Tr.s("br_close"); onClicked: root.closeRequested() }
            }
        }

        // ---- address bar ----------------------------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 50
            color: Theme.backgroundAlt
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 2
                IconButton { objectName: "brBack"; box: 30; kind: "back"; tip: Tr.s("br_back"); enabled: root.view ? root.view.canGoBack : false; onClicked: root.view.goBack() }
                IconButton { objectName: "brForward"; box: 30; kind: "forward"; tip: Tr.s("br_forward"); enabled: root.view ? root.view.canGoForward : false; onClicked: root.view.goForward() }
                IconButton { objectName: "brReload"; box: 30; kind: root.loading ? "stop" : "reload"; tip: Tr.s(root.loading ? "br_stop" : "br_reload"); onClicked: root.loading ? root.view.stop() : root.view.reload() }
                Item { Layout.preferredWidth: 4 }
                // the address as one rounded field: the lock (what this site may do - like the site information of Chrome) and the address
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: 17
                    color: Theme.background
                    border.width: addressField.activeFocus ? 2 : 1
                    border.color: addressField.activeFocus ? Theme.accent : Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 12
                        spacing: 4
                        IconButton {
                            id: siteButton
                            objectName: "brSite"
                            box: 26
                            visible: !root.isStart
                            kind: root.curUrl.indexOf("https:") === 0 ? "lock" : "info"
                            tip: Tr.s("br_site_settings") + " - " + root.hostOf(root.curUrl)
                            onClicked: { siteMenu.refresh(); siteMenu.open() }
                        }
                        TextField {
                            id: addressField
                            objectName: "brAddress"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            placeholderText: Tr.s("br_address_placeholder")
                            placeholderTextColor: Theme.textDisabled
                            color: Theme.text
                            font.pixelSize: 13
                            selectByMouse: true
                            leftPadding: root.isStart ? 10 : 2
                            verticalAlignment: TextInput.AlignVCenter
                            background: null
                            text: root.isStart ? "" : root.curUrl
                            onAccepted: { root.go(text); if (root.view) root.view.forceActiveFocus() }
                            onActiveFocusChanged: if (activeFocus) selectAll()
                            Connections {
                                target: root
                                function onCurUrlChanged() { if (!addressField.activeFocus) addressField.text = root.isStart ? "" : root.curUrl }
                            }
                        }
                    }
                }
                IconButton { objectName: "brAllSites"; box: 30; kind: "sites"; tip: Tr.s("br_all_sites"); onClicked: allSites.show() }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            Rectangle {                                                       // load progress
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width * (root.view ? root.view.loadProgress : 0) / 100
                visible: root.loading
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
                MButton { objectName: "brShowFolder"; visible: root.noticeFolder !== ""; variant: "ghost"; compact: true; text: Tr.s("br_show_folder"); onClicked: Qt.openUrlExternally("file:///" + root.noticeFolder) }
                MButton { variant: "ghost"; compact: true; text: "✕"; onClicked: { root.notice = ""; root.noticeFolder = "" } }
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

        // the AI tests a screen size: which one, and a cross to end the test. A bar of its own above the pages - nothing is drawn over the page, so no click of the AI or the user lands on it
        Rectangle {
            objectName: "brViewportBar"
            visible: root.vpW > 0 && root.fsView === null
            Layout.fillWidth: true
            implicitHeight: visible ? 30 : 0
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: Theme.text
                    text: Tr.s("br_viewport_bar").replace("%1", root.vpW + " x " + root.vpH + (root.vpName !== "" ? " (" + root.vpName + ")" : ""))
                          .replace("%2", Math.round(Math.min(1, stage.width / Math.max(1, root.vpW), stage.height / Math.max(1, root.vpH)) * 100))
                }
                MButton { objectName: "brViewportReset"; variant: "ghost"; compact: true; text: "✕"; onClicked: root.setViewport(0, 0, ""); Accessible.name: Tr.s("br_viewport_reset") }
            }
        }
        // ---- the pages: one web view per tab; the AI's cursor and the start page are drawn above them -------------------------------------
        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // while a screen size is tested: the outline of the screen and a chip that says which size it is (the user can end the test with the cross)
            Rectangle {
                visible: root.vpW > 0 && root.fsView === null
                z: -1
                readonly property real fit: Math.min(1, stage.width / Math.max(1, root.vpW), stage.height / Math.max(1, root.vpH))
                width: root.vpW * fit + 2
                height: root.vpH * fit + 2
                x: (stage.width - width) / 2
                y: (stage.height - height) / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.accent
            }

            Repeater {
                id: views
                model: tabs
                delegate: WebEngineView {
                    id: wv
                    required property int index
                    required property string address                            // a delegate with required properties has no `model` object: the roles are required properties
                    // the whole pane, or - while the AI tests a screen size - exactly that size (the page sees it as its window: media queries, innerWidth), centred and scaled to fit
                    readonly property bool emulated: root.vpW > 0 && root.fsView !== wv
                    readonly property real fitScale: emulated ? Math.min(1, parent.width / root.vpW, parent.height / root.vpH) : 1
                    width: emulated ? root.vpW : parent.width
                    height: emulated ? root.vpH : parent.height
                    x: emulated ? (parent.width - width) / 2 : 0
                    y: emulated ? (parent.height - height) / 2 : 0
                    scale: fitScale
                    transformOrigin: Item.Center
                    visible: index === root.cur
                    profile: browserProfile                         // NOT `profile: profile` - inside the view that would be its own property (the default profile of Qt)
                    Component.onCompleted: url = address                        // the first address of the tab (no binding: a redirect must not start a new load)
                    // Everything the engine keeps off by default is on, so a page behaves as in Chrome (what a site may do with the camera, the location ... is still the user's answer in the bar)
                    settings.javascriptCanOpenWindows: true
                    settings.javascriptCanAccessClipboard: true                 // the "copy" buttons of a page
                    settings.javascriptCanPaste: true
                    settings.allowWindowActivationFromJavaScript: true
                    settings.playbackRequiresUserGesture: false                 // a video or a sound may start by itself
                    settings.screenCaptureEnabled: true                         // screen sharing (the user still has to allow it)
                    settings.pluginsEnabled: true
                    settings.pdfViewerEnabled: true                             // a PDF opens in the viewer of the engine
                    settings.dnsPrefetchEnabled: true
                    settings.fullScreenSupportEnabled: true                     // a video / a page may go full screen (Esc leaves it)
                    onFullScreenRequested: request => { request.accept(); root.setFullscreen(request.toggleOn, wv) }
                    settings.localContentCanAccessRemoteUrls: true              // a page on this computer may load its fonts / styles / pictures from the Internet, like in Chrome (Google Fonts: without it the page looked different)
                    property var consoleErrors: []                              // what this page logged since it started loading (for the AI)

                    onUrlChanged: { if (index < tabs.count) tabs.setProperty(index, "address", url.toString()) }
                    onTitleChanged: { if (index < tabs.count) tabs.setProperty(index, "title", title) }
                    onPermissionRequested: permission => {
                        if (permission.state === WebEnginePermission.State.Granted || permission.state === WebEnginePermission.State.Denied) {
                            permission.state === WebEnginePermission.State.Granted ? permission.grant() : permission.deny()   // already decided by the user earlier
                            return
                        }
                        const p = root.pending.slice()
                        p.push(permission)
                        root.pending = p                                          // the bar asks; nothing is granted before the user answers
                    }
                    onNewWindowRequested: request => { root.newTab(request.requestedUrl.toString(), true, false) }   // a link that wants a new window: a new tab
                    onJavaScriptConsoleMessage: (level, message, lineNumber, sourceID) => {
                        if (level === WebEngineView.ErrorMessageLevel) {
                            const e = consoleErrors.slice(0, 19)
                            e.push((sourceID ? sourceID.split("/").pop() + ":" + lineNumber + " " : "") + message)
                            consoleErrors = e
                        }
                    }
                    onLoadingChanged: loadingInfo => {
                        if (index < tabs.count)
                            tabs.setProperty(index, "busy", loading)
                        if (loadingInfo.status === WebEngineView.LoadStartedStatus) {
                            consoleErrors = []
                        } else if (loadingInfo.status === WebEngineView.LoadFailedStatus) {
                            if (root.openId !== 0 && root.openTab === index)
                                root.finishOpen(false, loadingInfo.errorString)
                        } else if (loadingInfo.status === WebEngineView.LoadSucceededStatus) {
                            root.watchTab(index)                                  // a page of this computer: its folder is watched, the tab follows the files
                            if (root.openId !== 0 && root.openTab === index)
                                settle.restart()
                        }
                    }
                }
            }

            // the start page (nothing open in this tab): a native page in the style of the program instead of a white sheet
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

            // the AI's cursor: it moves to the element, presses, and lets go (the page gets real mouse events; this is what the user sees of them)
            Item {
                id: aiCursor
                objectName: "brAiCursor"
                z: 60
                property real tx: stage.width / 2
                property real ty: stage.height / 2
                x: tx
                y: ty
                width: 28
                height: 34
                opacity: 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 260 } }
                Behavior on x { NumberAnimation { id: moveX; duration: 560; easing.type: Easing.InOutCubic } }
                Behavior on y { NumberAnimation { duration: 560; easing.type: Easing.InOutCubic } }
                Rectangle {                                                       // the click: a ring that grows and fades
                    id: ripple
                    width: 10; height: 10; radius: 5
                    x: -5; y: -5
                    color: "transparent"
                    border.width: 3
                    border.color: Theme.accent
                    opacity: 0
                    scale: 1
                    ParallelAnimation {
                        id: rippleAnim
                        NumberAnimation { target: ripple; property: "scale"; from: 1; to: 5; duration: 480; easing.type: Easing.OutCubic }
                        NumberAnimation { target: ripple; property: "opacity"; from: 0.95; to: 0; duration: 480 }
                    }
                }
                Canvas {                                                          // an arrow like a mouse pointer, the tip at (0, 0)
                    width: 28; height: 34
                    onPaint: {
                        const c = getContext("2d")
                        c.reset()
                        c.beginPath()
                        c.moveTo(1, 1); c.lineTo(1, 24); c.lineTo(7, 18); c.lineTo(12, 30); c.lineTo(16, 28); c.lineTo(11, 17); c.lineTo(19, 17); c.closePath()
                        c.fillStyle = "#FFFFFF"
                        c.fill()
                        c.lineWidth = 2
                        c.strokeStyle = "#14161B"
                        c.stroke()
                    }
                }
                Rectangle { x: 20; y: 24; width: pill.implicitWidth + 14; height: 20; radius: 10; color: Theme.accent                    // a small label: this is the AI
                    Text { id: pill; anchors.centerIn: parent; text: "AI"; font.pixelSize: 11; font.weight: Font.Bold; color: Theme.readableOn(Theme.accent) } }
            }
            Timer { id: cursorHide; interval: 4500; onTriggered: aiCursor.opacity = 0 }
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
                const q = browserProfile.queryPermission(root.curUrl, p.type)
                r.push({ type: p.type, key: p.key, state: q.state, permission: q })
            }
            rows = r
        }
        background: Rectangle { radius: 14; color: Theme.surface; border.width: 1; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: root.hostOf(root.curUrl); font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
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
        function show() { items = browserProfile.listAllPermissions(); open() }
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
                    PermissionChoice { permission: modelData; shown: modelData.state; onModified: allSites.items = browserProfile.listAllPermissions() }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }

    // ---- commands of the AI (BrowserBridge): open | read | look | click | type | scroll | drag | tab ------------------------------------------
    property bool aiBusy: false
    Timer { id: busyOff; interval: 4000; onTriggered: root.aiBusy = false }

    // a one-shot timer: later(ms, fn)
    function later(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', root)
        t.interval = ms
        t.triggered.connect(function() { t.destroy(); fn() })
        t.start()
    }

    // the page is "ready" a moment after the load ended (fonts, images, scripts that build the page)
    Timer { id: settle; interval: 900; onTriggered: root.finishOpen(true, "") }
    Timer { id: openLimit; interval: 25000; onTriggered: if (root.openId !== 0) root.finishOpen(true, "", true) }
    function finishOpen(ok, error, slow) {
        const id = root.openId
        const v = views.itemAt(root.openTab)
        root.openId = 0
        settle.stop()
        openLimit.stop()
        if (id === 0)
            return
        if (!ok) {
            root.bridge.reply(id, { ok: false, error: "The page could not be loaded: " + error })
            return
        }
        root.bridge.reply(id, { ok: true, text: "Opened " + (v ? v.url : "") + " (title: " + (v ? v.title : "") + ")" + (slow ? ". The page was still loading after 25 seconds." : ".")
                                                + " It is tab " + (root.openTab + 1) + " of " + tabs.count + "." })
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
    // find an element by CSS selector or by its visible text, scroll it to the middle of the window and measure it (the argument is DATA: a JSON string)
    readonly property string findScript: "(function(t,doScroll){var el=null;try{el=document.querySelector(t)}catch(e){}"
        + "if(!el){var low=String(t).toLowerCase().trim();var all=[].slice.call(document.querySelectorAll('a,button,input,select,textarea,summary,label,[role=button],[onclick],li,h1,h2,h3,h4,p,span,div,img'));"
        + "var txt=function(e){return String(e.innerText||e.value||e.placeholder||e.alt||e.getAttribute('aria-label')||e.title||'').trim().toLowerCase()};"
        + "el=all.find(function(e){return txt(e)===low})||all.find(function(e){return txt(e).indexOf(low)>=0&&e.children.length<4})}"
        + "if(!el)return JSON.stringify({ok:false});"
        + "if(doScroll)el.scrollIntoView({block:'center',inline:'center',behavior:'instant'});"
        + "var r=el.getBoundingClientRect();"
        + "return JSON.stringify({ok:true,x:r.left+r.width/2,y:r.top+r.height/2,w:r.width,h:r.height,tag:el.tagName.toLowerCase(),text:String(el.innerText||el.value||el.alt||'').trim().slice(0,60),href:el.getAttribute('href')||'',"
        + "inside:r.right>0&&r.bottom>0&&r.left<window.innerWidth&&r.top<window.innerHeight})})"
    function find(target, doScroll, cb) {
        root.view.runJavaScript(root.findScript + "(" + JSON.stringify(target) + "," + (doScroll ? "true" : "false") + ")", function(r) {
            let o = { ok: false }
            try { o = JSON.parse(r) } catch (e) {}
            cb(o)
        })
    }

    function describeRead(r) {
        const o = JSON.parse(r)
        let s = "Page: " + o.title + " (" + root.curUrl + ")\n"
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
        const errors = root.view ? root.view.consoleErrors : []
        if (errors.length > 0)
            s += "Errors the page logged:\n- " + errors.join("\n- ") + "\n"
        if (o.headings.length > 0)
            s += "Headings:\n" + o.headings.join("\n") + "\n"
        s += "Visible text (" + o.textLength + " characters" + (o.textLength > 4000 ? ", cut at 4000" : "") + "):\n" + o.text
        return s
    }

    // ---- the cursor and the input it makes ---------------------------------------------------------------------------------------------------
    function cursorTo(x, y) {
        aiCursor.opacity = 1
        aiCursor.tx = x
        aiCursor.ty = y
        cursorHide.restart()
    }
    function cursorToView(x, y) { const p = root.view.mapToItem(stage, x, y); cursorTo(p.x, p.y) }     // a point of the page -> where it is on the pane (the view may be scaled)
    function clickRipple() { rippleAnim.restart() }
    function mouse(kind, x, y) { root.bridge.mouse(root.view, kind, x, y) }
    // the AI works only in pages of this computer
    function mayInteract() { return root.view && root.bridge.aiMayInteract(root.curUrl) }
    readonly property string denyInteract: "the AI may click, type and drag only in pages of this computer (your own work), not on websites; use browser_read to read this page"

    function doClick(id, target, then) {                                      // locate -> cursor -> real press and release
        find(target, true, function(o) {
            if (!o.ok) { root.bridge.reply(id, { ok: false, error: "no element matches \"" + target + "\": use a CSS selector or the visible text; browser_read lists the headings and text" }); return }
            later(120, function() {
                cursorToView(o.x, o.y)
                later(640, function() {
                    mouse("move", o.x, o.y)
                    later(120, function() {
                        mouse("press", o.x, o.y)
                        clickRipple()
                        later(110, function() {
                            mouse("release", o.x, o.y)
                            later(450, function() { then(o) })
                        })
                    })
                })
            })
        })
    }

    function run(id, op, args) {
        root.aiBusy = true
        busyOff.restart()
        if (tabs.count === 0)
            newTab("about:blank", true, false)
        if (op === "open") {
            if (args.newTab === true)
                newTab(args.url, true, true)
            else if (root.curUrl === args.url)
                root.view.reload()
            else
                root.view.url = args.url
            if (!(tabs.get(root.cur).agent === true)) tabs.setProperty(root.cur, "agent", true)   // the page the AI opened is its tab: later previews go there
            root.openTab = root.cur
            root.openId = id
            openLimit.restart()
        } else if (op === "tab") {
            const i = args.index - 1
            if (args.action === "list") {
                let s = "Tabs (" + tabs.count + "):"
                for (let k = 0; k < tabs.count; ++k)
                    s += "\n" + (k + 1) + (k === root.cur ? "*" : " ") + " " + (tabs.get(k).title || "(no title)") + " - " + views.itemAt(k).url
                root.bridge.reply(id, { ok: true, text: s + "\n(* = the tab that is shown)" })
            } else if (i < 0 || i >= tabs.count) {
                root.bridge.reply(id, { ok: false, error: "there is no tab " + args.index + " (use browser_tab list)" })
            } else if (args.action === "switch") {
                root.cur = i
                root.bridge.reply(id, { ok: true, text: "Now showing tab " + args.index + ": " + views.itemAt(i).url })
            } else {
                closeTab(i)
                root.bridge.reply(id, { ok: true, text: "Closed tab " + args.index + ". " + tabs.count + " tab(s) left." })
            }
        } else if (op === "read") {
            root.view.runJavaScript(root.readScript, function(r) {
                if (r === undefined || r === null || r === "")
                    root.bridge.reply(id, { ok: false, error: "no page is open yet: use browser_open first" })
                else
                    root.bridge.reply(id, { ok: true, text: root.describeRead(r) })
            })
        } else if (op === "click") {
            if (!mayInteract()) { root.bridge.reply(id, { ok: false, error: root.denyInteract }); return }
            const before = root.curUrl
            doClick(id, args.target, function(o) {
                root.bridge.reply(id, { ok: true, text: "Clicked " + o.tag + (o.text ? " \"" + o.text + "\"" : "") + (o.href ? " (link to " + o.href + ")" : "")
                                                       + ". " + (root.curUrl !== before ? "The page went to " + root.curUrl + "." : "The page stayed at " + root.curUrl + ".") })
            })
        } else if (op === "type") {
            if (!mayInteract()) { root.bridge.reply(id, { ok: false, error: root.denyInteract }); return }
            doClick(id, args.target, function(o) {
                const script = "(function(v){var el=document.activeElement;if(!el)return 'nofocus';"
                    + "if(el.isContentEditable){el.textContent=v}else if(el.tagName==='INPUT'||el.tagName==='TEXTAREA'){var d=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(el),'value');d.set.call(el,v)}else{return 'notabox'}"
                    + "el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));return 'ok'})(" + JSON.stringify(args.text) + ")"
                root.view.runJavaScript(script, function(r) {
                    if (r === "ok")
                        root.bridge.reply(id, { ok: true, text: "Typed " + args.text.length + " characters into " + o.tag + (o.text ? " \"" + o.text + "\"" : "") + "." })
                    else
                        root.bridge.reply(id, { ok: false, error: "that element is not a text box that can take typing (" + r + ")" })
                })
            })
        } else if (op === "scroll") {
            if (!root.view) { root.bridge.reply(id, { ok: false, error: "no page is open yet: use browser_open first" }); return }
            root.view.runJavaScript(root.posScript, function(p) {
                if (p === undefined || p === null) { root.bridge.reply(id, { ok: false, error: "no page is open yet: use browser_open first" }); return }
                const pos = JSON.parse(p)
                const step = Math.max(200, Math.round(pos.v * 0.9))
                const max = Math.max(0, pos.h - pos.v)
                const to = args.direction === "top" ? 0 : args.direction === "bottom" ? max : args.direction === "up" ? Math.max(0, pos.y - step) : Math.min(max, pos.y + step)
                cursorTo(stage.width * 0.55, stage.height * 0.55)
                later(450, function() {
                    root.view.runJavaScript("window.scrollTo({top:" + to + ",behavior:'smooth'})", function() {
                        later(900, function() {
                            root.view.runJavaScript(root.posScript, function(q) {
                                const n = JSON.parse(q)
                                root.bridge.reply(id, { ok: true, text: "Scrolled " + args.direction + ": now at " + n.y + " px of " + n.h + " px" + (n.y + n.v >= n.h - 2 ? " (the end of the page)." : ".") })
                            })
                        })
                    })
                })
            })
        } else if (op === "drag") {
            if (!mayInteract()) { root.bridge.reply(id, { ok: false, error: root.denyInteract }); return }
            find(args.from, true, function(a) {
                if (!a.ok) { root.bridge.reply(id, { ok: false, error: "no element matches \"" + args.from + "\"" }); return }
                find(args.to, false, function(b) {
                    if (!b.ok || !b.inside) { root.bridge.reply(id, { ok: false, error: "the target \"" + args.to + "\" is not found or not visible together with the start; scroll so that both are on screen" }); return }
                    cursorToView(a.x, a.y)
                    later(700, function() {
                        mouse("move", a.x, a.y)
                        later(100, function() {
                            mouse("press", a.x, a.y)
                            let n = 0
                            const steps = 16
                            const t = Qt.createQmlObject('import QtQuick; Timer { repeat: true }', root)
                            t.interval = 45
                            t.triggered.connect(function() {
                                ++n
                                const f = n / steps, e = f * f * (3 - 2 * f)
                                const x = a.x + (b.x - a.x) * e, y = a.y + (b.y - a.y) * e
                                cursorToView(x, y)
                                mouse("move", x, y)
                                if (n >= steps) {
                                    t.stop(); t.destroy()
                                    mouse("release", b.x, b.y)
                                    clickRipple()
                                    later(450, function() { root.bridge.reply(id, { ok: true, text: "Dragged from " + a.tag + (a.text ? " \"" + a.text + "\"" : "") + " to " + b.tag + (b.text ? " \"" + b.text + "\"" : "") + "." }) })
                                }
                            })
                            t.start()
                        })
                    })
                })
            })
        } else if (op === "viewport") {
            if (args.reset === true)
                root.setViewport(0, 0, "")
            else
                root.setViewport(args.width, args.height, args.name || "")
            later(500, function() {                                              // the page has laid itself out again: say what it really sees
                root.view.runJavaScript("JSON.stringify({w:window.innerWidth,h:window.innerHeight,sideways:document.documentElement.scrollWidth>window.innerWidth+1})", function(r) {
                    let o = { w: 0, h: 0, sideways: false }
                    try { o = JSON.parse(r) } catch (e) {}
                    root.bridge.reply(id, { ok: true, text: args.reset === true
                        ? "The browser is back at the size of its pane (" + o.w + " x " + o.h + ")."
                        : "Viewport " + args.width + " x " + args.height + (args.name ? " (" + args.name + ")" : "") + ": the page now sees a window of " + o.w + " x " + o.h + " px"
                          + (o.sideways ? " and it scrolls sideways (something is wider than the window)" : "") + ". browser_read and browser_look show it at this size; use browser_viewport with device reset when you are done." })
                })
            })
        } else if (op === "look") {
            root.view.runJavaScript(root.posScript, function(p) {
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
                root.view.runJavaScript("window.scrollTo(0," + y + ");" + root.posScript, function(q) {
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
            root.view.grabToImage(function(result) {
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
    // The profile is declared LAST on purpose: Qt destroys the children of an object in the order they were made, and a WebEngineProfile must be destroyed after every view that uses it
    // (the other way round the program crashed when it ended).
    WebEngineProfile {
        id: browserProfile
        storageName: "drivevault-browser"
        persistentStoragePath: root.bridge.storagePath !== "" ? root.bridge.storagePath + "/profile" : ""
        cachePath: root.bridge.storagePath !== "" ? root.bridge.storagePath + "/cache" : ""
        persistentPermissionsPolicy: WebEngineProfile.StoreOnDisk       // what the user allowed or blocked is remembered, like in Chrome
        offTheRecord: root.bridge.storagePath === ""
        // like Chrome: the file goes to the Downloads folder under a name that never overwrites another file; a kind of file that can run gets a warning in the bar
        onDownloadRequested: download => {
            const folder = root.bridge.downloadsFolder()
            const name = root.bridge.uniqueDownloadName(folder, download.suggestedFileName !== "" ? download.suggestedFileName : download.downloadFileName)
            const risky = root.bridge.riskyDownload(name)
            download.downloadDirectory = folder
            download.downloadFileName = name
            download.stateChanged.connect(function () {
                if (download.state === WebEngineDownloadRequest.DownloadCompleted) {
                    root.notice = Tr.s(risky ? "br_dl_done_risky" : "br_dl_done").replace("%1", name)
                    root.noticeFolder = folder
                } else if (download.state === WebEngineDownloadRequest.DownloadInterrupted) {
                    root.notice = Tr.s("br_dl_failed").replace("%1", name)
                    root.noticeFolder = ""
                }
            })
            download.accept()
            root.noticeFolder = ""
            root.notice = Tr.s("br_dl_started").replace("%1", name)
        }
        // the engine says "QtWebEngine/x.y.z" in its name; some sites (sign-in pages) turn such a browser away, so it presents itself as plain Chromium
        httpUserAgent: root.bridge.userAgent
    }

    Component.onCompleted: {
        if (tabs.count === 0)
            newTab("about:blank", true, false)
        root.bridge.viewReady = true
    }
    Component.onDestruction: {
        if (fsView)
            setFullscreen(false, fsView)                  // never leave the window full screen when the browser goes away
        root.bridge.viewReady = false
        root.bridge.unwatchAll()
    }
}
