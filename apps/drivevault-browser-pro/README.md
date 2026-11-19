# DriveVault Browser Pro

A Chrome-like web browser for Windows (Qt 6.8, Qt WebEngine / Chromium) with a built-in ad blocker **planned** (not implemented yet). Accent colour cyan `#06B6D4`.

**Status: version 0.1.0, in development. There is no installer and no Release yet.** The plan is `docs/Browser-Pro-Plan.md`; the first tests of the engine are in `docs/Phase0b-Results.md`.

## What works in 0.1.0 (Phase 1: the browser shell)
Tabs (new, close, duplicate, pin, close others / to the right, reopen closed), address bar with suggestions from bookmarks and history (anything that is not an address becomes a search), back / forward / reload / home,
bookmarks (star, bookmarks bar, panel, import / export as HTML), history (search, remove, clear), downloads (pause / resume / cancel, show in folder, warning for program files), find in page, zoom per site, full screen,
save a page as PDF, view source, developer tools, private window, website permissions that ask (notifications are refused by default), restore of the tabs of last time (setting), Thai + English.

## Not available yet
Ad blocking and the defence against anti-adblock walls (Phases 2 - 4), Picture-in-Picture and video download (Phase 5), tab dragging, import from Chrome, printing to a printer, clearing cookies, extensions, Google sign-in / sync (dropped by the owner), the installer and the card in Master Pro (Phase 6).
Video encoded with H.264 / AAC does not play (the Qt binaries have no proprietary codecs).

## Build (developers)
```powershell
.\scripts\build.ps1 -Test
```
Needs the suite's `.tools` (Qt 6.8.3 with WebEngine, see `docs\DEVELOPMENT.md` in the repository root). The tests open real windows for about a minute.
