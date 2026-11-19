# Phase 0b spike results (2026-11-19)

Throw-away test program: `spike/phase0b` (Qt Widgets + `QWebEngineView`, Qt **6.10.3** msvc2022_64 with WebEngine from `.tools\Qt-6.10`, installed with aqtinstall next to 6.8.3, which is untouched). A local `python -m http.server` on 127.0.0.1:8765 serves `web/`. Each line says what was **measured** on this PC. Nothing here is a release claim.

## Works
| Check | Result |
|---|---|
| `QWebEngineUrlRequestInterceptor` blocks a request | `/ads.js` and a `fetch('/pagead/x')` were blocked (page saw `onerror` / `failed`); counters 2 blocked of 6 seen |
| Interceptor redirect to a **custom scheme** stub | `/adsbygoogle.js` -> `dvres:abg.js` served by a `QWebEngineUrlSchemeHandler`: the page's `onload` fired, the stub's global was defined, the real file never loaded |
| Notification permission default | `Notification.permission == "default"` (ask): the program's own policy can deny it |
| `QWebEngineExtensionManager` exists | yes, in 6.10.3; `loadExtension` / `installExtension` of a MV3 folder succeed (`loaded = true`); the two built-in ones (`chromium-pdf`, `Google Hangouts`) are listed |

## Does not work / problems
| Check | Result | Consequence |
|---|---|---|
| Redirect to a `data:` URL | the page got `onerror` (blocked) | stubs must be served by a custom scheme (done above), not `data:` |
| **`setExtensionEnabled(...)`** | **crashes the process (0xC0000005 inside `Qt6WebEngineCore.dll`)** - on our extension, on the built-in Hangouts one, before and after a page was loaded | Extensions stay `enabled = false`, so their content script never ran (`data-dv-ext` stayed null). **Extension support is not usable in 6.10.3 as tested.** Not tried: Qt 6.11.3 / 6.12.1 (another large download), a profile created differently (e.g. the Quick `WebEngineProfile`), other extension types |
| H.264 / AAC | `canPlayType` returns empty for `avc1` and `mp4a`; VP9 is "probably" | The Qt binaries have **no proprietary codecs**: many sites' mp4 / H.264 video will not play (YouTube's VP9 / AV1 will). The same limit applies to Master Pro's browser tab. A fix means building Qt WebEngine from source with proprietary codecs (very large) - needs a decision |
| Picture-in-Picture | `document.pictureInPictureEnabled == false` | The own mini-window fallback of plan section 6 is needed |
| Widevine | `requestMediaKeySystemAccess` exists, but no CDM is shipped: protected video will not play | as planned: DRM is out of scope |
| User agent | `Mozilla/5.0 (Windows NT 6.2; Win64; x64) ... QtWebEngine/6.10.3 Chrome/134.0.0.0 Safari/537.36` | "Windows NT 6.2" and `QtWebEngine/..` must be cleaned like Master Pro does (`BrowserBridge::userAgent`) |

## Not tested
- Google sign-in inside a page: needs a human. Run `build\spike.exe google` (after building as in the plan) and try `accounts.google.com`; report what appears. The result decides nothing in the code, only the wording of the UI.
- Real Chrome Web Store extensions, the Chrome Web Store install step, the Google OAuth flow, Drive sync.
- Performance of the interceptor with a real filter list (Phase 2).

## Qt 6.12.1 attempt (2026-11-19)
Not installable with the portable tool: `aqt list-qt` names 6.11.0 - 6.12.1, but the binary repository (`download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_<ver>/qt6_<ver>/Updates.xml`) answers **404 for 6.11.0 - 6.11.3 and 6.12.0 - 6.12.1** (and 200 for 6.10.3). So the crash of `setExtensionEnabled` could not be re-tested on a newer Qt. Nothing was downloaded or installed in this attempt. Ways forward (each needs the owner): the official Qt installer with a Qt account (`aqt install-qt-official`, the owner signs in; the agent never enters the password), building Qt WebEngine 6.12 from source (very large), or waiting for the binaries.
