# DriveVault suite integration (for "DriveVault Master Pro" and future products)

**Online updates (Master Pro 1.1.0+).** Every program release is a GitHub Release of `newzydev/drivevault-suite` with the tag `<program folder name below apps/>-v<x.y.z>` (`drivevault-video-editor-pro-v1.0.1`, `drivevault-master-pro-v1.1.0`, later `drivevault-image-editor-pro-...`, `drivevault-audio-editor-pro-...`) and exactly these assets:
the setup file `DriveVault-<Name>-Setup-x64.exe` and its `DriveVault-<Name>-Setup-x64.exe.sha256` (`<hex> *<file name>`, written by `package.ps1`). The release is not a draft or pre-release; the setup's version resource
(`CompanyName` DriveVault, `ProductName` = the program, `ProductVersion` = the tag's version) must match. Master Pro reads it when the user presses "Check for updates" and once at start-up (setting, on by default). Master Pro's own release (`drivevault-master-pro-v...`) is offered through a banner and updates Master Pro itself.

**DriveVault Master Pro** is the hub for Video Editor Pro and PDF Pro (image and audio editors are planned):
it lists, opens, installs, updates and uninstalls the programs, in the spirit of Creative Cloud. **Every product installer installs Master Pro first** (it checks GitHub for the newest hub release,
downloads it and runs it silently when the hub is missing or older). This page is the contract that every product - and the hub - follows.

## Folder layout

| What | Where |
|---|---|
| Suite folder = DriveVault Master Pro | `C:\Program Files\DriveVault` (`DriveVaultMasterPro.exe`, registry `Software\DriveVault\MasterPro`) |
| This product | `C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro` |
| PDF Pro | `C:\Program Files\DriveVault\Common\DriveVault PDF Pro` |
| Next products | `C:\Program Files\DriveVault\Common\DriveVault <Product Name>` (the product's full name, with spaces) |
| Start Menu | one folder **DriveVault** with one shortcut per product |
| Per-user data | `%LOCALAPPDATA%\DriveVault\<ProductId>` (Video Editor: `VideoEditorPro`) |
| Per-user install (no administrator) | `%LOCALAPPDATA%\Programs\DriveVault\Common\DriveVault <Product Name>` |

The uninstaller of a product removes only its own folder, shortcuts and registry keys. The shared `DriveVault` folder disappears only when it is empty.

## Product manifest

Every product installs `product.json` in its folder:

```json
{
  "manifestVersion": 1,
  "suite": "DriveVault",
  "id": "drivevault.videoeditor",
  "name": "DriveVault Video Editor Pro",
  "version": "1.0.0",
  "publisher": "DriveVault",
  "executable": "DriveVaultVideoEditorPro.exe",
  "accentColor": "#8B5CF6",
  "dataFolder": "VideoEditorPro"
}
```

## Registry (HKLM for all-users installs, HKCU for per-user installs)

`Software\DriveVault\Products\<ProductKey>` (Video Editor: `VideoEditorPro`) with string values `DisplayName`, `Version`, `InstallDir`, `Executable`,
`Manifest` (full path of product.json), `AccentColor`, `Publisher`. The standard Windows uninstall key (`...\Uninstall\{AppId}_is1`) stays the source
for `UninstallString`. The hub enumerates `Software\DriveVault\Products` in both hives.

## Silent install / uninstall (what the hub runs)

```
DriveVault-Video-Editor-Pro-Setup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /ALLUSERS  /LANG=thai|english [/DIR="..."] [/LOG="..."]
DriveVault-Video-Editor-Pro-Setup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER ...        (per-user, no administrator)
"<InstallDir>\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART                                     (keeps settings, never deletes projects)
```

### Updating (what the hub runs for an update, and what every setup does)

The hub starts a setup that replaces an installed version **with its wizard (not silent)**, so the user sees the installer at once:

```
<Setup>.exe /NORESTART /CLOSEAPPLICATIONS /LANG=thai|english /UPDATE [/ALLUSERS|/CURRENTUSER]
```

An update is recognised by `/UPDATE` **or** by an existing `Software\Microsoft\Windows\CurrentVersion\Uninstall\{AppId}_is1` key (so a double click on a newer setup behaves the same). Every setup then:
1. closes the running program by itself (`CloseApplications=force`: Windows Restart Manager, no question - a program with unsaved work asks the user itself when it is closed);
2. skips the questions: welcome, licence (accepted at the first install), components, tasks (as chosen before), ready page - the installing page appears at once, no model download;
3. ends on the Finish page where "Launch ..." is ticked, so pressing Finish opens the program again;
4. (Video Editor Pro, PDF Pro) when `/UPDATE` was given and this setup also replaced Master Pro (which closes the running hub), opens Master Pro again after Finish.

Silent switches (`/VERYSILENT`, tests, the other products' nested hub setup) are unchanged and never relaunch anything. Not tested by an agent: the real wizard flow (the installer is built, not run; `test-installer.ps1` is the owner's).

Exit code 0 = success. Installing a newer version over an old one upgrades in place (same `AppId`); `Version` in the registry tells the hub which version is installed.
`AppId` of Video Editor Pro: `{6E1B7A52-4C3D-4F7E-9A3B-D17E5A0C2F11}` - never change it.

PDF Pro uses the same switches and the setup filename `DriveVault-PDF-Pro-Setup-x64.exe`. Its permanent AppId is `{9835145C-1C5A-48CB-A1CE-3F29EC3037BA}`. The registry key and data folder are `PdfPro`, the manifest ID is `drivevault.pdf`, and the release slug is `drivevault-pdf-pro`. Its `OpenWithProgids` registration offers PDF Pro for `.pdf` while leaving the default app under Windows user control.

## Brand colours (one accent colour per product)

| Product | Accent | Status |
|---|---|---|
| Video Editor Pro | **#8B5CF6** (violet) | in use |
| PDF Pro | **#EF4444** (red) | in use |
| Image editor | #3B82F6 (blue) | proposal |
| Audio editor | #10B981 (emerald) | proposal |
| Master Pro (hub) | #F59E0B (amber) | proposal |

The accent colour appears in the product icon, the installer artwork (`scripts\make-brand-assets.ps1 -Accent "#RRGGBB"`), `product.json` and the application theme
(`accentColor` setting default). Neutral surfaces, typography (Inter + Noto Sans Thai) and spacing are shared by all products.

## What every new product installer must do for the hub

1. Install into `<suite folder>\Common\DriveVault <Product Name>` and write `Software\DriveVault\Products\<ProductKey>` with `DisplayName, Version, InstallDir, Executable, Manifest, AccentColor, Publisher` **and `Uninstaller`** (`{uninstallexe}`).
2. Check GitHub for the newest Master Pro and run it silently first when `Software\DriveVault\MasterPro` is missing or older (Master Pro is **not** inside the program installers): the releases API `https://api.github.com/repos/newzydev/drivevault-suite/releases?per_page=100`, the highest `drivevault-master-pro-v<x.y.z>` that is not a draft / pre-release and has both `DriveVault-Master-Pro-Setup-x64.exe` and `.exe.sha256`; download the `.sha256`, then the setup from `https://github.com/newzydev/drivevault-suite/releases/download/<tag>/` with its SHA-256 checked (Inno `DownloadTemporaryFile`; 4 tries with growing pauses, 2 when a Master Pro is installed already); run it with `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /ALLUSERS|/CURRENTUSER /DIR="<suite folder>" /LANG=<language>`. All best effort: offline or a failure installs the program anyway and says so on the last page; an installed Master Pro is kept when GitHub is not reachable; `/NOMASTER` skips the step (tests, offline PCs). The code is shared: `shared\installer\master-hub-code.iss` + `master-hub-messages.iss`, included by `drivevault.iss` and `pdf.iss` (the function `PrepareToInstall`).
3. Give the setup exe a version resource with `CompanyName = DriveVault` and `ProductName` equal to the product's name: the hub only offers / starts such files.
4. Never delete the suite folder; remove only your own folder, shortcuts and keys. The hub removes the suite folder itself when it is the last one and the folder is empty.

Master Pro knows the planned products (Image Editor Pro `ImageEditorPro` #3B82F6, Audio Editor Pro `AudioEditorPro` #10B981) as "Coming soon"; their registry keys must use these names to be recognised, other keys are listed generically.

## Google account (shared by every program, since Master Pro 1.39.0 / Video Editor Pro 1.17.0 / PDF Pro 1.23.0)

Signing in with Google is **optional** and one account serves the whole suite (code: `shared/auth`, rules: `shared/auth/CLAUDE.md`).
- **File**: `%LOCALAPPDATA%\DriveVault\Account\account.bin`, one per Windows user, encrypted with Windows DPAPI (this user only, entropy `DriveVault.Account.v1`). JSON inside: `sub` (Google id), `name`, `email`, `avatar` (Base64 PNG, 128 px, may be missing). **No token is stored.** Uninstallers do not remove it.
- **Rule for every program**: show it in the title bar (`DvAccountChip`), watch the folder (the library does it), never write the file by another route than `GoogleAccount::signIn / signOut`, never keep a copy elsewhere, and never lock a feature behind it without a new owner decision.
- **Sync**: a program that signs in or out changes the file; every other program, running or started later, reloads it (folder watcher + a look every 2 s).
- **Tests** set `DV_ACCOUNT_DIR` (plain `account.json` there) so they never touch the real account.
- A new program adds `shared/auth` as in `shared/auth/CLAUDE.md`; the Google OAuth client is built in from a file outside the repository (`shared/scripts/make-oauth-header.ps1`).

### Per-account data
Programs keep their history per signed-in account (Master Pro: AI chats, PDF Pro: recent files, Video Editor Pro: recent projects) below `accounts/<key>` of their own data folder, `<key>` = `GoogleAccount::scopeKey()` (24 hex digits of the SHA-256 of the Google id). Nobody signed in = the shared data as before. The first use of an account copies the shared data. Details and rules: `shared/auth/CLAUDE.md`.
