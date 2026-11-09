# DriveVault Master Pro - rules (adds to the repository root file)

The hub of the suite: shows the installed DriveVault programs in their own accent colours, opens them, and installs / updates / uninstalls them from **setup files that exist on this PC**. Qt 6.8 / Qt Quick, C++20.
Every program installer installs this hub first (silently), so it is always present.

## Structure
```
src/        Hub (registry scan, installers, tasks, online update), Releases (GitHub release list parser, SHA-256 file parser), WinUtil (version resources, ShellExecute, version compare), main.cpp, app.rc
qml/        Main, ProductCard, AboutDialog, MButton, Theme (singleton), Tr (singleton: Thai / English table)
tests/      tst_hub, tst_window, fake_setup (test double for a setup / uninstall program)
installer/  master.iss + wizard pictures      scripts/  build, package, make-icon      docs/  notices, LGPL text
```

## Rules
- **Honest scope**: online access is limited to (1) the GitHub Releases list of `newzydev/drivevault-suite`, read when the user presses "Check for updates" and **once when the program opens** while the setting "Check for updates on start" is on (default on, `settings.ini` key `autoCheckUpdates`, a checkable item of the "..." menu in the top bar; the start check is quiet: it says nothing when it fails or finds nothing; it is off for `--screenshot` / `--list-products`), and (2) downloading one setup file from the official release address **only after the user presses an update button**. No automatic downloads, no repeated background polling, no online store. A download that still fails after the retries (HTTP 502/503/504, timeout) opens the release page (`html_url`, only below `Options::pagePrefix`) in the browser so the user can download by hand (`Hub::failOnline`, signal `releasePageOpened`); tests set `openPageOnFailure = false`. Master Pro can update itself (banner "Master Pro x.y.z is available" -> `Hub::updateSelf`: verified like any download, then run with `/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`). Image Editor Pro and Audio Editor Pro are listed as "Coming soon" because they do not exist. Do not add UI that implies otherwise.
- **Online update safety** (`Hub::downloadAndInstall`, `Releases.cpp`): download only from `Options::downloadPrefix` (official release URL); the `.exe.sha256` asset is mandatory and the SHA-256 must match; then CompanyName = DriveVault, ProductName = the catalog program and ProductVersion = the release version, otherwise the file is deleted and nothing runs. Release tags are `<apps folder name>-v<x.y.z>` (`drivevault-video-editor-pro`, `drivevault-image-editor-pro`, `drivevault-audio-editor-pro`; map in `slugForKey`); drafts / pre-releases are ignored. A new program that should get online updates needs its slug there. Tests use local files through `Options::releasesUrl` / `downloadPrefix`; the real exe never takes them from the command line. Signatures are still **not** verified - never call the SHA-256 check "signature verification"; it only catches a damaged or swapped download.
- **Only DriveVault setup files are started** (version resource `CompanyName = DriveVault`). Signatures are **not** verified - never describe that as verification. Master Pro's own setup is offered only through its own banner (`selfUpdateVersion`), never as a program card.
- Programs are found through `Software\DriveVault\Products\<Key>` in HKLM and HKCU (contract: `docs/suite-integration.md`). Catalog keys: `VideoEditorPro`, `ImageEditorPro`, `AudioEditorPro`; unknown keys are listed generically.
  A registry entry whose program file is gone is "broken" (Needs repair), never "installed".
- Long work (running a setup / uninstaller) runs on a worker thread, one task at a time (`Hub::busy`); the UI must never block. Exit codes are shown, not hidden. Network work (`QNetworkAccessManager`, async, 30 s inactivity timeout) runs on the main thread and sets `Hub::checking` or `busy` while it works.
- Texts: every user-visible string is in `qml/Tr.qml` (Thai + English) or `Hub::tr2(en, th)`. Add both languages together.
- UI: neutral dark surfaces + one accent per program (`product.accent`); Inter + Noto Sans Thai. Controls have `objectName`s that the tests use (`launch_<key>`, `install_<key>`, ...).
- QML gotchas: ids of one file are not visible in another file (use signals); QVariantMap values that are empty strings must be `toString()`-ed or QML sees `undefined`.

## Commands
```powershell
.\scripts\build.ps1 -Test        # build + tst_hub + tst_window
.\scripts\package.ps1            # + windeployqt, app-local VC runtime, dependency check, license-file checks, Inno Setup -> dist\DriveVault-Master-Pro-Setup-x64.exe
```
Diagnostics of the exe: `--list-products <file.json>`, `--screenshot <png> [--size WxH] [--lang th|en]`, `--registry-root <key>`, `--data-dir <dir>`.

## Installer behaviour that must stay true (tested by the Video Editor `test-installer.ps1`, which only the owner runs)
Installs to `C:\Program Files\DriveVault` (admin) or per-user; creates `installers` and `Common`; registers `Software\DriveVault\MasterPro`; **refuses to uninstall while any program is installed** (also silently);
after uninstalling it removes the empty suite folder (delayed `rmdir`, only empty folders). Version resource: ProductName `DriveVault Master Pro`, CompanyName `DriveVault`.
