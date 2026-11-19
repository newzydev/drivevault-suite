import QtQuick
import DriveVault.Browser.Backend

// Not a window itself: keeps the browser windows alive and opens new ones when the program asks (new window, new private window).
QtObject {
    id: app

    property var windows: []
    readonly property var firstWindow: windows.length > 0 ? windows[0] : null

    property Component windowComponent: Component { BrowserWindow {} }

    function open(privateMode, url, first) {
        const urls = (first && Browser.initialUrls.length > 0) ? Browser.initialUrls
                   : (first && !privateMode && Browser.settings.startup === "restore") ? Browser.loadSession()
                   : (url ? [url] : [])
        const w = windowComponent.createObject(null, { privateMode: privateMode, primary: first, startUrls: urls })
        if (!w) return
        windows = windows.concat([w])
        w.closed.connect(function () { app.windows = app.windows.filter(function (x) { return x !== w }) })
    }

    function showPanel(name) {
        if (firstWindow) firstWindow.panel = name
    }

    property Connections requests: Connections {
        target: Browser
        function onWindowRequested(privateMode, url) { app.open(privateMode, url, false) }
    }

    Component.onCompleted: open(Browser.startPrivate, "", true)
}
