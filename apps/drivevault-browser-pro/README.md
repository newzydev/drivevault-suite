# DriveVault Browser Pro

A Chrome-like web browser for Windows (Qt 6.8, Qt WebEngine / Chromium) with a built-in ad blocker (network filters only so far). Accent colour cyan `#06B6D4`.

**Status: version 0.2.0, in development. There is no installer and no Release yet.** The plan is `docs/Browser-Pro-Plan.md`; the first tests of the engine are in `docs/Phase0b-Results.md`.

## What works in 0.2.0 (Phases 1 - 2)
Tabs (new, close, duplicate, pin, close others / to the right, reopen closed), address bar with suggestions from bookmarks and history (anything that is not an address becomes a search), back / forward / reload / home,
bookmarks (star, bookmarks bar, panel, import / export as HTML), history (search, remove, clear), downloads (pause / resume / cancel, show in folder, warning for program files), find in page, zoom per site, full screen,
save a page as PDF, view source, developer tools, private window, website permissions that ask (notifications are refused by default), restore of the tabs of last time (setting), Thai + English.

**Ad blocker engine (Phase 2):** reads filter lists (Adblock Plus / uBlock Origin syntax) and stops matching network requests before they leave the PC, in normal and private windows. Shield button with the count of the page, pause per site, a switch per list, "Update filter lists" (downloads only when you press it). **No filter list is included or downloaded yet**, so until the lists are installed nothing is blocked.

## Not available yet
Hiding page elements, scripts and the defence against anti-adblock walls (Phases 3 - 4), Picture-in-Picture and video download (Phase 5), tab dragging, import from Chrome, printing to a printer, clearing cookies, extensions, Google sign-in / sync (dropped by the owner), the installer and the card in Master Pro (Phase 6).
Video encoded with H.264 / AAC does not play (the Qt binaries have no proprietary codecs).

## Build (developers)
```powershell
.\scripts\build.ps1 -Test
```
Needs the suite's `.tools` (Qt 6.8.3 with WebEngine, see `docs\DEVELOPMENT.md` in the repository root). The tests open real windows for about a minute.
