# DriveVault Master Pro - rules (adds to the repository root file)

The hub of the suite: shows the installed DriveVault programs in their own accent colours, opens them, and installs / updates / uninstalls them from **setup files that exist on this PC**. Qt 6.8 / Qt Quick, C++20.
Every program installer installs this hub first (silently), so it is always present.

## Structure
```
src/        Hub (registry scan, installers, tasks), WinUtil (version resources, ShellExecute, version compare), main.cpp, app.rc
qml/        Main, ProductCard, AboutDialog, MButton, Theme (singleton), Tr (singleton: Thai / English table)
tests/      tst_hub, tst_window, fake_setup (test double for a setup / uninstall program)
installer/  master.iss + wizard pictures      scripts/  build, package, make-icon      docs/  notices, LGPL text
```

## Rules
- **Honest scope**: no online catalog, no downloads, no auto-update exist. Image Editor Pro and Audio Editor Pro are listed as "Coming soon" because they do not exist. Do not add UI that implies otherwise.
- **Only DriveVault setup files are started** (version resource `CompanyName = DriveVault`). Signatures are **not** verified - never describe that as verification. Master Pro's own setup is never offered as an update.
- Programs are found through `Software\DriveVault\Products\<Key>` in HKLM and HKCU (contract: `docs/suite-integration.md`). Catalog keys: `VideoEditorPro`, `ImageEditorPro`, `AudioEditorPro`; unknown keys are listed generically.
  A registry entry whose program file is gone is "broken" (Needs repair), never "installed".
- Long work (running a setup / uninstaller) runs on a worker thread, one task at a time (`Hub::busy`); the UI must never block. Exit codes are shown, not hidden.
- Texts: every user-visible string is in `qml/Tr.qml` (Thai + English) or `Hub::tr2(en, th)`. Add both languages together.
- UI: neutral dark surfaces + one accent per program (`product.accent`); Inter + Noto Sans Thai. Controls have `objectName`s that the tests use (`launch_<key>`, `install_<key>`, ...).
- QML gotchas: ids of one file are not visible in another file (use signals); QVariantMap values that are empty strings must be `toString()`-ed or QML sees `undefined`.

## Commands
```powershell
.\scripts\build.ps1 -Test        # build + tst_hub + tst_window
.\scripts\package.ps1            # + windeployqt, app-local VC runtime, dependency check, license-file checks, Inno Setup -> dist\DriveVault-Master-Pro-Setup-x64.exe
```
Diagnostics of the exe: `--list-products <file.json>`, `--screenshot <png> [--size WxH] [--lang th|en]`, `--registry-root <key>`, `--data-dir <dir>`.

## Installer behaviour that must stay true (tested by the Video Editor `test-installer.ps1`)
Installs to `C:\Program Files\DriveVault` (admin) or per-user; creates `installers` and `Common`; registers `Software\DriveVault\MasterPro`; **refuses to uninstall while any program is installed** (also silently);
after uninstalling it removes the empty suite folder (delayed `rmdir`, only empty folders). Version resource: ProductName `DriveVault Master Pro`, CompanyName `DriveVault`.
