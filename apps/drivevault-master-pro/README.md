# DriveVault Master Pro

The hub of the DriveVault suite (think "Creative Cloud" for DriveVault programs). It finds the installed DriveVault programs, opens them, shows their version,
installs / updates them from setup files and uninstalls them. Every program has its own accent colour (Video Editor Pro violet, Image Editor blue, Audio Editor
emerald; Master Pro amber). Native Windows program: C++20, Qt 6.8 / Qt Quick, CMake. Languages: ไทย / English.

```
C:\Program Files\DriveVault                                  <- DriveVault Master Pro (this program, DriveVaultMasterPro.exe)
C:\Program Files\DriveVault\Common                           <- every other program lives below this folder (keeps the suite folder tidy)
C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro   <- the Video Editor
C:\Program Files\DriveVault\Common\DriveVault <next program>     <- future programs
```

## It is installed automatically

Every program installer carries `DriveVault-Master-Pro-Setup-x64.exe` inside and runs it silently first when Master Pro is missing or older (same scope - all users
or current user - and language). Master Pro has its own uninstaller; while any DriveVault program is installed it **refuses** to be uninstalled (also silently). When
the last program is gone it can be uninstalled and removes the suite folder if it is empty. The contract with the programs (folders, `product.json`, registry
`Software\DriveVault\Products\<key>` incl. the `Uninstaller` value, silent switches, colours) is documented in
`docs\suite-integration.md` at the repository root.

## What it does (and honestly does not)

* Reads `Software\DriveVault\Products` in HKLM and HKCU (refreshes every 5 s, so installs from elsewhere show up), validates that the program file still exists
  ("Needs repair" when it does not) and also lists programs it does not know yet.
* **Open**, **Folder**, **Uninstall** (starts the program's own uninstaller; projects are never deleted), **Install / Update** from setup files found in
  `<Master>\installers`, `%LOCALAPPDATA%\DriveVault\MasterPro\installers` and folders you add, or a file you choose. Only setup files whose version resource says
  *CompanyName = DriveVault* are started; **signatures are not verified** (the installers are not signed yet). A newer setup file than the installed version
  shows "Update available".
* Image Editor and Audio Editor are shown as **Coming soon** - they do not exist yet. Programs are installed from setup files on this PC. Since 1.1.0 the **Check for updates** button asks the GitHub Releases of `newzydev/drivevault-suite` which versions exist (only when you press it - once when the program opens too, unless switched off) and offers **Download and update / install**: the setup file comes from the official release address only, its SHA-256 is compared with the release's `.sha256` file, its version resource must be DriveVault + the right program + the released version, and then it runs like any other setup file (and is deleted after a successful run). There is no online store and no automatic update.

## Build, run, test

```powershell
.\scripts\build.ps1 -Test          # configure + build Release + tst_hub + tst_window (uses the suite's portable tools in <suite>\.tools)
.\build\release\bin\DriveVaultMasterPro.exe
.\scripts\package.ps1              # build + tests + windeployqt + app-local VC runtime + clean-PC import check + Inno Setup -> dist\DriveVault-Master-Pro-Setup-x64.exe
```

Diagnostics: `--list-products <file.json>` writes what the hub sees; `--screenshot <png> [--size WxH] [--lang th|en]`; `--registry-root <key>` and `--data-dir <dir>` for tests.

Tests: `tst_hub` (version resources, registry scan incl. broken / unknown programs, setup discovery, update detection, running a setup / uninstaller with exit code and
silent switches through a test double, only-DriveVault files are started, persistence, document lookup) and `tst_window` (the real window offscreen against a fake
registry: every program shows its own colour; Thai differs from English). The installer behaviour of Master Pro (install by the program installer, registry, Start Menu,
refusal while a program is installed, uninstall, empty-folder clean-up) is tested by `..\DriveVault Video Editor Pro\scripts\test-installer.ps1` (68 checks).

## Status

VERSION 1.5.1 (online update check since 1.1.0). Not code-signed (the signing scripts are in the Video Editor repository: `scripts\sign.ps1`, `package.ps1 -Sign`). Not tested on a separate clean PC / VM,
and the all-users path under `C:\Program Files` needs administrator rights and was not run by the automated tests (the per-user path was). `LICENSE.txt` and the installer licence pages are the drafted EULA generated from `shared/legal`
(not yet reviewed by a lawyer).
