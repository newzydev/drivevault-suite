import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import QtWebEngine
import DriveVault.Browser.Backend

// One browser window: tabs, address bar, bookmarks bar, the pages (one WebEngineView per tab) and a side panel.
// privateMode = a private window (its own off-the-record profile: no history, no cookies kept).
ApplicationWindow {
    id: win
    objectName: "browserWindow"

    signal closed()

    property bool privateMode: false
    property bool primary: true
    property var startUrls: []
    property string panel: ""                       // "" | history | bookmarks | downloads
    property int currentIndex: 0
    property int pinnedCount: 0
    property var closedUrls: []
    property var pending: []                        // permission questions that wait for the user (WebEnginePermission), oldest first
    property bool findOpen: false
    property string findResult: ""
    property string hoverUrl: ""
    property bool isFull: false
    property int beforeFull: Window.Windowed
    property bool wasMaximized: false
    property rect normalGeometry: Qt.rect(0, 0, 1280, 820)
    property var currentView: null
    property var pipView: null                      // the page (WebEngineView) that is in the small picture-in-picture window, or null
    property var pipWindow: null
    property string noteText: ""                    // a short message at the bottom of the page area
    property var suggestions: []
    property int suggestIndex: -1
    property var barItems: []

    readonly property color accent: privateMode ? Theme.privateAccent : Theme.accent
    readonly property string curUrl: currentView ? currentView.url.toString() : ""
    readonly property string curTitle: currentView ? currentView.title : ""
    readonly property bool curBlank: curUrl === "" || curUrl === "about:blank"
    readonly property string curHost: Browser.hostOf(curUrl)
    readonly property bool starred: { Browser.bookmarks.items; return curUrl !== "" && Browser.bookmarks.contains(curUrl) }

    width: 1280
    height: 820
    minimumWidth: 640
    minimumHeight: 420
    flags: Qt.Window | Qt.FramelessWindowHint
    color: Theme.background
    title: ProfileSession.locked ? Tr.t("app")
        : (privateMode ? "[" + Tr.t("privateBadge") + "] " : "") + (curTitle !== "" && !curBlank ? curTitle + " - " : "") + Tr.t("app")

    function rememberNormalGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(x, y, width, height)
    }
    function toggleMaximized() {
        if (visibility === Window.Maximized) {
            showNormal()
        } else {
            rememberNormalGeometry()
            showMaximized()
        }
    }
    function restoreWindow() {
        const state = primary && !privateMode ? Browser.settings.windowState() : {}
        if (state.width > 0 && state.height > 0) {
            width = Math.max(minimumWidth, Math.min(state.width, screen.desktopAvailableWidth))
            height = Math.max(minimumHeight, Math.min(state.height, screen.desktopAvailableHeight))
            const screens = Qt.application.screens
            for (let i = 0; i < screens.length; ++i) {
                const s = screens[i]
                if (state.x >= s.virtualX && state.x + 160 < s.virtualX + s.width
                        && state.y >= s.virtualY && state.y + 32 < s.virtualY + s.height) {
                    x = state.x
                    y = state.y
                    break
                }
            }
        }
        normalGeometry = Qt.rect(x, y, width, height)
        if (state.maximized)
            showMaximized()
        else
            showNormal()
    }
    Timer {
        id: geometrySettled
        interval: 350
        onTriggered: win.rememberNormalGeometry()
    }
    onXChanged: geometrySettled.restart()
    onYChanged: geometrySettled.restart()
    onWidthChanged: geometrySettled.restart()
    onHeightChanged: geometrySettled.restart()
    onVisibilityChanged: {
        if (visibility === Window.Windowed || visibility === Window.Maximized)
            wasMaximized = visibility === Window.Maximized
    }

    // ---- tabs -------------------------------------------------------------------------------------------------------------
    // (the ListModel `tabs` is declared at the END of this file: Qt destroys the children of an object in the order they were made, and the views write into the model
    //  from their signals - a model that dies first made the program crash when it was closed while pages were still loading)

    function recount() {
        let n = 0
        for (let i = 0; i < tabs.count; ++i) if (tabs.get(i).pinned) ++n
        pinnedCount = n
    }
    function refreshCurrent() { currentView = views.itemAt(currentIndex) }
    onCurrentIndexChanged: refreshCurrent()

    function addTab(url, activate) {
        tabs.append({ title: "", icon: "", loading: false, pinned: false, startUrl: url || "" })
        if (activate !== false) currentIndex = tabs.count - 1
        if (!url && activate !== false) Qt.callLater(focusWorkspaceSearch)
    }
    // ---- picture in picture ------------------------------------------------------------------------------------------------
    // The engine's own picture-in-picture is switched off (document.pictureInPictureEnabled is false), so the tab itself is moved into a small window that stays on top
    // and the video of the page is made to fill it (resources/pip-on.js: style only, the video keeps playing). Closing the small window moves the tab back.
    function showNote(text) { noteText = text; noteTimer.restart() }
    function popOutVideo() {
        const v = currentView
        if (!v || pipView) return
        v.runJavaScript(Browser.pipOnScript, function (r) {
            if (ProfileSession.locked) {
                v.runJavaScript(Browser.pipOffScript)
                return
            }
            let info = null
            try { info = JSON.parse(r) } catch (e) { info = null }
            if (!info || !info.ok) { showNote(Tr.t("pip_none")); return }
            const w = 480
            const h = Math.max(140, Math.min(720, Math.round(w * (info.w > 0 && info.h > 0 ? info.h / info.w : 9 / 16))))
            pipWindow = pipComponent.createObject(null, { width: w, height: h, title: (privateMode ? "[" + Tr.t("privateBadge") + "] " : "") + Tr.t("pip_title") + " - " + (info.title || "") })
            pipView = v
            v.poppedOut = true
            v.parent = pipWindow.contentItem
            pipWindow.show()
        })
    }
    function bringBackVideo() {
        const v = pipView
        if (!v) return
        pipView = null
        v.runJavaScript(Browser.pipOffScript)
        v.poppedOut = false
        v.parent = stage
        const w = pipWindow
        pipWindow = null
        if (w) { w.visible = false; w.destroy(1500) }
    }

    function closeTab(i) {
        if (i < 0 || i >= tabs.count) return
        const v = views.itemAt(i)
        if (v && v === pipView) bringBackVideo()
        if (v) { const u = v.url.toString(); if (u !== "" && u !== "about:blank" && !privateMode) closedUrls = closedUrls.concat([u]) }
        if (tabs.count === 1) { win.close(); return }
        const cur = currentIndex
        tabs.remove(i)
        currentIndex = i < cur ? cur - 1 : Math.min(cur, tabs.count - 1)
        recount()
        refreshCurrent()
    }
    function closeOthers(keep) {
        for (let i = tabs.count - 1; i >= 0; --i) if (i !== keep && !tabs.get(i).pinned) closeTab(i)
    }
    function closeRight(from) {
        for (let i = tabs.count - 1; i > from; --i) if (!tabs.get(i).pinned) closeTab(i)
    }
    function reopenClosed() {
        if (closedUrls.length === 0) return
        const u = closedUrls[closedUrls.length - 1]
        closedUrls = closedUrls.slice(0, -1)
        addTab(u, true)
    }
    function togglePin(i) {
        const makePinned = !tabs.get(i).pinned
        tabs.setProperty(i, "pinned", makePinned)
        recount()
        moveTab(i, makePinned ? pinnedCount - 1 : pinnedCount)
    }
    function moveTab(i, target) {
        const cur = currentIndex
        if (target !== i) {
            tabs.move(i, target, 1)
            if (cur === i) currentIndex = target
            else if (i < cur && target >= cur) currentIndex = cur - 1
            else if (i > cur && target <= cur) currentIndex = cur + 1
        }
        refreshCurrent()
    }
    function tabsMatching(query) {
        const term = query.trim().toLowerCase()
        const results = []
        for (let i = 0; i < tabs.count; ++i) {
            const view = views.itemAt(i)
            const label = tabs.get(i).title
            const title = label && label !== "about:blank" ? label : Tr.t("untitled")
            const url = view ? view.url.toString() : ""
            if (title.toLowerCase().includes(term) || url.toLowerCase().includes(term))
                results.push({ tabIndex: i, title: title, url: url })
        }
        return results
    }
    function openReader() {
        const view = currentView
        if (!view || curBlank) return
        view.runJavaScript(`(() => {
            const content = document.querySelector('article') || document.querySelector('[role="main"], main') || document.body;
            return content ? content.innerText.trim().slice(0, 250000) : '';
        })()`, text => {
            if (view !== currentView) return
            if (!text) { showNote(Tr.t("reader_empty")); return }
            reader.heading = view.title || Tr.t("reader_mode")
            reader.article = text
            reader.open()
        })
    }
    function printPage(view) {
        if (!view || ProfileSession.locked || view.url.toString() === "about:blank") return
        const title = view.title
        view.printToPdf(pdf => {
            Qt.callLater(() => {
                if (!ProfileSession.locked && Browser.printPdf(pdf, title) < 0) win.showNote(Tr.t("print_failed"))
            })
        })
    }
    function openUrl(url, newTab) {
        if (newTab || !currentView) addTab(url, true)
        else currentView.url = url
    }
    function navigate(text) {
        if (!currentView) return
        currentView.url = Browser.resolve(text)
        currentView.forceActiveFocus()
    }
    function homeAddress(homePage) {
        return homePage !== "" ? Browser.resolve(homePage) : "https://www.google.com"
    }
    function goHome() {
        if (currentView) currentView.url = homeAddress(Browser.settings.homePage)
    }
    function setFull(on) {
        if (on === isFull) return
        if (on) {
            rememberNormalGeometry()
            beforeFull = visibility
            visibility = Window.FullScreen
        }
        else visibility = beforeFull === Window.FullScreen ? Window.Windowed : beforeFull
        isFull = on
    }
    function prepareScripts(view, url) {
        const src = Browser.adblock.scriptFor(url)
        view.userScripts.collection = src === "" ? [] : [{
            name: "dv-adblock", sourceCode: src,
            injectionPoint: WebEngineScript.DocumentCreation, worldId: WebEngineScript.MainWorld, runsOnSubFrames: false
        }]
    }
    function showPanel(kind) {
        if (kind === "settings") settingsModal.open()
        else panel = kind
    }
    function togglePanel(kind) { showPanel(panel === kind ? "" : kind) }
    function focusOmnibox() { omnibox.forceActiveFocus(); omnibox.selectAll() }
    function focusWorkspaceSearch() {
        if (newTabPage.visible && !settingsModal.visible && !mainMenu.visible)
            newTabPage.focusSearch()
    }
    function zoomBy(delta) {
        const v = currentView
        if (!v) return
        const f = delta === 0 ? 1.0 : Math.max(0.25, Math.min(5.0, Math.round((v.zoomFactor + delta) * 100) / 100))
        v.zoomFactor = f
        if (!privateMode) Browser.settings.setZoomFor(Browser.hostOf(v.url.toString()), f)
    }
    function doFind(backward) {
        if (!currentView) return
        if (findField.text === "") { currentView.findText(""); findResult = ""; return }
        currentView.findText(findField.text, backward ? WebEngineView.FindBackward : 0)
    }
    function openFind() { findOpen = true; findField.forceActiveFocus(); findField.selectAll() }
    function closeFind() { findOpen = false; if (currentView) currentView.findText(""); findResult = ""; if (currentView) currentView.forceActiveFocus() }
    function openDevTools() {
        if (!currentView) return
        devLoader.active = true
        if (devLoader.item) { currentView.devToolsView = devLoader.item.view; devLoader.item.show(); devLoader.item.raise() }
    }
    function permName(type) {
        const T = WebEnginePermission.PermissionType
        switch (type) {
        case T.MediaAudioCapture: return Tr.t("perm_mic")
        case T.MediaVideoCapture: return Tr.t("perm_camera")
        case T.MediaAudioVideoCapture: return Tr.t("perm_camera_mic")
        case T.DesktopVideoCapture: return Tr.t("perm_screen")
        case T.DesktopAudioVideoCapture: return Tr.t("perm_screen_audio")
        case T.Geolocation: return Tr.t("perm_location")
        case T.Notifications: return Tr.t("perm_notifications")
        case T.ClipboardReadWrite: return Tr.t("perm_clipboard")
        case T.MouseLock: return Tr.t("perm_mouselock")
        case T.LocalFontsAccess: return Tr.t("perm_fonts")
        }
        return Tr.t("perm_other")
    }
    function answerPermission(allow) {
        if (pending.length === 0) return
        const p = pending[0]
        pending = pending.slice(1)
        if (allow === true) p.grant()
        else if (allow === false) p.deny()
    }
    function updateSuggestions() {
        const t = omnibox.text.trim()
        if (t === "" || !omnibox.activeFocus) { suggestions = []; suggestIndex = -1; return }
        suggestions = [{ kind: "search", title: t, url: "" }].concat(Browser.suggest(t, 6))
        suggestIndex = -1
    }
    function acceptAddress() {
        const s = suggestIndex >= 0 && suggestIndex < suggestions.length ? suggestions[suggestIndex] : null
        const text = s && s.kind !== "search" ? s.url : omnibox.text
        suggestions = []
        suggestIndex = -1
        navigate(text)
    }
    function syncAddress() {
        if (!omnibox.activeFocus) omnibox.text = Browser.prettyUrl(curUrl)
    }
    onCurUrlChanged: syncAddress()
    onCurrentViewChanged: { syncAddress(); closeFindIfOther() }
    function closeFindIfOther() { if (findOpen) { findResult = "" } }

    function refreshBar() {
        barItems = Browser.bookmarks.items.filter(function (b) { return b.folder === "bar" })
    }
    Connections { target: Browser.bookmarks; function onChanged() { win.refreshBar() } }

    Component.onCompleted: {
        restoreWindow()
        if (!ProfileSession.locked) initializeTabs()
    }
    function initializeTabs() {
        refreshBar()
        if (startUrls.length > 0) {
            for (let i = 0; i < startUrls.length; ++i) addTab(startUrls[i], i === 0)
            currentIndex = 0
        } else {
            const h = Browser.settings.homePage
            addTab(h !== "" && !privateMode ? Browser.resolve(h).toString() : "", true)
        }
    }
    Connections {
        target: ProfileSession
        function onLockedChanged() {
            if (!ProfileSession.locked) {
                if (tabs.count === 0) win.initializeTabs()
                return
            }
            win.bringBackVideo()
            if (devLoader.item) devLoader.item.hide()
            for (const popup of [mainMenu, settingsModal, profileOptions, tabSearch, tabMenu, mediaMenu, shieldMenu, pageMenu, reader, captureDialog, pdfDialog])
                popup.close()
            lockPin.forceActiveFocus()
        }
    }
    onClosing: {
        if (primary && !privateMode) {
            rememberNormalGeometry()
            Browser.settings.saveWindowState({
                maximized: wasMaximized, x: normalGeometry.x, y: normalGeometry.y,
                width: normalGeometry.width, height: normalGeometry.height
            })
            const urls = []
            for (let i = 0; i < views.count; ++i) { const v = views.itemAt(i); if (v) urls.push(v.url.toString()) }
            if (tabs.count > 0) Browser.saveSession(urls)
        }
        bringBackVideo()
        // the pages are taken down one by one while the window and its model are still alive (a page that is still loading would otherwise call into a half destroyed window)
        for (let i = 0; i < views.count; ++i) { const v = views.itemAt(i); if (v) v.stop() }
        currentView = null
        tabs.clear()
        closed()
    }

    // ---- diagnostics: --selftest <file> runs this on the first window (the page to load is given as an address argument) ------------
    property string selfTestResult: ""
    property var stChecks: []
    function check(name, ok, detail) { stChecks = stChecks.concat([{ name: name, ok: !!ok, detail: detail === undefined ? "" : String(detail) }]) }
    function selfTest() {
        stChecks = []
        const base = tabs.count
        check("a tab exists at start", base >= 1, base)
        addTab("about:blank", true)
        check("addTab makes a new current tab", tabs.count === base + 1 && currentIndex === base, tabs.count + "/" + currentIndex)
        const addedView = currentView
        moveTab(currentIndex, 0)
        check("moving a tab preserves its WebEngineView and selection", currentIndex === 0 && currentView === addedView, currentIndex)
        moveTab(currentIndex, base)
        togglePin(currentIndex)
        check("pinning moves the tab to the front and the selection follows", tabs.get(0).pinned === true && currentIndex === 0 && pinnedCount === 1, "pinned=" + pinnedCount + " current=" + currentIndex)
        closeTab(0)
        check("closing the pinned tab leaves the other one selected", tabs.count === base && currentIndex === 0 && pinnedCount === 0, tabs.count + "/" + currentIndex)
        stTimer.step = 1
        stTimer.start()
    }
    function selfTestStep(step) {
        if (step === 1) {                                   // the local page has loaded by now
            check("page title reached the tab", currentView && currentView.title === "Smoke", currentView ? currentView.title : "no view")
            check("title reached the tab model", tabs.get(currentIndex).title === "Smoke", tabs.get(currentIndex).title)
            const matches = tabsMatching("sMoKe")
            check("tab search finds the current title without case sensitivity",
                matches.length === 1 && matches[0].tabIndex === currentIndex, matches.length)
            check("history has the page", privateMode || Browser.history.search("page.html", 5).length >= 1, Browser.history.count)
            starButton.clicked()
            check("star adds a bookmark", Browser.bookmarks.contains(curUrl) && starred, curUrl)
            starButton.clicked()
            check("star again removes it", !Browser.bookmarks.contains(curUrl) && !starred, "")
            starButton.clicked()
            Browser.bookmarks.items
            check("bookmarks bar shows it", barItems.length === 1, barItems.length)
            omnibox.forceActiveFocus()
            omnibox.text = "Smoke"
            updateSuggestions()
            check("address bar suggests a search and the history page", suggestions.length >= 2 && suggestions[0].kind === "search", suggestions.length)
            suggestions = []
            openFind()
            findField.text = "Hello"
            stTimer.step = 2
            stTimer.start()
        } else if (step === 2) {
            check("find in page counts the match", findResult === "1/1", findResult)
            closeFind()
            check("closeFind closes the bar", !findOpen, "")
            const before = currentView.zoomFactor
            zoomBy(0.1)
            check("zoom in changes the factor", currentView.zoomFactor > before, currentView.zoomFactor)
            zoomBy(0)
            check("zoom reset", Math.abs(currentView.zoomFactor - 1.0) < 0.001, currentView.zoomFactor)
            omnibox.text = "localhost:1/x"
            acceptAddress()
            stTimer.step = 3
            stTimer.start()
        } else if (step === 3) {
            check("typed address is opened", curUrl.indexOf("http://localhost:1/x") === 0, curUrl)
            const originalLanguage = Tr.lang
            languageChoices.itemAt(0).clicked()
            const thaiSelected = Tr.lang === "th" && languageChoices.itemAt(0).current
            languageChoices.itemAt(1).clicked()
            check("title bar language switch selects Thai and English", thaiSelected && Tr.lang === "en" && languageChoices.itemAt(1).current, "")
            Browser.settings.language = originalLanguage
            menuButton.clicked()
            check("toolbar menu opens below the three-dot button",
                  mainMenu.visible && !mainMenu.modal && mainMenu.y > mainMenu.anchorPoint.y, mainMenu.y)
            mainMenu.close()
            settingsButton.clicked()
            check("title bar settings button opens settings", settingsModal.visible && settingsModal.modal, "")
            settingsModal.close()
            check("Home uses Google by default and respects a custom page",
                  homeAddress("") === "https://www.google.com"
                  && homeAddress("https://example.com/").toString() === "https://example.com/", "")
            selfTestResult = JSON.stringify(stChecks)
        }
    }
    Timer { id: stTimer; property int step: 0; interval: 1800; onTriggered: win.selfTestStep(step) }
    // --survey: a look at a real page (tests on real websites): size of its text, frames, walls that talk about ad blockers that are still visible, scroll lock
    function survey() {
        if (!currentView) { selfTestResult = "[]"; return }
        const js = "(function(){var W=/ad[\\s-]?block|ads? blocker|adblocking|(?:disable|turn off|deactivate|pause|whitelist)[^.]{0,40}\\bads?\\b|\\bads?\\b[^.]{0,40}(?:disable|turn off|whitelist)|ตัวบล็อกโฆษณา|บล็อกโฆษณา|แอดบล็อก/i;"
                  + "var vw=innerWidth,vh=innerHeight,walls=[],all=document.querySelectorAll('body *'),n=Math.min(all.length,6000);"
                  + "for(var i=0;i<n&&walls.length<3;i++){var e=all[i];if(e.children.length>40)continue;var t=e.textContent;if(!t||t.length>3000||!W.test(t))continue;"
                  + "for(var a=e,d=0;a&&a!==document.body&&d<8;a=a.parentElement,d++){var cs=getComputedStyle(a);"
                  + "if((cs.position==='fixed'||(cs.position==='absolute'&&parseInt(cs.zIndex,10)>=100))&&cs.display!=='none'&&cs.visibility!=='hidden'&&cs.opacity!=='0'&&a.offsetWidth>=vw*0.5&&a.offsetHeight>=vh*0.3)"
                  + "{walls.push({tag:a.tagName,id:String(a.id).slice(0,40),cls:String(a.className).slice(0,60),text:t.trim().replace(/\\s+/g,' ').slice(0,140)});break;}}}"
                  + "var bs=getComputedStyle(document.body),hs=getComputedStyle(document.documentElement);"
                  + "return JSON.stringify({title:document.title,textLen:(document.body?document.body.innerText:'').length,iframes:document.querySelectorAll('iframe').length,imgs:document.images.length,walls:walls,bodyOverflow:bs.overflow,htmlOverflow:hs.overflow,scrollH:document.documentElement.scrollHeight,ready:document.readyState});})()"
        currentView.runJavaScript(js, function (r) {
            selfTestResult = JSON.stringify([{ name: "survey", ok: true, detail: r === undefined ? "null" : r, blocked: Browser.adblock.blockedOn(curHost), host: curHost, url: curUrl, loading: currentView.loading, rules: Browser.adblock.lists.totalRules }])
        })
    }
    // --media-test: the videos the page loaded are noticed, the button shows, a video file and an HLS stream are saved through the program's own path
    property var mediaTestList: []
    property int mediaTestId: 0
    function mediaTest() {
        stChecks = []
        const list = Browser.media.listFor(curHost)
        mediaTestList = list
        check("the video file of the page was noticed", list.some(function (m) { return m.kind === "file" && m.url.indexOf("/media/clip.webm") >= 0 }), JSON.stringify(list))
        check("the HLS playlist of the page was noticed", list.some(function (m) { return m.kind === "hls" && m.url.indexOf("/media/live/index.m3u8") >= 0 }), "")
        check("no piece of the stream (.ts) is offered", !list.some(function (m) { return m.url.indexOf(".ts") >= 0 }), "")
        check("the video button is shown with the count", videoButton.visible && videoButton.count === list.length, videoButton.count)
        check("a video of YouTube is never offered for saving", Browser.media.save("https://rr1---sn-x.googlevideo.com/videoplayback/clip.mp4", "file", "https://www.youtube.com/watch?v=x") === false, "")
        const f = list.filter(function (m) { return m.kind === "file" })[0]
        const h = list.filter(function (m) { return m.kind === "hls" })[0]
        check("saving the video file starts", f !== undefined && Browser.media.save(f.url, f.kind, f.pageUrl) === true, "")
        if (h !== undefined && Browser.media.ffmpegAvailable) check("saving the stream starts", Browser.media.save(h.url, h.kind, h.pageUrl) === true, "")
        pipStep = 100
        pipTimer.interval = 7000
        pipTimer.start()
    }
    function mediaTestFinish() {
        const rows = []
        for (let i = 0; i < 4; ++i) { const d = Browser.downloads.get(i); if (d && d.name) rows.push(d) }
        const file = rows.filter(function (d) { return d.name.indexOf("clip") === 0 && d.name.indexOf(".webm") > 0 })[0]
        check("the video file is in the downloads list as done", file !== undefined && file.state === "done", JSON.stringify(rows))
        check("the saved video file has the size of the original", file !== undefined && Browser.fileSize(file.path) > 5000, file ? Browser.fileSize(file.path) : -1)
        if (Browser.media.ffmpegAvailable) {
            const st = rows.filter(function (d) { return d.name.indexOf(".mp4") > 0 })[0]
            check("the stream is saved as an mp4", st !== undefined && st.state === "done" && Browser.fileSize(st.path) > 1000, JSON.stringify(st))
        }
        selfTestResult = JSON.stringify(stChecks)
    }
    // --pip-test: pops the video of the loaded page out and back (button path, close-the-small-window path) and checks what really happens
    property int pipStep: 0
    property real pipT0: 0
    function pipTest() {
        stChecks = []
        pipStep = 0
        if (!currentView) { check("a page is open", false); selfTestResult = JSON.stringify(stChecks); return }
        currentView.runJavaScript("JSON.stringify({t: window.__video ? window.__video.currentTime : -1, paused: window.__video ? window.__video.paused : null})", function (r) {
            const o = JSON.parse(r)
            pipT0 = o.t
            check("the page plays its video before the pop-out", o.t >= 0 && o.paused === false, r)
            popOutVideo()
            pipStep = 1
            pipTimer.start()
        })
    }
    function pipQuery(cb) {
        currentView.runJavaScript("(function(){var v=window.__video,o=document.getElementById('other');return JSON.stringify({pos:getComputedStyle(v).position,w:Math.round(v.getBoundingClientRect().width),vw:innerWidth,paused:v.paused,t:v.currentTime,ctl:v.controls,"
                                  + "otherVis:o?getComputedStyle(o).visibility:null,style:!!document.getElementById('__dv_pip'),cls:v.classList.contains('__dv_pip_v')})})()", function (r) { cb(JSON.parse(r), r) })
    }
    function pipTestStep() {
        if (pipStep === 100) { pipStep = 0; mediaTestFinish(); return }
        if (pipStep === 1) {                                    // the video is in the small window
            check("the tab is in the small window", pipView !== null && pipWindow !== null && pipWindow.visible && pipView.parent === pipWindow.contentItem, "")
            check("the small window stays on top", pipWindow !== null && (pipWindow.flags & Qt.WindowStaysOnTopHint) !== 0, "")
            check("the tab shows the 'video is in the small window' note", currentView === pipView, "")
            pipQuery(function (o, raw) {
                check("the video fills the small window", o.pos === "fixed" && Math.abs(o.w - o.vw) <= 2, raw)
                check("the video has its controls", o.ctl === true, raw)
                check("the rest of the page is hidden", o.otherVis === "hidden", raw)
                check("the video keeps playing in the small window", o.paused === false && o.t > pipT0, raw)
                pipT0 = o.t
                bringBackVideo()
                pipStep = 2
                pipTimer.start()
            })
        } else if (pipStep === 2) {                             // brought back with the button path
            check("the tab is back in the window", pipView === null && currentView.parent === stage, "")
            pipQuery(function (o, raw) {
                check("the page looks as before (style and class removed)", o.style === false && o.cls === false && o.otherVis === "visible", raw)
                check("the video kept playing and kept its position", o.paused === false && o.t > pipT0, raw)
                pipT0 = o.t
                popOutVideo()
                pipStep = 3
                pipTimer.start()
            })
        } else if (pipStep === 3) {                             // second pop-out: closed with the small window's own close button
            check("popping out a second time works", pipView !== null && pipWindow !== null && pipWindow.visible, "")
            pipWindow.close()
            pipStep = 4
            pipTimer.start()
        } else if (pipStep === 4) {
            check("closing the small window brings the tab back", pipView === null && currentView.parent === stage, "")
            pipQuery(function (o, raw) {
                check("the page is as before after the second round", o.style === false && o.cls === false && o.paused === false && o.t > pipT0, raw)
                selfTestResult = JSON.stringify(stChecks)
            })
        }
    }
    Timer { id: pipTimer; interval: 1800; onTriggered: win.pipTestStep() }
    // --probe: what the loaded page recorded (window.__res) and how many requests the ad blocker stopped for its host
    function probe() {
        if (!currentView) { selfTestResult = "[]"; return }
        currentView.runJavaScript("JSON.stringify(window.__res || null)", function (r) {
            selfTestResult = JSON.stringify([{ name: "probe", ok: true, detail: r, blocked: Browser.adblock.blockedOn(curHost), host: curHost, rules: Browser.adblock.lists.totalRules }])
        })
    }

    // ---- shortcuts ---------------------------------------------------------------------------------------------------------
    Shortcut { sequence: "Ctrl+T"; onActivated: win.addTab("", true) }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: tabSearch.open() }
    Shortcut { sequence: "Ctrl+P"; onActivated: win.printPage(win.currentView) }
    Shortcut { sequence: "Ctrl+W"; onActivated: win.closeTab(win.currentIndex) }
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: win.reopenClosed() }
    Shortcut { sequence: "Ctrl+N"; onActivated: Browser.openWindow(false, "") }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: Browser.openWindow(true, "") }
    Shortcut { sequences: ["Ctrl+L", "Alt+D", "F6"]; onActivated: win.focusOmnibox() }
    Shortcut { sequences: ["Ctrl+R", "F5"]; onActivated: if (win.currentView) win.currentView.reload() }
    Shortcut { sequence: "Ctrl+Shift+R"; onActivated: if (win.currentView) win.currentView.reloadAndBypassCache() }
    Shortcut { sequence: "Alt+Left"; onActivated: if (win.currentView) win.currentView.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: if (win.currentView) win.currentView.goForward() }
    Shortcut { sequence: "Alt+Home"; onActivated: win.goHome() }
    Shortcut { sequence: "Ctrl+F"; onActivated: win.openFind() }
    Shortcut { sequence: "Ctrl+D"; onActivated: starButton.clicked() }
    Shortcut { sequence: "Ctrl+H"; onActivated: win.togglePanel("history") }
    Shortcut { sequence: "Ctrl+Shift+O"; onActivated: win.togglePanel("bookmarks") }
    Shortcut { sequence: "Ctrl+J"; onActivated: win.togglePanel("downloads") }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; onActivated: win.zoomBy(0.1) }
    Shortcut { sequence: "Ctrl+-"; onActivated: win.zoomBy(-0.1) }
    Shortcut { sequence: "Ctrl+0"; onActivated: win.zoomBy(0) }
    Shortcut { sequence: "F11"; onActivated: win.setFull(!win.isFull) }
    Shortcut { sequence: "F12"; onActivated: win.openDevTools() }
    Shortcut { sequence: "Alt+P"; onActivated: win.pipView ? win.bringBackVideo() : win.popOutVideo() }
    Shortcut { sequence: "Ctrl+U"; onActivated: if (!win.curBlank) win.addTab("view-source:" + win.curUrl, true) }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: win.currentIndex = (win.currentIndex + 1) % tabs.count }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: win.currentIndex = (win.currentIndex + tabs.count - 1) % tabs.count }
    Shortcut { sequence: "Escape"; enabled: win.findOpen; onActivated: win.closeFind() }

    // ---- small styled controls ---------------------------------------------------------------------------------------------
    component DMenu: Menu {
        background: Rectangle { implicitWidth: 260; color: Theme.surface; border.width: 1; border.color: Theme.border; radius: 8 }
    }
    component DItem: MenuItem {
        id: mi
        implicitHeight: 32
        font.pixelSize: 13
        contentItem: Text { leftPadding: 8; text: mi.text; color: mi.enabled ? Theme.text : Theme.textDisabled; font: mi.font; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { color: mi.highlighted ? Theme.surfaceHover : "transparent"; radius: 6 }
    }
    component PageMenuItem: DItem {
        id: mi
        property string shortcutLabel: ""
        height: visible ? implicitHeight : 0
        contentItem: RowLayout {
            spacing: 16
            Text {
                Layout.fillWidth: true
                leftPadding: 8
                text: mi.text
                color: mi.enabled ? Theme.text : Theme.textDisabled
                font: mi.font
                elide: Text.ElideRight
            }
            Text {
                visible: mi.shortcutLabel !== ""
                rightPadding: 8
                text: mi.shortcutLabel
                color: mi.enabled ? Theme.textSecondary : Theme.textDisabled
                font.pixelSize: 11
            }
        }
    }
    component DSep: MenuSeparator {
        contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSubtle }
    }

    component ResizeEdge: MouseArea {
        required property int edges
        z: 1
        enabled: win.visibility === Window.Windowed
        onPressed: win.startSystemResize(edges)
    }
    ResizeEdge { edges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor; x: 0; y: 10; width: 5; height: win.height - 20 }
    ResizeEdge { edges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor; x: win.width - 5; y: 10; width: 5; height: win.height - 20 }
    ResizeEdge { edges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor; x: 10; y: 0; width: win.width - 20; height: 5 }
    ResizeEdge { edges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor; x: 10; y: win.height - 5; width: win.width - 20; height: 5 }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; cursorShape: Qt.SizeFDiagCursor; width: 10; height: 10 }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; cursorShape: Qt.SizeFDiagCursor; x: win.width - 10; y: win.height - 10; width: 10; height: 10 }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; cursorShape: Qt.SizeBDiagCursor; x: win.width - 10; width: 10; height: 10 }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; cursorShape: Qt.SizeBDiagCursor; y: win.height - 10; width: 10; height: 10 }

    // ---- layout ------------------------------------------------------------------------------------------------------------
    ColumnLayout {
        enabled: !ProfileSession.locked
        anchors.fill: parent
        spacing: 0
        layer.enabled: ProfileSession.locked
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(1, Math.ceil(width / 5)), Math.max(1, Math.ceil(height / 5)))
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1
            blurMax: 64
        }

        Rectangle {
            visible: !win.isFull
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.background

            DragHandler {
                target: null
                onActiveChanged: if (active) win.startSystemMove()
            }
            TapHandler { onDoubleTapped: win.toggleMaximized() }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 24
                spacing: 12
                Image {
                    source: "qrc:/qt/qml/DriveVault/Browser/resources/icons/app.png"
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    text: Tr.t("app")
                    color: Theme.text
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    implicitHeight: 32
                    implicitWidth: languages.implicitWidth + 6
                    radius: 16
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    Row {
                        id: languages
                        anchors.centerIn: parent
                        spacing: 2
                        Repeater {
                            id: languageChoices
                            model: ["th", "en"]
                            delegate: AbstractButton {
                                id: languageButton
                                required property string modelData
                                readonly property bool current: Tr.lang === modelData
                                implicitWidth: 46
                                implicitHeight: 26
                                hoverEnabled: true
                                text: modelData === "th" ? Tr.t("language_th") : "EN"
                                Accessible.name: text
                                onClicked: Browser.settings.language = modelData

                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                contentItem: Text {
                                    text: languageButton.text
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: languageButton.current ? Theme.readableOn(win.accent) : Theme.textSecondary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 13
                                    color: languageButton.current ? win.accent : languageButton.hovered ? Theme.surfaceHover : "transparent"
                                    border.width: languageButton.visualFocus ? 1 : 0
                                    border.color: Theme.text
                                }
                            }
                        }
                    }
                }
                BButton {
                    id: settingsButton
                    Layout.preferredWidth: 34
                    Layout.rightMargin: 4
                    leftPadding: 0
                    rightPadding: 0
                    Accessible.name: Tr.t("settings")
                    checkable: true
                    checked: settingsModal.visible
                    onClicked: settingsModal.open()
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: Tr.t("settings")
                    GearIcon {
                        anchors.centerIn: parent
                        color: settingsButton.checked ? win.accent : Theme.text
                    }
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }
                RowLayout {
                    spacing: 2
                    WindowButton { kind: "minimize"; text: Tr.t("minimize"); onClicked: { win.rememberNormalGeometry(); win.showMinimized() } }
                    WindowButton {
                        kind: win.visibility === Window.Maximized ? "restore" : "maximize"
                        text: Tr.t(kind)
                        onClicked: win.toggleMaximized()
                    }
                    WindowButton { kind: "close"; text: Tr.t("close"); onClicked: win.close() }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
        }

        // tab strip
        Rectangle {
            visible: !win.isFull
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.backgroundAlt
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 6
                spacing: 4
                Rectangle {
                    visible: win.privateMode
                    Layout.preferredHeight: 26
                    Layout.preferredWidth: badge.implicitWidth + 26
                    Layout.alignment: Qt.AlignVCenter
                    radius: 13
                    color: Qt.alpha(Theme.privateAccent, 0.2)
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4
                        LineIcon { kind: "mask"; size: 14; color: Theme.privateAccent }
                        Text { id: badge; text: Tr.t("privateBadge"); color: Theme.privateAccent; font.pixelSize: 12; font.weight: Font.Medium }
                    }
                }
                ListView {
                    id: strip
                    objectName: "tabStrip"
                    Layout.fillWidth: true
                    Layout.maximumWidth: contentWidth + 2
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    interactive: false
                    clip: true
                    model: tabs
                    readonly property real tabWidth: Math.max(64, Math.min(220, (width - win.pinnedCount * 44) / Math.max(1, tabs.count - win.pinnedCount)))
                    delegate: Item {
                        id: tab
                        required property int index
                        required property string title
                        required property string icon
                        required property bool loading
                        required property bool pinned
                        readonly property bool current: index === win.currentIndex
                        width: pinned ? 44 : strip.tabWidth
                        height: strip.height
                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 1
                            anchors.rightMargin: 1
                            radius: 9
                            color: tab.current ? Theme.surface : tabHover.hovered ? Qt.alpha(Theme.surface, 0.6) : "transparent"
                            border.width: tab.current ? 1 : 0
                            border.color: Theme.border
                            Rectangle {              // the colour line of the selected tab
                                visible: tab.current
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width - 20
                                height: 2
                                radius: 1
                                color: win.accent
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: tab.pinned ? 10 : 4
                                spacing: 8
                                Item {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    Image {
                                        anchors.fill: parent
                                        visible: !tab.loading && tab.icon !== ""
                                        source: tab.icon
                                        sourceSize: Qt.size(32, 32)
                                        fillMode: Image.PreserveAspectFit
                                    }
                                    LineIcon { anchors.centerIn: parent; visible: !tab.loading && tab.icon === ""; kind: "globe"; size: 15; color: Theme.textSecondary }
                                    BusyIndicator { anchors.centerIn: parent; visible: tab.loading; running: tab.loading; width: 16; height: 16 }
                                }
                                Text {
                                    visible: !tab.pinned
                                    Layout.fillWidth: true
                                    text: tab.title !== "" && tab.title !== "about:blank" ? tab.title : Tr.t("untitled")
                                    color: tab.current ? Theme.text : Theme.textSecondary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                IconButton {
                                    visible: !tab.pinned && (tab.current || tabHover.hovered)
                                    kind: "close"
                                    box: 22
                                    tip: Tr.t("tab_close")
                                    onClicked: win.closeTab(tab.index)
                                }
                            }
                        }
                        HoverHandler { id: tabHover }
                        DragHandler {
                            id: tabDrag
                            target: null
                            acceptedButtons: Qt.LeftButton
                            yAxis.enabled: false
                            grabPermissions: PointerHandler.CanTakeOverFromItems
                            cursorShape: Qt.ClosedHandCursor
                            onActiveTranslationChanged: {
                                if (!active) return
                                const pos = tab.mapToItem(strip.contentItem, centroid.position)
                                const target = strip.indexAt(pos.x, tab.height / 2)
                                if (target >= 0 && tabs.get(target).pinned === tab.pinned)
                                    win.moveTab(tab.index, target)
                            }
                        }
                        TapHandler { acceptedButtons: Qt.LeftButton; onTapped: win.currentIndex = tab.index }
                        TapHandler { acceptedButtons: Qt.MiddleButton; onTapped: win.closeTab(tab.index) }
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: { tabMenu.target = tab.index; tabMenu.popup() } }
                    }
                }
                IconButton {
                    objectName: "newTabButton"
                    Layout.alignment: Qt.AlignVCenter
                    kind: "plus"
                    box: 28
                    tip: Tr.t("newTab")
                    onClicked: win.addTab("", true)
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    kind: "search"
                    box: 28
                    Layout.alignment: Qt.AlignVCenter
                    tip: Tr.t("tab_search")
                    onClicked: tabSearch.open()
                }
            }
        }

        // toolbar
        Rectangle {
            visible: !win.isFull
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 2
                IconButton { objectName: "backButton"; kind: "back"; tip: Tr.t("back"); enabled: win.currentView && win.currentView.canGoBack; onClicked: win.currentView.goBack() }
                IconButton { objectName: "forwardButton"; kind: "forward"; tip: Tr.t("forward"); enabled: win.currentView && win.currentView.canGoForward; onClicked: win.currentView.goForward() }
                IconButton {
                    objectName: "reloadButton"
                    kind: win.currentView && win.currentView.loading ? "stop" : "reload"
                    tip: win.currentView && win.currentView.loading ? Tr.t("stop") : Tr.t("reload")
                    onClicked: win.currentView.loading ? win.currentView.stop() : win.currentView.reload()
                }
                IconButton { objectName: "homeButton"; kind: "home"; tip: Tr.t("home"); onClicked: win.goHome() }

                TextField {
                    id: omnibox
                    objectName: "omnibox"
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    Layout.preferredHeight: 34
                    placeholderText: Tr.t("addressHint")
                    placeholderTextColor: Theme.textDisabled
                    color: Theme.text
                    selectionColor: Qt.alpha(win.accent, 0.5)
                    font.pixelSize: 13
                    leftPadding: 36
                    rightPadding: 40
                    selectByMouse: true
                    background: Rectangle {
                        radius: 17
                        color: Theme.background
                        border.width: omnibox.activeFocus ? 2 : 1
                        border.color: omnibox.activeFocus ? win.accent : Theme.border
                        LineIcon {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            kind: win.curUrl.indexOf("https://") === 0 ? "lock" : win.curBlank ? "search" : "info"
                            color: win.curUrl.indexOf("https://") === 0 ? Theme.textSecondary : win.curBlank ? Theme.textSecondary : Theme.warning
                        }
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) { text = win.curBlank ? "" : win.curUrl; selectAll() }
                        else { win.suggestions = []; win.suggestIndex = -1; text = Browser.prettyUrl(win.curUrl) }
                    }
                    onTextEdited: win.updateSuggestions()
                    onAccepted: win.acceptAddress()
                    Keys.onDownPressed: if (win.suggestions.length > 0) win.suggestIndex = Math.min(win.suggestions.length - 1, win.suggestIndex + 1)
                    Keys.onUpPressed: if (win.suggestions.length > 0) win.suggestIndex = Math.max(-1, win.suggestIndex - 1)
                    Keys.onEscapePressed: { win.suggestions = []; win.suggestIndex = -1; text = Browser.prettyUrl(win.curUrl); if (win.currentView) win.currentView.forceActiveFocus() }

                    IconButton {
                        id: starButton
                        objectName: "starButton"
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        box: 28
                        enabled: !win.curBlank
                        kind: win.starred ? "starfill" : "star"
                        active: win.starred
                        activeColor: Theme.warning
                        tip: win.starred ? Tr.t("bookmarkRemove") : Tr.t("bookmarkAdd")
                        onClicked: {
                            if (win.curBlank) return
                            if (win.starred) Browser.bookmarks.removeUrl(win.curUrl)
                            else Browser.bookmarks.add(win.curTitle, win.curUrl, "bar")
                        }
                    }
                }
                IconButton {
                    id: shieldButton
                    objectName: "shieldButton"
                    kind: "shield"
                    tip: Tr.t("shield_tip")
                    readonly property bool paused: { Browser.adblock.tick; return Browser.adblock.isPaused(win.curHost) }
                    readonly property int count: { Browser.adblock.tick; return Browser.adblock.blockedOn(win.curHost) }
                    readonly property bool ready: Browser.adblock.enabled && Browser.adblock.lists.totalRules > 0
                    active: ready && !paused
                    activeColor: win.accent
                    onClicked: shieldMenu.popup(x, y + height)
                    Rectangle {
                        visible: shieldButton.ready && !shieldButton.paused && shieldButton.count > 0
                        x: parent.width - width + 2; y: 0
                        width: Math.max(14, countText.implicitWidth + 6); height: 14; radius: 7
                        color: win.accent
                        Text {
                            id: countText
                            anchors.centerIn: parent
                            text: shieldButton.count > 99 ? "99+" : shieldButton.count
                            color: Theme.readableOn(win.accent)
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                        }
                    }
                }
                IconButton {
                    id: videoButton
                    objectName: "videoButton"
                    kind: "video"
                    tip: Tr.t("media_tip")
                    readonly property int count: { Browser.media.tick; return Browser.media.countFor(win.curHost) }
                    visible: count > 0
                    onClicked: { mediaMenu.entries = Browser.media.listFor(win.curHost); mediaMenu.popup(x, y + height) }
                    Rectangle {
                        x: parent.width - width + 2; y: 0
                        width: Math.max(14, mediaCount.implicitWidth + 6); height: 14; radius: 7
                        color: win.accent
                        Text { id: mediaCount; anchors.centerIn: parent; text: videoButton.count; color: Theme.readableOn(win.accent); font.pixelSize: 9; font.weight: Font.DemiBold }
                    }
                }
                IconButton {
                    objectName: "pipButton"
                    kind: "pip"
                    tip: win.pipView ? Tr.t("pip_back") : Tr.t("pip_tip")
                    active: win.pipView !== null
                    activeColor: win.accent
                    enabled: !win.curBlank || win.pipView !== null
                    onClicked: win.pipView ? win.bringBackVideo() : win.popOutVideo()
                }
                IconButton {
                    objectName: "downloadsButton"
                    kind: "download"
                    tip: Tr.t("downloads")
                    active: win.panel === "downloads"
                    onClicked: win.togglePanel("downloads")
                    Rectangle {
                        visible: Browser.downloads.activeCount > 0
                        x: parent.width - 12; y: 5; width: 8; height: 8; radius: 4
                        color: win.accent
                    }
                }
                IconButton {
                    id: menuButton
                    objectName: "menuButton"
                    kind: "menu"
                    tip: Tr.t("menu")
                    active: mainMenu.visible
                    onClicked: mainMenu.open()
                }
            }
            Rectangle {                           // load progress
                anchors.bottom: parent.bottom
                visible: win.currentView && win.currentView.loading
                width: parent.width * (win.currentView ? win.currentView.loadProgress / 100 : 0)
                height: 2
                color: win.accent
            }
        }

        // bookmarks bar
        Rectangle {
            visible: !win.isFull && Browser.settings.showBookmarksBar && win.barItems.length > 0   // no bookmarks on the bar yet: no bar
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.surface
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
            Row {
                id: barRow
                objectName: "bookmarksBar"
                anchors.verticalCenter: parent.verticalCenter
                x: 8
                spacing: 2
                Repeater {
                    model: win.barItems
                    delegate: Rectangle {
                        id: bm
                        required property var modelData
                        height: 26
                        width: Math.min(170, label.implicitWidth + 28)
                        radius: 7
                        color: bmHover.hovered ? Theme.surfaceHover : "transparent"
                        HoverHandler { id: bmHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { acceptedButtons: Qt.LeftButton; onTapped: win.openUrl(bm.modelData.url, false) }
                        TapHandler { acceptedButtons: Qt.MiddleButton; onTapped: win.openUrl(bm.modelData.url, true) }
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: bmMenu.popup() }
                        DMenu {
                            id: bmMenu
                            DItem { text: Tr.t("bm_openNewTab"); onTriggered: win.openUrl(bm.modelData.url, true) }
                            DItem { text: Tr.t("bm_moveOther"); onTriggered: Browser.bookmarks.move(bm.modelData.id, "other") }
                            DItem { text: Tr.t("bm_delete"); onTriggered: Browser.bookmarks.remove(bm.modelData.id) }
                        }
                        Text {
                            id: label
                            anchors.centerIn: parent
                            width: parent.width - 20
                            text: bm.modelData.title
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }

        // permission question
        Rectangle {
            visible: win.pending.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Qt.alpha(win.accent, 0.16)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: win.pending.length > 0 ? Tr.t("perm_ask", win.pending[0].origin.host || win.pending[0].origin.toString(), win.permName(win.pending[0].permissionType)) : ""
                    color: Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                BButton { objectName: "permAllow"; text: Tr.t("perm_allow"); variant: "primary"; onClicked: win.answerPermission(true) }
                BButton { objectName: "permBlock"; text: Tr.t("perm_block"); onClicked: win.answerPermission(false) }
                IconButton { kind: "close"; tip: Tr.t("close"); onClicked: win.answerPermission(undefined) }
            }
        }

        // pages + side panel
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                id: stage
                objectName: "stage"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Repeater {
                    id: views
                    model: tabs
                    onItemAdded: win.refreshCurrent()
                    onItemRemoved: win.refreshCurrent()
                    delegate: WebEngineView {
                        id: wv
                        required property int index
                        required property string startUrl
                        required property bool pinned
                        property bool poppedOut: false                   // the page is in the picture-in-picture window (it must stay visible there)
                        anchors.fill: parent
                        visible: poppedOut || index === win.currentIndex
                        profile: win.privateMode ? privateProfile : Browser.profile
                        url: "about:blank"
                        backgroundColor: Theme.background
                        Component.onCompleted: if (startUrl !== "") url = startUrl
                        onVisibleChanged: if (visible) lifecycleState = WebEngineView.LifecycleState.Active
                        Timer {
                            interval: 300000
                            running: !wv.visible && !wv.loading
                                && wv.lifecycleState === WebEngineView.LifecycleState.Active
                                && wv.recommendedState !== WebEngineView.LifecycleState.Active
                            onTriggered: wv.lifecycleState = WebEngineView.LifecycleState.Frozen
                        }
                        Timer {
                            interval: 900000
                            running: !wv.visible && !wv.pinned
                                && wv.lifecycleState === WebEngineView.LifecycleState.Frozen
                                && wv.recommendedState === WebEngineView.LifecycleState.Discarded
                            onTriggered: wv.lifecycleState = WebEngineView.LifecycleState.Discarded
                        }

                        settings.javascriptCanOpenWindows: true
                        settings.javascriptCanAccessClipboard: true
                        settings.javascriptCanPaste: true
                        settings.allowWindowActivationFromJavaScript: true
                        settings.playbackRequiresUserGesture: false
                        settings.screenCaptureEnabled: true
                        settings.pluginsEnabled: true
                        settings.pdfViewerEnabled: true
                        settings.dnsPrefetchEnabled: true
                        settings.fullScreenSupportEnabled: true
                        settings.localContentCanAccessRemoteUrls: true

                        onContextMenuRequested: request => {
                            if (poppedOut || request.isContentEditable || request.selectedText !== ""
                                    || request.linkUrl.toString() !== "" || request.mediaType !== ContextMenuRequest.MediaTypeNone)
                                return
                            request.accepted = true
                            pageMenu.popup(stage, request.position.x, request.position.y)
                        }

                        // the hiding rules and scriptlets of the page that is about to open go in BEFORE it is created (a script of the page can then not run first)
                        onNavigationRequested: request => {
                            if (!request.isMainFrame) return
                            // signed in to a Google profile: Google's own sign-in page opens with that account's address filled in
                            const hinted = win.privateMode || !ProfileSession.signedIn ? "" : Browser.googleSignInUrl(request.url, ProfileSession.email).toString()
                            if (hinted !== "") {
                                request.action = WebEngineNavigationRequest.IgnoreRequest
                                wv.url = hinted
                                return
                            }
                            win.prepareScripts(wv, request.url.toString())
                        }
                        onTitleChanged: tabs.setProperty(index, "title", title)
                        onIconChanged: tabs.setProperty(index, "icon", icon.toString())
                        onLoadingChanged: loadRequest => {
                            tabs.setProperty(index, "loading", loading)
                            if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
                                const u = url.toString()
                                if (!win.privateMode) Browser.history.add(u, title)
                                if (!win.privateMode && u !== "about:blank") zoomFactor = Browser.settings.zoomFor(Browser.hostOf(u))
                            }
                        }
                        onLinkHovered: hoveredUrl => { if (index === win.currentIndex) win.hoverUrl = hoveredUrl.toString() }
                        onNewWindowRequested: request => {
                            if (request.requestedUrl.toString() !== "")
                                win.addTab(request.requestedUrl.toString(), request.destination !== WebEngineNewWindowRequest.InNewBackgroundTab)
                        }
                        onFullScreenRequested: request => { request.accept(); win.setFull(request.toggleOn) }
                        onPrintRequested: if (index === win.currentIndex) win.printPage(wv)
                        onFindTextFinished: result => {
                            if (index === win.currentIndex)
                                win.findResult = result.numberOfMatches > 0 ? result.activeMatch + "/" + result.numberOfMatches : (findField.text !== "" ? Tr.t("findNone") : "")
                        }
                        onPermissionRequested: permission => {
                            const T = WebEnginePermission.PermissionType
                            if (permission.state === WebEnginePermission.State.Granted) { permission.grant(); return }
                            if (permission.state === WebEnginePermission.State.Denied) { permission.deny(); return }
                            if (permission.permissionType === T.Notifications && Browser.settings.blockNotifications) { permission.deny(); return }
                            win.pending = win.pending.concat([permission])
                        }
                    }
                }

                NewTabPage {
                    id: newTabPage
                    objectName: "newTabPage"
                    anchors.fill: parent
                    visible: win.curBlank
                    onVisibleChanged: if (visible) Qt.callLater(win.focusWorkspaceSearch)
                    privateMode: win.privateMode
                    onGo: text => win.navigate(text)
                    onContextRequested: position => pageMenu.popup(stage, position.x, position.y)
                    onProfileOptionsRequested: profileOptions.open()
                }

                Rectangle {                           // find in page
                    visible: win.findOpen
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 10
                    width: 340
                    height: 40
                    radius: 10
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 4
                        spacing: 4
                        TextField {
                            id: findField
                            objectName: "findField"
                            Layout.fillWidth: true
                            placeholderText: Tr.t("findHint")
                            placeholderTextColor: Theme.textDisabled
                            color: Theme.text
                            font.pixelSize: 13
                            background: null
                            selectByMouse: true
                            onTextChanged: win.doFind(false)
                            onAccepted: win.doFind(false)
                            Keys.onPressed: event => { if (event.key === Qt.Key_Return && (event.modifiers & Qt.ShiftModifier)) { win.doFind(true); event.accepted = true } }
                        }
                        Text { text: win.findResult; color: Theme.textSecondary; font.pixelSize: 12 }
                        IconButton { kind: "back"; box: 28; tip: Tr.t("back"); onClicked: win.doFind(true); rotation: 90 }
                        IconButton { kind: "forward"; box: 28; tip: Tr.t("forward"); onClicked: win.doFind(false); rotation: 90 }
                        IconButton { kind: "close"; box: 28; tip: Tr.t("close"); onClicked: win.closeFind() }
                    }
                }

                Rectangle {                           // the tab whose video is in the small window
                    objectName: "pipPlaceholder"
                    visible: win.pipView !== null && win.pipView === win.currentView
                    anchors.fill: parent
                    z: 5
                    color: Theme.background
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 14
                        LineIcon { Layout.alignment: Qt.AlignHCenter; kind: "pip"; size: 40; color: win.accent }
                        Text { Layout.alignment: Qt.AlignHCenter; text: Tr.t("pip_placeholder"); color: Theme.text; font.pixelSize: 15 }
                        BButton { Layout.alignment: Qt.AlignHCenter; text: Tr.t("pip_back"); variant: "primary"; onClicked: win.bringBackVideo() }
                    }
                }

                Rectangle {                           // a short message
                    visible: win.noteText !== ""
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 24
                    z: 30
                    width: noteLabel.implicitWidth + 28
                    height: 34
                    radius: 17
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    Text { id: noteLabel; anchors.centerIn: parent; text: win.noteText; color: Theme.text; font.pixelSize: 13 }
                }

                Rectangle {                           // the address of a link under the pointer
                    visible: win.hoverUrl !== ""
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: Math.min(parent.width * 0.6, hoverText.implicitWidth + 20)
                    height: 24
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    Text { id: hoverText; anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; text: win.hoverUrl; color: Theme.text; font.pixelSize: 12; elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter }
                }

                Rectangle {                           // address suggestions (over the page)
                    id: suggestBox
                    objectName: "suggestions"
                    visible: win.suggestions.length > 0
                    x: 80
                    y: 0
                    z: 20
                    width: Math.min(640, parent.width - 160)
                    height: suggestList.contentHeight + 8
                    radius: 12
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    ListView {
                        id: suggestList
                        anchors.fill: parent
                        anchors.margins: 4
                        interactive: false
                        model: win.suggestions
                        delegate: Rectangle {
                            id: sg
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 34
                            radius: 8
                            color: index === win.suggestIndex || sgHover.hovered ? Theme.surfaceHover : "transparent"
                            HoverHandler { id: sgHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: { win.suggestIndex = sg.index; win.acceptAddress() }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10
                                LineIcon {
                                    kind: sg.modelData.kind === "search" ? "search" : sg.modelData.kind === "bookmark" ? "starfill" : "history"
                                    color: sg.modelData.kind === "bookmark" ? Theme.warning : Theme.textSecondary
                                    size: 14
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: sg.modelData.kind === "search" ? Tr.t("searchFor", sg.modelData.title) : (sg.modelData.title || sg.modelData.url) + "  —  " + Browser.prettyUrl(sg.modelData.url)
                                    color: Theme.text
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            SidePanel {
                id: sidePanel
                objectName: "sidePanel"
                visible: win.panel !== ""
                Layout.preferredWidth: 400
                Layout.fillHeight: true
                kind: win.panel
                privateMode: win.privateMode
                onOpenUrl: (url, newTab) => win.openUrl(url, newTab)
                onCloseRequested: win.panel = ""
            }
        }
    }

    // ---- menus ---------------------------------------------------------------------------------------------------------------
    DMenu {
        id: tabMenu
        property int target: 0
        DItem { text: Tr.t("tab_reload"); onTriggered: { const v = views.itemAt(tabMenu.target); if (v) v.reload() } }
        DItem { text: Tr.t("tab_duplicate"); onTriggered: { const v = views.itemAt(tabMenu.target); if (v) win.addTab(v.url.toString(), true) } }
        DItem {
            text: tabMenu.target < tabs.count && tabs.get(tabMenu.target).pinned ? Tr.t("tab_unpin") : Tr.t("tab_pin")
            onTriggered: win.togglePin(tabMenu.target)
        }
        DSep {}
        DItem { text: Tr.t("tab_close"); onTriggered: win.closeTab(tabMenu.target) }
        DItem { text: Tr.t("tab_closeOthers"); onTriggered: win.closeOthers(tabMenu.target) }
        DItem { text: Tr.t("tab_closeRight"); onTriggered: win.closeRight(tabMenu.target) }
        DSep {}
        DItem { text: Tr.t("tab_reopen"); enabled: win.closedUrls.length > 0; onTriggered: win.reopenClosed() }
    }
    DMenu {
        id: mediaMenu
        objectName: "mediaMenu"
        property var entries: []
        Repeater {
            model: mediaMenu.entries
            delegate: DItem {
                required property var modelData
                text: (modelData.kind === "file" ? Tr.t("media_save_file") : Tr.t("media_save_stream")) + "  -  " + modelData.name
                onTriggered: {
                    if (Browser.media.save(modelData.url, modelData.kind, modelData.pageUrl || win.curUrl)) { win.panel = "downloads"; win.showNote(Tr.t("media_started")) }
                }
            }
        }
    }
    Connections {
        target: Browser.media
        function onRefused(code, detail) {
            win.showNote(code === "excluded" ? Tr.t("media_excluded") : code === "noffmpeg" ? Tr.t("media_noffmpeg") : Tr.t("media_failed", detail))
        }
    }
    DMenu {
        id: shieldMenu
        objectName: "shieldMenu"
        DItem {
            enabled: false
            text: !Browser.adblock.enabled ? Tr.t("shield_off")
                : Browser.adblock.lists.totalRules === 0 ? Tr.t("shield_nolists")
                : shieldButton.paused ? Tr.t("shield_paused")
                : Tr.t("shield_blocked", shieldButton.count)
        }
        DItem {
            text: shieldButton.paused ? Tr.t("shield_resume") : Tr.t("shield_pause")
            enabled: Browser.adblock.enabled && !win.curBlank && win.curHost !== ""
            onTriggered: Browser.adblock.setPaused(win.curHost, !shieldButton.paused)
        }
        DSep {}
        DItem { text: Tr.t("shield_settings"); onTriggered: settingsModal.open() }
    }
    DMenu {
        id: pageMenu
        objectName: "pageContextMenu"
        popupType: Popup.Item
        implicitWidth: 300
        padding: 6
        PageMenuItem { text: Tr.t("back"); shortcutLabel: Tr.t("shortcut_back"); enabled: !!win.currentView && win.currentView.canGoBack; onTriggered: win.currentView.goBack() }
        PageMenuItem { text: Tr.t("forward"); shortcutLabel: Tr.t("shortcut_forward"); enabled: !!win.currentView && win.currentView.canGoForward; onTriggered: win.currentView.goForward() }
        PageMenuItem { text: Tr.t("reload"); shortcutLabel: "Ctrl+R"; enabled: !!win.currentView; onTriggered: win.currentView.reload() }
        DSep { visible: !win.curBlank; height: visible ? implicitHeight : 0 }
        PageMenuItem { visible: !win.curBlank; enabled: visible; text: Tr.t("savePdf"); onTriggered: pdfDialog.open() }
        PageMenuItem { visible: !win.curBlank; enabled: visible; text: Tr.t("print_page"); shortcutLabel: "Ctrl+P"; onTriggered: win.printPage(win.currentView) }
        PageMenuItem { visible: !win.curBlank; enabled: visible; text: Tr.t("reader_mode"); onTriggered: win.openReader() }
        PageMenuItem { visible: !win.curBlank; enabled: visible; text: Tr.t("page_capture"); onTriggered: captureDialog.open() }
        PageMenuItem { visible: !win.curBlank; enabled: visible; text: Tr.t("viewSource"); shortcutLabel: "Ctrl+U"; onTriggered: win.addTab("view-source:" + win.curUrl, true) }
        PageMenuItem { visible: !win.curBlank; enabled: visible; text: Tr.t("devtools"); shortcutLabel: "F12"; onTriggered: win.openDevTools() }
        DSep {}
        PageMenuItem { text: Tr.t("settings"); onTriggered: win.togglePanel("settings") }
    }
        component CenterModal: Popup {
        parent: Overlay.overlay
        anchors.centerIn: parent
        popupType: Popup.Item
        modal: true
        dim: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape
        Overlay.modal: Rectangle { color: "#B000080F" }
        background: Rectangle {
            radius: 18
            color: Theme.backgroundAlt
            border.color: Theme.border
        }
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
                NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 150; easing.type: Easing.OutCubic }
            }
        }
    }

    component LockActionButton: BButton {
        id: action
        required property string glyph
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 36
        leftPadding: 6
        rightPadding: 6
        contentItem: Item {
            Row {
                anchors.centerIn: parent
                spacing: 6
                LineIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    kind: action.glyph
                    size: 14
                    color: action.variant === "primary" ? Theme.readableOn(action.fill) : Theme.textSecondary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: action.text
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: action.variant === "primary" ? Theme.readableOn(action.fill) : Theme.text
                }
            }
        }
    }

    Rectangle {
        id: lockScreen
        objectName: "profileLockScreen"
        anchors.fill: parent
        visible: ProfileSession.locked
        color: "#7208101A"
        onVisibleChanged: {
            lockPin.clear()
            if (visible) lockPin.forceActiveFocus()
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, 360)
            height: unlockForm.implicitHeight + 40
            radius: 24
            color: "#141F2A"
            border.color: "#365263"
            readonly property bool compact: win.height < 500
            ColumnLayout {
                id: unlockForm
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                ProfileAvatar {
                    visible: ProfileSession.signedIn
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: unlockForm.parent.compact ? 46 : 58
                    Layout.preferredHeight: Layout.preferredWidth
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: ProfileSession.name || Tr.t("profile_unlock")
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: ProfileSession.signedIn
                    horizontalAlignment: Text.AlignHCenter
                    text: Tr.t("profile_pin_instruction")
                    color: Theme.textSecondary
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    visible: ProfileSession.signedIn
                    spacing: 12
                    Accessible.name: Tr.t("profile_pin_progress", lockPin.length)
                    Accessible.role: Accessible.StaticText
                    Repeater {
                        model: 6
                        Rectangle {
                            required property int index
                            width: 11
                            height: width
                            radius: width / 2
                            color: index < lockPin.length ? Theme.accent : "transparent"
                            border.color: index <= lockPin.length ? Theme.accent : "#587080"
                        }
                    }
                }
                TextInput {
                    id: lockPin
                    objectName: "profileLockPin"
                    Component.onCompleted: if (ProfileSession.locked) forceActiveFocus()
                    visible: ProfileSession.signedIn
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 1
                    opacity: 0
                    Accessible.name: Tr.t("ntp_pin")
                    echoMode: TextInput.Password
                    maximumLength: 6
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                    inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData
                    onAccepted: if (unlockButton.enabled) unlockButton.clicked()
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    horizontalAlignment: Text.AlignHCenter
                    text: ProfileSession.profileError ? Tr.t(ProfileSession.profileError) : ""
                    color: Theme.error
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    Layout.alignment: Qt.AlignHCenter
                    visible: ProfileSession.signedIn
                    columns: 3
                    columnSpacing: 7
                    rowSpacing: 6
                    Repeater {
                        model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "⌫"]
                        delegate: BButton {
                            required property string modelData
                            Layout.preferredWidth: 68
                            Layout.preferredHeight: unlockForm.parent.compact ? 30 : 38
                            opacity: modelData === "" ? 0 : 1
                            enabled: modelData === "⌫" ? lockPin.length > 0 : modelData !== "" && lockPin.length < 6
                            text: modelData
                            Accessible.name: modelData === "⌫" ? Tr.t("profile_pin_delete") : modelData
                            contentItem: Text {
                                text: modelData
                                color: Theme.text
                                opacity: parent.enabled ? 1 : 0.5
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                lockPin.text = modelData === "⌫" ? lockPin.text.slice(0, -1) : lockPin.text + modelData
                                lockPin.forceActiveFocus()
                                lockPin.cursorPosition = lockPin.length
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    LockActionButton { glyph: "close"; text: Tr.t("close"); onClicked: win.close() }
                    LockActionButton {
                        glyph: "mask"
                        text: Tr.t("profile_guest")
                        onClicked: ProfileSession.openGuest(Tr.lang)
                    }
                    LockActionButton {
                        id: unlockButton
                        glyph: "lock"
                        visible: ProfileSession.signedIn
                        enabled: lockPin.length === 6
                        text: Tr.t("profile_unlock_action")
                        variant: "primary"
                        onClicked: {
                            ProfileSession.unlock(lockPin.text)
                            lockPin.clear()
                            if (lockScreen.visible) lockPin.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    CenterModal {
        id: profileOptions
        width: Math.min(440, parent.width - 40)
        padding: 20
        property bool confirmingSignOut: false
        onAboutToShow: confirmingSignOut = false
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ProfileAvatar { Layout.preferredWidth: 52; Layout.preferredHeight: 52 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        Layout.fillWidth: true
                        text: ProfileSession.name || ProfileSession.email
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: ProfileSession.email
                        textFormat: Text.PlainText
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text { Layout.fillWidth: true; text: Tr.t("profile_idle"); color: Theme.text; wrapMode: Text.WordWrap }
                RowLayout {
                    spacing: 4
                    BButton {
                        implicitWidth: 34
                        text: "−"
                        Accessible.name: Tr.t("profile_idle_less")
                        enabled: ProfileSession.idleMinutes > 1
                        onClicked: ProfileSession.setIdleMinutes(ProfileSession.idleMinutes - 1)
                    }
                    Text {
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignHCenter
                        text: Number(ProfileSession.idleMinutes).toLocaleString(Qt.locale(Tr.lang === "th" ? "th_TH" : "en_US"), "f", 0)
                        color: Theme.text
                        font.weight: Font.DemiBold
                    }
                    BButton {
                        implicitWidth: 34
                        text: "+"
                        Accessible.name: Tr.t("profile_idle_more")
                        enabled: ProfileSession.idleMinutes < 120
                        onClicked: ProfileSession.setIdleMinutes(ProfileSession.idleMinutes + 1)
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: ProfileSession.profileError ? Tr.t(ProfileSession.profileError)
                    : profileOptions.confirmingSignOut ? Tr.t("profile_signout_confirm") : ""
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                BButton { text: Tr.t("close"); onClicked: profileOptions.close() }
                BButton {
                    text: Tr.t("profile_signout")
                    variant: "danger"
                    onClicked: {
                        if (profileOptions.confirmingSignOut) ProfileSession.requestSignOut()
                        else profileOptions.confirmingSignOut = true
                    }
                }
            }
        }
    }

    CenterModal {
        id: settingsModal
        objectName: "settingsModal"
        width: Math.min(1040, parent.width - 64)
        height: Math.min(720, parent.height - 64)
        padding: 1
        contentItem: BrowserSettingsPage {
            onCloseRequested: settingsModal.close()
        }
    }

    component ToolbarMenuRow: AbstractButton {
        id: row
        property string glyph
        property string shortcutLabel
        width: parent.width
        height: 33
        leftPadding: 9
        rightPadding: 9
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.role: Accessible.MenuItem
        Accessible.name: text
        background: Rectangle {
            radius: 7
            color: row.down || row.hovered || row.visualFocus ? Theme.surfaceHover : "transparent"
        }
        contentItem: RowLayout {
            spacing: 10
            LineIcon { kind: row.glyph; size: 15; color: row.enabled ? win.accent : Theme.textSecondary }
            Text {
                Layout.fillWidth: true
                text: row.text
                color: Theme.text
                opacity: row.enabled ? 1 : 0.45
                elide: Text.ElideRight
            }
            Text {
                visible: row.shortcutLabel !== ""
                text: row.shortcutLabel
                color: Theme.textSecondary
                font.pixelSize: 11
            }
        }
    }

    Popup {
        id: mainMenu
        objectName: "mainMenu"
        parent: Overlay.overlay
        popupType: Popup.Item
        width: Math.min(350, parent.width - 16)
        height: Math.min(menuEntries.implicitHeight + 16, parent.height - y - 8)
        property point anchorPoint
        x: Math.max(8, Math.min(anchorPoint.x - width, parent.width - width - 8))
        y: anchorPoint.y + 5
        focus: true
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: 12; color: Theme.backgroundAlt; border.color: Theme.border }
        onAboutToShow: {
            anchorPoint = menuButton.mapToItem(parent, menuButton.width, menuButton.height)
            menuScroll.contentY = 0
        }

        contentItem: Flickable {
            id: menuScroll
            contentWidth: width
            contentHeight: menuEntries.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Column {
                id: menuEntries
                width: menuScroll.width
                spacing: 1
                AbstractButton {
                    id: profileRow
                    visible: ProfileSession.signedIn
                    width: parent.width
                    height: 52
                    hoverEnabled: true
                    Accessible.name: ProfileSession.name + ", " + ProfileSession.email
                    onClicked: { mainMenu.close(); profileOptions.open() }
                    background: Rectangle {
                        radius: 8
                        color: profileRow.hovered || profileRow.visualFocus ? Theme.surfaceHover : Theme.surface
                    }
                    leftPadding: 10
                    rightPadding: 10
                    contentItem: RowLayout {
                        spacing: 10
                        ProfileAvatar { Layout.preferredWidth: 36; Layout.preferredHeight: 36; Layout.alignment: Qt.AlignVCenter }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 1
                            Text { Layout.fillWidth: true; text: ProfileSession.name; textFormat: Text.PlainText; color: Theme.text; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: ProfileSession.email; textFormat: Text.PlainText; color: Theme.textSecondary; font.pixelSize: 12; elide: Text.ElideRight }
                        }
                    }
                }
                Repeater {
                    model: [
                        { key: "newTab", icon: "plus", keys: "Ctrl+T", run: () => win.addTab("", true) },
                        { key: "newWindow", icon: "plus", keys: "Ctrl+N", run: () => Browser.openWindow(false, "") },
                        { key: "newPrivate", icon: "mask", keys: "Ctrl+Shift+N", run: () => Browser.openWindow(true, "") },
                        { divider: true },
                        { key: "history", icon: "history", keys: "Ctrl+H", run: () => win.togglePanel("history") },
                        { key: "downloads", icon: "download", keys: "Ctrl+J", run: () => win.togglePanel("downloads") },
                        { key: "bookmarks", icon: "bookmark", run: () => win.togglePanel("bookmarks") },
                        { key: "tab_search", icon: "search", keys: "Ctrl+Shift+A", run: () => tabSearch.open() },
                        { divider: true, zoom: true },
                        { key: "print_page", icon: "print", keys: "Ctrl+P", needsPage: true, run: () => win.printPage(win.currentView) },
                        { key: "find", icon: "find", keys: "Ctrl+F", run: () => win.openFind() },
                        { key: "savePdf", icon: "download", needsPage: true, run: () => pdfDDialog.open() },
                        { divider: true },
                        { key: "viewSource", icon: "find", keys: "Ctrl+U", needsPage: true, run: () => win.addTab("view-source:" + win.curUrl, true) },
                        { key: "devtools", icon: "info", keys: "F12", run: () => win.openDevTools() },
                        { key: "settings", icon: "menu", run: () => settingsModal.open() }
                    ]
                    delegate: Column {
                        id: section
                        required property var modelData
                        width: menuEntries.width
                        Item {
                            visible: !!section.modelData.divider
                            width: parent.width
                            height: 11
                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 1; color: Theme.border }
                        }
                        ToolbarMenuRow {
                            visible: !section.modelData.divider
                            enabled: visible && (!section.modelData.needsPage || !win.curBlank)
                            text: section.modelData.key ? Tr.t(section.modelData.key) : ""
                            glyph: section.modelData.icon || "menu"
                            shortcutLabel: section.modelData.keys || ""
                            onClicked: {
                                mainMenu.close()
                                section.modelData.run()
                            }
                        }
                        RowLayout {
                            visible: !!section.modelData.zoom
                            width: parent.width
                            height: 38
                            spacing: 5
                            LineIcon { kind: "search"; size: 15; color: win.accent }
                            Text { Layout.fillWidth: true; text: Tr.t("menu_zoom"); color: Theme.text }
                            BButton { text: "−"; implicitWidth: 30; Accessible.name: Tr.t("zoomOut"); onClicked: win.zoomBy(-0.1) }
                            BButton {
                                implicitWidth: 55
                                text: Math.round((win.currentView ? win.currentView.zoomFactor : 1) * 100) + "%"
                                Accessible.name: Tr.t("zoomReset")
                                onClicked: win.zoomBy(0)
                            }
                            BButton { text: "+"; implicitWidth: 30; Accessible.name: Tr.t("zoomIn"); onClicked: win.zoomBy(0.1) }
                            BButton {
                                text: Tr.t("fullscreen")
                                onClicked: { mainMenu.close(); win.setFull(true) }
                            }
                        }
                        Item {
                            visible: !!section.modelData.zoom
                            width: parent.width
                            height: 11
                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 1; color: Theme.border }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: reader
        property string heading: ""
        property string article: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(780, parent.width - 40)
        height: Math.min(760, parent.height - 40)
        modal: true
        title: heading
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            TextArea {
                text: reader.article
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                font.pixelSize: 17
                color: Theme.text
            }
        }
        background: Rectangle { radius: 14; color: Theme.backgroundAlt; border.color: Theme.border }
    }

    FileDialog {
        id: captureDialog
        title: Tr.t("page_capture")
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG (*.png)"]
        defaultSuffix: "png"
        onAccepted: {
            const view = win.currentView
            const path = Browser.localPath(selectedFile)
            if (!view || !view.grabToImage(result => {
                win.showNote(Tr.t(result.saveToFile(path) ? "page_captured" : "page_capture_failed"))
            })) win.showNote(Tr.t("page_capture_failed"))
        }
    }

    Popup {
        id: tabSearch
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48)
        height: Math.min(380, parent.height - 48)
        focus: true
        modal: true
        padding: 16
        background: Rectangle { radius: 14; color: Theme.backgroundAlt; border.color: Theme.border }
        Overlay.modal: Rectangle { color: "#9900080F" }
        onOpened: {
            tabQuery.clear()
            tabResults.model = win.tabsMatching("")
            tabQuery.forceActiveFocus()
        }
        function selectTab(index) {
            close()
            win.currentIndex = index
        }
        contentItem: ColumnLayout {
            TextField {
                id: tabQuery
                Layout.fillWidth: true
                placeholderText: Tr.t("tab_search")
                Accessible.name: placeholderText
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                onTextChanged: tabResults.model = win.tabsMatching(text)
                onAccepted: if (tabResults.count > 0) tabSearch.selectTab(tabResults.model[0].tabIndex)
            }
            Text { visible: tabResults.count === 0; text: Tr.t("tab_no_results"); color: Theme.textSecondary }
            ListView {
                id: tabResults
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 3
                delegate: BButton {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.title
                    ToolTip.visible: hovered && modelData.url !== ""
                    ToolTip.text: modelData.url
                    onClicked: tabSearch.selectTab(modelData.tabIndex)
                }
            }
        }
    }

    FileDialog {
        id: pdfDialog
        title: Tr.t("savePdf")
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF (*.pdf)"]
        defaultSuffix: "pdf"
        onAccepted: if (win.currentView) win.currentView.printToPdf(Browser.localPath(selectedFile))
    }

    Loader {
        id: devLoader
        active: false
        sourceComponent: Window {
            property alias view: devView
            width: 1000
            height: 700
            title: Tr.t("devtools")
            color: Theme.background
            WebEngineView {
                id: devView
                anchors.fill: parent
                profile: win.privateMode ? privateProfile : Browser.profile
            }
        }
    }

    Timer { id: noteTimer; interval: 3500; onTriggered: win.noteText = "" }
    Component {
        id: pipComponent
        Window {                                          // the small window of the picture in picture: always on top, the page's video fills it
            objectName: "pipWindow"
            flags: Qt.Window | Qt.WindowStaysOnTopHint | Qt.WindowTitleHint | Qt.WindowCloseButtonHint | Qt.WindowMinMaxButtonsHint
            minimumWidth: 200
            minimumHeight: 120
            color: "black"
            onClosing: win.bringBackVideo()
        }
    }

    // The tab model goes after every view that writes into it (see the note at "tabs" above) ...
    ListModel { id: tabs }

    // ... and the private profile is declared LAST: Qt destroys the children of an object in the order they were made, and a profile must go after every view that uses it.
    WebEngineProfile {
        id: privateProfile
        offTheRecord: true
        httpUserAgent: Browser.profile.httpUserAgent
        onDownloadRequested: download => Browser.downloads.handle(download)
        Component.onCompleted: Browser.adblock.attach(privateProfile)         // the ad blocker works in private windows too
    }
}
