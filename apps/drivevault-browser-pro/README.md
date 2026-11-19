# DriveVault Browser Pro

A Chrome-like web browser for Windows (Qt 6.8, Qt WebEngine / Chromium) with a built-in ad blocker. Accent colour cyan `#06B6D4`.

**Status: version 0.6.1, beta. The installer is built by `scripts\package.ps1` (not install-tested by an agent), the program is listed in Master Pro, and the published release is 0.6.0.** Not done: extensions (on hold, see the plan), Google sign-in / sync (cancelled by the owner). The plan is `docs/Browser-Pro-Plan.md`; the first tests of the engine are in `docs/Phase0b-Results.md`.

## What works in 0.6.1 (Phases 1 - 6)
Tabs (new, close, duplicate, pin, close others / to the right, reopen closed), address bar with suggestions from bookmarks and history and Google as the default search engine, back / forward / reload / home,
bookmarks (star, bookmarks bar, panel, import / export as HTML), history (search, remove, clear), downloads (pause / resume / cancel, show in folder, warning for program files), find in page, zoom per site, full screen,
save a page as PDF, view source, developer tools, private window, website permissions that ask (notifications are refused by default), restore of the tabs of last time (setting), Thai + English.

The DriveVault title bar has minimize, maximize / restore, and close controls. The main window remembers its normal size and position and whether it was maximized.

**Ad blocker engine (Phase 2):** reads filter lists (Adblock Plus / uBlock Origin syntax) and stops matching network requests before they leave the PC, in normal and private windows. Shield button with the count of the page, pause per site, a switch per list, "Update filter lists" (downloads only when you press it). The installer includes snapshots of the four filter lists.

**Inside the page (Phase 3):** hides the elements the lists name (also boxes a script adds later), runs the "scriptlets" of the lists (stops tracking requests and timers, removes values from JSON, keeps scripts from reading a property ...), honours `$generichide` / `$elemhide` and exceptions, and answers a blocked ad or analytics library with a harmless stand-in file so that the page does not break (`$redirect=` and a built-in table: adsbygoogle, analytics, tag manager, ad tags, "ads.js"-style bait files). Switches: the blocker, "hide page elements and run protection scripts", "defend against pages that detect ad blockers", each list, pause per site.

**Defence against ad block detection (Phase 4):** empty "bait" boxes are not hidden, detector libraries (FuckAdBlock / BlockAdBlock style) are answered "no ad blocker", walls that say "turn off your ad blocker" are hidden and scrolling is released, and fetch / picture tests of ad addresses succeed. Shown to work on test pages made for it (each with the defence on and off: `docs/anti-adblock-results.md`). On 20 real news and portal sites the blocker kept the page text (96 - 102 % on 15 of 16 loadable sites), stopped 3 - 79 requests per page and removed most advertising frames. **On real websites (20 sites opened) no ad block wall showed up, so it has only been shown on test pages; it cannot beat every site**: pages that check on their server, or keep their content hidden until an ad was shown, can still refuse to work.

**Picture in picture and saving videos (Phase 5):** the video of a page can be popped out into a small always-on-top window (button or Alt+P; the page moves with it and the video keeps playing; videos in the main frame of the page, not inside embedded frames). When a page loads a video / audio file or an HLS / DASH playlist a button with a count appears and the file can be saved (the bundled FFmpeg copies streams into an .mp4). YouTube and subscription streaming sites are never offered, DRM-protected streams are refused, and the browser's log-in cookies are not used. Tested on test pages and local servers only.

## Not available yet
A test of the defence against a real ad block wall, hiding and scripts inside embedded frames, filters that need page analysis, picture in picture for videos in embedded frames, tab dragging, import from Chrome, printing to a printer, clearing cookies, extensions, Google sign-in / sync (dropped by the owner).
Video encoded with H.264 / AAC does not play (the Qt binaries have no proprietary codecs).

## Build (developers)
```powershell
.\scripts\build.ps1 -Test
```
Needs the suite's `.tools` (Qt 6.8.3 with WebEngine, see `docs\DEVELOPMENT.md` in the repository root). The tests open real windows for about a minute.
