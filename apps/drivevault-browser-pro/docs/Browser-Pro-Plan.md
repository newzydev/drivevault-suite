# DriveVault Browser Pro - plan

Status: **PLANNED, no code yet.** Nothing here is available to users. Written 2026-11-19. Open decisions are in section 9; defaults are marked "recommended".

## 1. What it is

A Chrome-like web browser for Windows with a strong built-in ad blocker and a defence against "anti-adblock" walls (sites that refuse to work when they detect a blocker). Chrome is the model for layout and behaviour (tabs, omnibox, bookmarks, history, downloads, find, zoom, DevTools, private window). Same stack as the rest of the suite: C++20, Qt 6.8 / Qt Quick, CMake. Accent colour proposal: cyan `#06B6D4` (free: violet, red, amber, blue and emerald are taken).

## 2. Engine choice (needs the owner's confirmation, see 9.1)

Qt WebEngine (Chromium). There is no realistic native alternative for a full web browser. The suite rule "no WebView" was relaxed for Master Pro's browser tab by the owner on 2026-11-17; this program **is** a browser, so the same exception is needed for it. Facts already proven in this repository: Qt WebEngine 6.8.3 is in `.tools`, Master Pro uses it (`qml/BrowserView.qml`: tabs, permission bar, downloads, persistent profile) and the installer carries it (about 150 MB).

Limits of Qt WebEngine that shape the plan (verify each in Phase 0, do not assume):
- **No Chrome extension support** (uBlock Origin / Adblock Plus cannot be installed). The blocker must be built in. This is also what the owner asked for.
- Network-level blocking is possible: `QWebEngineUrlRequestInterceptor` sees every request of every frame (URL, first-party URL, resource type) and can `block()` or `redirect()`. It runs on the IO thread: the filter lookup must be fast and lock-free.
- Page-level changes are possible: `QWebEngineScript` (document start, main world or isolated world) and injected CSS.
- Not possible or unproven: blocking WebSocket / WebRTC by URL, request body inspection, and Picture-in-Picture / DevTools behaviour in Qt's Chromium build (Phase 0 checks).

## 3. Ad blocker design

### 3.1 Layers (in the order they act)
1. **Network filters** (`||example.com^`, `$third-party`, `$script`, `$image`, `$domain=`, `@@` exceptions, `$important`). Parsed into a token index (hash of the rarest token of each rule) so a request is matched in microseconds. In `QWebEngineUrlRequestInterceptor`.
2. **Cosmetic filters** (`##.ad-banner`, `example.com##div`, `#@#` exceptions, `:has()`, `:-abp-has` style procedural filters where Chromium cannot do it natively). Injected as one stylesheet per page at document creation, plus a small observer script for procedural ones.
3. **Scriptlets** (`example.com##+js(abort-on-property-read, ...)`, the uBlock Origin syntax): fixed, reviewed JavaScript snippets run in the page's main world at document start. Built-in set: `abort-on-property-read/write`, `abort-on-stack-trace`, `set-constant`, `json-prune`, `no-fetch-if`, `no-xhr-if`, `remove-attr/class`, `prevent-addEventListener`, `nowebrtc`, `window.open` guard, timer speed-up for "wait N seconds" walls.
4. **Neutered resources (redirects)**: for ad-library URLs that pages test for (`adsbygoogle.js`, `ads.js`, `google-analytics.js`, `googletag`, `doubleclick` stubs) the request is *redirected* to an empty but well-behaved stub (custom scheme handler `dvres:`), not blocked. A blocked bait script is the most common detection; a stub that defines the expected globals passes the check.
5. **Anti-adblock countermeasures** (section 4).

