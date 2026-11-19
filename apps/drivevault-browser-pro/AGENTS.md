# DriveVault Browser Pro - rules (adds to the repository root file)

A Chrome-like web browser for Windows: C++20, Qt 6.8 / Qt Quick, **Qt WebEngine (Chromium)**. Version 0.1.0 = Phase 1 of `docs/Browser-Pro-Plan.md` (the browser shell). **No installer, no Release, not in the Master Pro catalog yet** (Phase 6). Read the plan first; do not label a planned feature as available.
Owner's decisions (2026-11-19): Qt WebEngine for this program (the suite rule "no WebView" does not apply to it, as for Master Pro's browser tab); **no Google sign-in, no Google sync** (plan section 13); extensions are on hold (Qt 6.10.3 crashes in `setExtensionEnabled`, `docs/Phase0b-Results.md`).

## Layout
```
src/core/   no WebEngine, tested: UrlUtil (address bar rules, safe / unique file names), BookmarkStore (JSON + Netscape HTML import / export), HistoryStore (JSON lines, 20,000 max), BrowserSettings (JSON, zoom per host)
src/app/    BrowserApp (the QML singleton "Browser": stores, downloads, shared profile, helpers), DownloadModel (WebEngine downloads), main.cpp, app.rc
qml/        Main (keeps the windows alive), BrowserWindow (tabs, address bar, bookmarks bar, pages, find, permissions, menus, shortcuts, --selftest), NewTabPage, SidePanel (history / bookmarks / downloads / settings), Theme, Tr (Thai + English), LineIcon, IconButton, BButton
tests/      tst_core (unit tests), tst_smoke (starts the real exe: picture, local page, panels, private window, the window's own checks)
spike/      phase0b = the throw-away Qt 6.10 test of the plan (not built by the project)
```
Output `build\release\bin\DriveVaultBrowserPro.exe`. Data: `%LOCALAPPDATA%\DriveVault\BrowserPro` (`--data-dir <dir>` for tests: also keeps downloads in `<dir>\downloads`).

## Commands (from this folder)
```powershell
.\scripts\build.ps1 -Test        # configure + build Release + tst_core + tst_smoke (smoke opens real windows for about a minute)
..\..\scripts\run-browser-pro.bat   # smoke start / look at the program (no install)
.\scripts\make-icon.ps1          # redraws resources\icons\app.png / app.ico
```
Diagnostics of the exe: `--screenshot <png> [--size WxH] [--lang th|en] [--panel history|bookmarks|downloads|settings] [--private]`, `--selftest <json> <page address>` (17 checks of tabs, pin, bookmarks, history, suggestions, find, zoom, typed address, panels; exit 0 = all passed), `--data-dir`, `--lang`, addresses as arguments.

## Rules
- **Honest labels**: Settings says "Ad blocking: not implemented yet". Never show a switch that does nothing. Cookies are NOT cleared by "Clear history and cache" (the Quick profile has no cookie store API); the page says so.
- Private window: its own `WebEngineProfile { offTheRecord: true }`, declared LAST in `BrowserWindow.qml` (Qt destroys children in creation order; the profile must go after every view). It never writes history, session or zoom. Downloads and bookmarks made in it are kept (the page says so).
- The shared profile is made in `main.cpp` on the stack **before** the QML engine so it outlives it. Never write `profile: profile` in a view. `QQuickWebEngineProfile` has no constructor with a name: use `setStorageName`.
- Notifications are refused without asking by default (`blockNotifications`); every other permission asks (`WebEnginePermission`, remembered on disk, the bar under the toolbar). The program never opens a downloaded file; risky extensions (exe, msi, bat, ps1, js, lnk ...) get a warning (`riskyFileName`).
- The user agent drops `QtWebEngine/x` and says `Windows NT 10.0` (the engine said 6.2). This is how Chrome-based browsers present themselves; it is not a promise that every site accepts it.
- Texts: every string is in `qml/Tr.qml` (English + Thai together). QML gotchas as in Master Pro: ids are per file, a delegate with `required` properties has no `model` object, `font.family` takes one name.
- Network: nothing is downloaded by the program itself; pages load what they load. Search uses the engine the user chose (DuckDuckGo default).
- Not done yet (do not claim): tab drag-to-reorder, ad blocking (Phases 2 - 4), Picture-in-Picture and video download (Phase 5), import from Chrome, print to a printer (only "save as PDF"), clearing cookies, extensions, the installer and the Master Pro card. H.264 / AAC video does not play with the Qt binaries (no proprietary codecs, plan section 12).
