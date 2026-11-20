# DriveVault Browser Pro

A Chrome-like web browser for Windows (Qt 6.8, Qt WebEngine / Chromium) with a built-in ad blocker. Accent colour cyan `#06B6D4`.

**Status: version 1.0.0 is the first published stable release.** The program is listed in Master Pro. The plan is `docs/Browser-Pro-Plan.md`.

## Local profiles in 1.2.0

The browser opens on its New Tab page. Use "Sign in with Google" or "Continue as guest" on the page. After sign-in Browser Pro reopens automatically with the account profile. Guest mode continues to use the existing local browser data. A signed-in profile stores its own browsing data under a directory keyed by Google's stable account ID. Google supplies the name, email and avatar. Browsing data stays on the PC. Sign-out keeps the local profile files so the same account can use them again.

Use a Google OAuth **Desktop app** client. Starting in 1.3.2, build.ps1 embeds the client pair from the downloaded Desktop app JSON selected by `DV_GOOGLE_OAUTH_FILE`. Web application clients stay on their server. Builds without Desktop app credentials show an inline unavailable message. End users of this build can sign in directly. The generated header stays under the ignored build directory. The ciphertext and key are both in the executable, so this obscures plain-text strings and provides no secrecy from reverse engineering. The authorization page opens in the system browser and returns through a loopback redirect with PKCE. OAuth tokens are used only during sign-in. Windows DPAPI protects the imported configuration and saved identity record.

Set a six-digit PIN after the first Google sign-in. It is required on restart and after ten idle minutes by default. Open the profile row in the three-dot menu to adjust the delay from 1 to 120 minutes or sign out. The PIN locks the app interface; local browser files remain accessible to the Windows account that owns them.

## Changes in 1.1.1

The toolbar menu opens below the three-dot button, with grouped commands, keyboard shortcuts and inline zoom controls.

## Changes in 1.1.0

Workspace shortcuts have their own storage and a three-dot menu to edit or remove them. Open tabs can be reordered and searched with Ctrl+Shift+A. Settings can clear cookies and reset saved permissions by website. The bookmarks panel can import a Chrome profile's Bookmarks file.

The page menu can print through the Windows printer dialog, open a text reader, and capture the visible page to a PNG. Chromium can freeze idle background tabs after five minutes and unload unpinned tabs after a further 15 minutes when it recommends doing so. Selecting an unloaded tab reloads its page. Picture in picture can use video in same-origin frames. The ad blocker recognizes simple `:-abp-has` selectors.

## What works in 1.0.0 (Phases 1 - 6)
Tabs (new, close, duplicate, pin, close others / to the right, reopen closed), address bar with suggestions from bookmarks and history and Google as the default search engine, back / forward / reload / home,
bookmarks (star, bookmarks bar, panel, import / export as HTML), history (search, remove, clear), downloads (pause / resume / cancel, show in folder, warning for program files), find in page, zoom per site, full screen,
save a page as PDF, view source, developer tools, private window, website permissions that ask (notifications are refused by default), restore of the tabs of last time (setting), Thai + English.

The DriveVault title bar has a Thai / EN switch, a Settings button, minimize, maximize / restore, and close controls. The main window remembers its normal size and position and whether it was maximized.

**Ad blocker engine (Phase 2):** reads filter lists (Adblock Plus / uBlock Origin syntax) and stops matching network requests before they leave the PC, in normal and private windows. Shield button with the count of the page, pause per site, a switch per list, "Update filter lists" (downloads only when you press it). The installer includes snapshots of the four filter lists.

**Inside the page (Phase 3):** hides the elements the lists name (also boxes a script adds later), runs the "scriptlets" of the lists (stops tracking requests and timers, removes values from JSON, keeps scripts from reading a property ...), honours `$generichide` / `$elemhide` and exceptions, and answers a blocked ad or analytics library with a harmless stand-in file so that the page does not break (`$redirect=` and a built-in table: adsbygoogle, analytics, tag manager, ad tags, "ads.js"-style bait files). Switches: the blocker, "hide page elements and run protection scripts", "defend against pages that detect ad blockers", each list, pause per site.

**Defence against ad block detection (Phase 4):** empty "bait" boxes are not hidden, detector libraries (FuckAdBlock / BlockAdBlock style) are answered "no ad blocker", walls that say "turn off your ad blocker" are hidden and scrolling is released, and fetch / picture tests of ad addresses succeed. Shown to work on test pages made for it (each with the defence on and off: `docs/anti-adblock-results.md`). On 20 real news and portal sites the blocker kept the page text (96 - 102 % on 15 of 16 loadable sites), stopped 3 - 79 requests per page and removed most advertising frames. **On real websites (20 sites opened) no ad block wall showed up, so it has only been shown on test pages; it cannot beat every site**: pages that check on their server, or keep their content hidden until an ad was shown, can still refuse to work.

**Picture in picture and saving videos (Phase 5):** the video of a page can be popped out into a small always-on-top window (button or Alt+P; the page moves with it and the video keeps playing; videos in the main frame of the page, not inside embedded frames). When a page loads a video / audio file or an HLS / DASH playlist a button with a count appears and the file can be saved (the bundled FFmpeg copies streams into an .mp4). YouTube and subscription streaming sites are never offered, DRM-protected streams are refused, and the browser's log-in cookies are not used. Tested on test pages and local servers only.

## Not available yet
A test of the defence against a real ad block wall, hiding and scripts inside embedded frames, advanced filters beyond simple `:-abp-has`, picture in picture for cross-origin frames, H.264/AAC playback with this Qt build, extensions, and Google Sync.
Video encoded with H.264 / AAC does not play (the Qt binaries have no proprietary codecs).

## Build (developers)
```powershell
.\scripts\build.ps1 -Test
```
Needs the suite's `.tools` (Qt 6.8.3 with WebEngine, see `docs\DEVELOPMENT.md` in the repository root). The tests open real windows for about a minute.

The Google shortcut uses the favicon published by Google at https://www.google.com/favicon.ico.