### 3.2 Categories (mirror what the owner listed; each is a switch in Settings > Ad blocking)
| Category (owner's list) | How it is done | Source |
|---|---|---|
| Ads (base) | network + cosmetic filters | EasyList |
| Extra tracking protection | network filters | EasyPrivacy |
| Social media icon tracking | network + cosmetic | Fanboy's Social Blocking list |
| Basic distraction control (corner video boxes, pay / install / shop prompts) | cosmetic + network | Fanboy's Annoyance list parts, uBlock "Annoyances" |
| Block push notifications | the browser **denies the notifications permission by default** (own permission policy) + filters for push-prompt scripts | built in + Annoyance lists |
| Block cookie consent pop-ups | cosmetic hide + scriptlet that clicks "reject / necessary only" where a list says so | EasyList Cookie / uBlock "Cookie notices" |
| Enhanced distraction control (newsletter pop-ups, floating video, site notifications, survey requests) | cosmetic + scriptlets + `window.open` / overlay guard | Annoyance lists |
| Language filter lists | one regional list per language the user picks, Thai first | the exact Thai list must be checked before it is added (do not guess its name) |

Every list is shown by name, version date, rule count and licence. Users can add a custom list URL and write own rules (`My filters`). Per-site pause (shield button in the address bar, like Chrome's site controls) and a "Report broken site" that only copies text locally.

### 3.3 Lists: licences and network use
- Common lists (EasyList, EasyPrivacy, Fanboy's) are CC BY-SA 3.0 and/or GPLv3+ **data**, not linked code. They need an entry in `THIRD-PARTY-NOTICES` and a decision for the release gate "every file has an owner" (9.2). uBlock Origin's own resources (scriptlets, redirects) are GPLv3: **do not copy that code**. The scriptlets here are written fresh, behaving the same, and only the *rule syntax* is supported.
- Owner rule (root CLAUDE.md): nothing downloads unless the user presses a button. So: the installer ships a **snapshot** of the lists (made at build time, with date shown), and Settings has a button "Update filter lists" (user pressed, from the official list URLs, hash recorded). No background polling. Optional later: a setting "update lists once at start" off by default, as in Master Pro's update check - owner's decision.

### 3.4 Engine implementation (recommended: own C++)
`src/adblock/`: `FilterParser`, `NetworkMatcher`, `CosmeticStore`, `ScriptletLibrary`, `ListManager`, `Interceptor`. Pure C++ / no WebEngine inside the first four so they are unit-tested like Master Pro's `BrowserBridge`. Alternative considered: Brave's `adblock-rust` (MPL-2.0, mature, fast) through FFI - rejected as default because it needs a Rust toolchain in `.tools` (a download) and a build-system change; revisit if the own matcher misses in the benchmark (9.3).
Target: 150,000 rules loaded < 1.5 s in the background thread, match time < 50 microseconds per request, memory < 150 MB for the lists. These are targets, to be measured and reported, not claims.

## 4. Anti-adblock defence (the owner's hard requirement)

Honest scope first: this is a cat-and-mouse game. No one can promise "every site". YouTube-style server-side ad insertion and sites that check ads **server-side** (e.g. an ad must load and call back to the server) cannot be solved by blocking in the client alone. The plan makes the common cases work and measures the result; the UI will never say "bypasses everything".

How common detectors work and what answers each:
| Detector | Answer |
|---|---|
| Bait element (`div.adsbox`, `#ad`, `.ad-banner`) whose height becomes 0 | the cosmetic layer never `display:none`s a bait-sized element on first load; where needed it keeps the box laid out (`visibility:hidden` + size kept) and a scriptlet reports the real size from `offsetHeight` / `getComputedStyle` |
| Bait script (`ads.js`, `adsbygoogle.js`, `pagead2.googlesyndication.com`) fails to load or its global is missing | redirect to a stub that defines the expected globals (section 3.1 layer 4) |
| Libraries (FuckAdBlock / BlockAdBlock / Admiral / Sourcepoint / Detect.js) | scriptlets: `set-constant` of their detect result, `abort-on-property-read` of their entry points, `prevent-addEventListener` for their "blocked" event |
| `fetch` / `XMLHttpRequest` bait to ad URLs that must succeed | `no-fetch-if` / `no-xhr-if` return a fake successful empty answer |
| Overlay "please disable your ad blocker" + scroll lock | cosmetic removal + `remove-class` of `no-scroll` on `body` / `html`, restore `overflow` |
| `MutationObserver` that notices removed ad nodes | scriptlet hides by CSS only, and wraps the observer for known libraries |
| Timers / "wait" walls | timer speed-up scriptlet |

Rules for these come from the lists' anti-adblock sections (EasyList Anti-Adblock, uBlock "Annoyances" anti-adblock part, Adblock Warning Removal) **and** the built-in generic scriptlets above, so a new site does not need a list update for the common libraries.

**Test lab** (the only honest way to say what works): `tests/lab/` holds local pages that each imitate one detector (bait div, bait script, fetch probe, library stub, overlay + scroll lock, timer wall). The test opens each with the blocker on and checks that the page's own detector says "no blocker" and the content is readable. A second, **manual** table of real sites (owner chooses, network use only when the owner asks) records pass / fail per date in `docs/anti-adblock-results.md`. Failing sites stay listed as failing.

Not in scope, on purpose: DRM circumvention, account / paywall bypass for paid content, and hiding the browser from fraud checks. Anti-adblock bypass of free ad-supported pages is a client-side choice of the user.

## 5. Browser shell (Chrome as the model)

Phase 1 scope, built on what Master Pro's `BrowserView.qml` already solved (tabs, permission bar with remembered choices, downloads to the Downloads folder with a warning for risky types, `Profile` declared last to avoid the exit crash, own user agent, open settings): copy and adapt it first; extract it into `shared/` only when two programs need identical code (do not destabilise Master Pro).
- Tabs (drag to reorder, pin, duplicate, close others, reopen closed, tab search), omnibox (address, host, search words; search engine choice, default DuckDuckGo as in Master Pro), back / forward / reload / home, find in page, zoom per site, full screen, print (`printToPdf` / system print), view source, DevTools (`inspectedView`; needs a check in Phase 0).
- Bookmarks (bar + manager, import / export HTML), history (page + clear), downloads page with pause / resume / show in folder, Settings (privacy, ad blocking, appearance, search, languages, about).
- Private window (off-the-record profile; no history, no cookies kept).
- Permissions as in Master Pro (ask, remember, reset). Notifications default **Block**.
- Google account, extensions and sync: **added by the owner's request 2026-11-19, see section 11** (it changes what section 2 says about extensions).
- Not planned: a built-in password manager (storing passwords needs its own security design; if wanted, a separate decision with Windows DPAPI).

## 6. Other features

| Feature | Plan | Check first |
|---|---|---|
| Picture-in-Picture | a toolbar button + hotkey: a fixed script finds the playing / largest `<video>` and calls `requestPictureInPicture()` | does Qt's Chromium build expose the PiP window? If not, fall back to our own always-on-top mini window that renders the video element (works for non-DRM video) |
| Video download | the request interceptor lists media URLs the page loads (`.mp4`, `.webm`, `.m4a`, `.m3u8` / `.mpd`); a button saves a direct file with Qt network, and HLS with the suite's FFmpeg (LGPL, already in `.tools`) | **DRM-protected streams (EME / Widevine) are never touched**; whether YouTube and similar are in or out is the owner's decision (9.4, recommended: out - terms of service) |
| Reader mode | later | |
| Screenshot of page / area | later | |
| Tab sleeping to save memory | later (`lifecycleState`) | |

## 7. Suite integration (contract in `docs/suite-integration.md`)

| Item | Value (proposal) |
|---|---|
| Folder | `apps/drivevault-browser-pro` |
| Product key (registry `Software\DriveVault\Products\<Key>`) | `BrowserPro` |
| Manifest id / data folder | `drivevault.browser` / `BrowserPro` (`%LOCALAPPDATA%\DriveVault\BrowserPro`: profile, filter lists, bookmarks) |
| Setup file | `DriveVault-Browser-Pro-Setup-x64.exe` (+ `.sha256`) |
| Release tag slug | `drivevault-browser-pro` (add to `slugForKey` in Master Pro) |
| Inno `AppId` | a new GUID, generated once at the start of Phase 6 and never changed |
| Master Pro | add `BrowserPro` to the catalog with accent colour; the card shows the real status only (Available once a Release exists) |
| Installer | `installer/windows/browser.iss` including `shared/installer/master-hub-code.iss` like `pdf.iss`; ships Qt WebEngine + the list snapshot |
| Other places | root `README.md` table, root `CLAUDE.md` / `AGENTS.md` table, `docs/suite-integration.md` (new product row), release gates (license audit, clean-machine check, notices incl. Chromium and each list) |

Master Pro's `docs/suite-integration.md` currently lists only Video Editor, PDF Pro, Image and Audio as products: update it in the same change that adds the catalog entry.

## 8. Phases (each ends with tests and a smoke start; versions start at 0.1.0 and the first Release is 1.0.0 only when the owner says so)

| Phase | Content | Done when |
|---|---|---|
| 0 Spike (about 1 day, no downloads) | tiny Qt Quick app with one `WebEngineView`; interceptor blocks a host and redirects a script to a `dvres:` stub; run one bait page; check PiP, DevTools, notification permission, user agent | a short written result in this folder: what works, what does not |
| 1 Browser shell | section 5 without ad blocking; `build.ps1 -Test`, `--screenshot` smoke | a user can browse, bookmark, download |
| 2 Filter engine | parser, matcher, list manager, snapshot lists, Settings page with the categories; benchmark | unit tests on fixtures; load time and match time measured and reported |
| 3 Cosmetic + scriptlets + redirects | layers 2 - 4 | lab pages pass for cosmetic hiding and stubs |
| 4 Anti-adblock | section 4 table, `tests/lab/`, results table | every lab detector passes; real-site table filled by the owner's sites |
| 5 PiP, video download, polish | section 6 | PiP and a direct mp4 / HLS download verified by hand |
| 6 Packaging | installer, notices, Master Pro catalog entry, docs, README, version 1.0.0 candidate | `package.ps1` gates pass (installer built, **not** install-tested: rule 3a) |

## 9. Decisions the owner must make

1. **Qt WebEngine for this program** (needed; recommended yes).
2. **Filter lists**: allow shipping the community lists (CC BY-SA / GPL data) as a snapshot with notices, and the "Update filter lists" button that downloads from their official addresses when the user presses it (recommended yes). The exact lists are re-checked, with their current licences, before Phase 2.
3. **Engine**: own C++ matcher (recommended) or Brave `adblock-rust` (needs a Rust toolchain download).
4. **Video download scope**: direct files and non-DRM HLS from any site (recommended) / also YouTube (not recommended).
5. **Accent colour**: cyan `#06B6D4` (recommended) or other.
6. Whether the first Release should wait for Phase 4, or ship after Phase 3 as 0.x with the real status "Beta" in the Master Pro card.

## 10. Risks

- Anti-adblock changes often: lists and scriptlets will need updates after release; the program says so and shows the list date.
- Chromium security updates arrive only with a new Qt release: keep a Qt update routine and show the Chromium version in About.
- Installer size (about 150 MB for WebEngine, more with lists) and memory use per tab; measure in Phase 1.
- Qt WebEngine gaps (PiP, DevTools, extension APIs, some media codecs: proprietary codecs such as H.264 need a build option that may not be in the Qt binaries in `.tools`): checked in Phase 0 before promising them.

## 11. Google sign-in, Chrome extensions, Google sync (owner's request 2026-11-19)

What was found (web search 2026-11-19; each point is re-verified in Phase 0 on the real build):

| Wish | What is possible | What is not |
|---|---|---|
| **Sign in to Google** | (a) Sign in **inside a page** (gmail.com, youtube.com): Google blocks sign-in from embedded browser frameworks ("Couldn't sign you in. This browser or app may not be secure", policy since 2021, [Google's note](https://developers.googleblog.com/2020/08/guidance-for-our-effort-to-block-less-secure-browser-and-apps.html)). It may or may not work for Qt WebEngine today: Phase 0 tries it with the plain Chrome-like user agent and reports. We do **not** build on a user-agent trick, which breaks without notice and is against what Google asks of embedded browsers. (b) **"Sign in with Google" for the program itself** through Google's official OAuth 2.0 installed-app flow: the sign-in opens in the user's system browser, the program receives the token on `127.0.0.1` (PKCE). This is the method Google recommends and it works. | A guarantee that gmail.com / YouTube sign-in works inside the program's own pages. |
| **Extensions from Google** | Qt WebEngine **6.10 and later** has `QWebEngineExtensionManager` (install a zipped / unpacked extension, loaded at start). Only **Manifest V3** extensions; not in private windows; the installed ones start disabled until the program enables them. The project is on Qt 6.8.3, so this needs a Qt upgrade (a download, owner's permission, and a re-check of Master Pro, which uses the same Qt). | The Chrome Web Store "Add to Chrome" button is Chrome's own code: the program needs its own "Install from the Web Store" step (fetch the CRX from the Store address with the user pressing the button, check it, hand it to the manager). Whether the Store terms allow it for a non-Chrome browser, and how much of the extension API the Qt layer implements (e.g. `webRequest` blocking, `declarativeNetRequest`, popups, `chrome.storage`), is unverified: Phase 0 tests with 3 real extensions chosen by the owner. Manifest V2 extensions (old uBlock Origin) will not load. Many extensions will only partly work. |
| **Sync with Google** (history, profile, bookmarks, ...) | **Own sync through the user's Google Drive** (`drive.appdata` scope = a hidden app folder, no access to the user's other files): bookmarks, history (bounded), settings, my filters, open tabs list, encrypted on this PC before upload (key from a user passphrase; Google cannot read it). Conflict rule: per-item last-write-wins with timestamps, deletions kept as tombstones. Works across PCs with DriveVault Browser Pro. | **Real Chrome Sync.** It is a private Google service reserved for Google Chrome builds (access needs Google's Chrome API keys); a third-party browser cannot connect to it, and our bookmarks will **not** appear in Google Chrome or the other way round. Passwords and payment cards are not synced (no password manager, section 5). |

Prerequisites the owner must provide: a Google Cloud project with an OAuth client (type "Desktop app") and the consent screen; `drive.appdata` is a non-sensitive scope (no verification needed for it, but the project must be set up by the owner's Google account). The client ID goes into the program (a desktop client secret is not secret; treat it as public, unlike the NVIDIA key rule in Master Pro: nothing else is embedded).

New phases (after the engine phases; Qt upgrade first):
| Phase | Content | Done when |
|---|---|---|
| 0b | on Qt 6.10+ (separate build folder, no change to Master Pro): load 3 MV3 extensions through `QWebEngineExtensionManager`; try Google sign-in inside a page; list what works | written result |
| 7 Google account | OAuth installed-app flow in the system browser, token in Windows Credential Manager (never in settings), sign-out | sign in and out verified by hand |
| 8 Sync | Drive appdata store, encryption, merge rules, "Sync now" + a switch per data type, status text that says the last real sync time | two profiles on one PC sync bookmarks both ways in a test; real Drive tested by hand with the owner's account |
| 9 Extensions | Qt upgrade done, extensions page (install from file, from Web Store address, enable / disable / remove, permissions shown), compatibility notes per tested extension | the 3 test extensions run; failures listed in the UI as "may not work fully" |

Honest labels in the UI: "Signed in to Google (sync only)" and "Extensions (Manifest V3, partial)". The program never says "Chrome Sync" and never implies that gmail.com sign-in works unless Phase 0b showed it.

## 12. Phase 0b result (2026-11-19) - read `Phase0b-Results.md`

Short version: the blocker design (interceptor block + redirect to a custom-scheme stub) is **confirmed to work**. Three assumptions of this plan are **not** confirmed on Qt 6.10.3: (1) extensions - `setExtensionEnabled` crashes, so extension support (section 11, Phase 9) is on hold until Qt 6.11 / 6.12 is tried or a Qt fix is found; (2) H.264 / AAC video does not play (no proprietary codecs in the Qt binaries) - needs an owner decision (build Qt WebEngine ourselves, or accept VP9 / AV1 only); (3) Picture-in-Picture is off in the engine, so the own mini-window fallback is the plan. Section 3 (filters) and sections 5 - 6 are unaffected. Google sign-in inside a page is still untested (manual).

## 13. Owner decision 2026-11-19: no Google sign-in in the program

The owner dropped Google sign-in. Consequences: **Phase 7 (Google account) and Phase 8 (sync through Google Drive) are cancelled**; no OAuth client, no Drive access, no Credential Manager token. Section 11 stays only as the record of what was found. Extensions (Phase 9) stay on hold (Phase0b-Results.md). Bookmarks, history and settings live only in `%LOCALAPPDATA%\DriveVault\BrowserPro`; a later local export / import (bookmarks as HTML, already in Phase 1) is the only way to move them. Signing in to a website with a Google account inside a page is the website's own matter; the program does not promise it (untested).

## 14. Phase 2 result (2026-11-19, version 0.2.0) and the filter list sources

Built and tested: parser, indexed rule set, list manager with the Update button (downloads only when pressed), interceptor in normal and private windows, shield button, per-list switches. Measured on a SYNTHETIC list of 150,000 rules: 0.4 s to load, 6 us per request; with Chromium, a blocked request never reached a local server. **No real list has been downloaded or measured yet** (waiting for the owner's go-ahead, below).

Sources checked on 2026-11-19 (a HEAD request each, no list body read; the servers did not report sizes):
| List | Address | Answer | Licence |
|---|---|---|---|
| EasyList | `https://easylist.to/easylist/easylist.txt` | 200 | GPLv3+ or CC BY-SA 3.0+ ([licence page](https://easylist.to/pages/licence.html)) |
| EasyPrivacy | `https://easylist.to/easylist/easyprivacy.txt` | 200 | same |
| Fanboy's Social Blocking | `https://easylist.to/easylist/fanboy-social.txt` | 200 | **to be read from the file header** (the EasyList licence page names only EasyList, EasyPrivacy, EasyList Germany and Italy; one source says CC BY 3.0, another says GPL / CC BY-SA) |
| Fanboy's Annoyance | `https://easylist.to/easylist/fanboy-annoyance.txt` | 200 | same doubt |
| Cookie notices (Fanboy's Cookiemonster) | `https://secure.fanboy.co.nz/fanboy-cookiemonster.txt` | 200 | unknown, read from the file; NOT in the catalog yet (`easylist-cookie.txt` on easylist.to and adblockplus.org answered 404) |
| Thai (EasyList Thailand) | address not found | - | unknown; NOT in the catalog |

Before the first Release the licence of every shipped list is read from its own header, written into `THIRD-PARTY-NOTICES`, and the release gate "every file has an owner" gets an entry for the list snapshot (plan 3.3).

**The owner allowed the download on 2026-11-19** (4 lists, 5.7 MB in all, into `.tools\browser-test-data`, not committed, not shipped). Done through the program's own Update code (`--update-lists`): 4 updated, 0 failed, SHA-256 and date kept in `<id>.meta.json`.
- **Licence**: the header of each of the four files says `Licence: https://easylist.to/pages/licence.html`, i.e. the EasyList repository licence (GPLv3+ or CC BY-SA 3.0+, attribution "The EasyList authors"). The doubt about the two Fanboy lists is therefore settled for these files as downloaded; re-read the headers before every release.
- **Content** (`easylist` 2.06 MB, `easyprivacy` 1.52 MB, `fanboy-social` 0.41 MB, `fanboy-annoyance` 1.74 MB): network rules 49,868 / 56,688 / 657 / 4,203 = **111,416**; cosmetic lines 24,794 / 41 / 13,250 / 45,304 (Phase 3); lines the parser skips 3,174 / 12 / 11 / 15 (regular expressions, `$removeparam`, `$csp`, `$popup` and others).
- **Speed** (release build, this PC): loaded in 318 ms; 15.5 us per request over 40,000 ordinary requests (0 of them blocked, as expected).
- **Correctness probes**: `pagead2.googlesyndication.com/.../adsbygoogle.js` (EasyList), `google-analytics.com/analytics.js` and `connect.facebook.net/.../fbevents.js` (EasyPrivacy), `securepubads.g.doubleclick.net/.../gpt.js` (EasyList) are blocked; Wikipedia, wikimedia images, jsdelivr, Google Fonts and the YouTube player script are allowed. Note: EasyPrivacy also blocks `googletagmanager.com`, which some sites need - Phase 3 / 4 answer that with stand-in resources.
- Not done: real sites in the real browser; the cookie list and a Thai list (see the table above).

## 15. Phase 3 result (2026-11-19, version 0.3.0): inside the page

Built: cosmetic filters (specific, generic names found by watching the DOM, complex generic, injected styles, `#@#` exceptions, `$generichide` / `$elemhide` site switches), 22 scriptlets written for this program with the short names the lists use, scriptlet and cosmetic lines of the lists parsed (what is not understood is counted and skipped), stand-in files (`$redirect=` and a built-in table of ad / analytics libraries) served by a `dvres:` scheme, the script of a page delivered through `userScripts` BEFORE the navigation (`onNavigationRequested`), a switch "hide page elements and run protection scripts".

**Measured / proved** (this PC, release build):
- Real lists (EasyList, EasyPrivacy, Fanboy Social and Annoyance, downloaded 2026-11-19 with the owner's go-ahead): **82,563 page rules + 30 scriptlets accepted**, 3,817 lines skipped as unsupported (network and page together); loading all of it 0.48 s. The script of one page is composed in 4.7 ms and is about 680 KB (mostly the names of the ~49,000 generic rules).
- Page load of a trivial local page, real lists on vs off (5 runs each, median): DOMContentLoaded 161 ms vs 168 ms: **no cost that could be measured** in this test (noise is larger). Real websites were not measured.
- `tst_lab` (Chromium, pages on this computer): hiding by generic id / class / complex selector, by site rule, by injected style, an element a script adds later, exception, `$generichide`, `$elemhide`, private window, switch off, pause per site, blocker off - all as expected; 12 scriptlets each shown to act and to do nothing when cosmetic is off or the site is paused (a blocked `fetch` / XHR never reaches the server); stand-ins: `$redirect=noopjs`, `$redirect=1x1.gif`, an `ads.js` bait (canRunAds becomes true), `adsbygoogle.js` (global `loaded`), a rule with no stand-in is stopped, and with the blocker off the real files arrive.

**Not done / not tested**: scriptlet lists (uBlock filters, Adblock Warning Removal List and the like) are not chosen and not downloaded; hiding and scriptlets inside iframes; procedural selectors; `remove-node-text`, `json-edit`; the defence layer of Phase 4 (bait elements, detector libraries, overlays); every test is on pages on this computer, none on real websites.