# DriveVault suite integration (for "DriveVault Master Pro" and future products)

**Online updates (Master Pro 1.1.0+).** Every program release is a GitHub Release of `newzydev/drivevault-suite` with the tag `<program folder name below apps/>-v<x.y.z>` (`drivevault-video-editor-pro-v1.0.1`, `drivevault-master-pro-v1.1.0`, later `drivevault-image-editor-pro-...`, `drivevault-audio-editor-pro-...`) and exactly these assets:
the setup file `DriveVault-<Name>-Setup-x64.exe` and its `DriveVault-<Name>-Setup-x64.exe.sha256` (`<hex> *<file name>`, written by `package.ps1`). The release is not a draft or pre-release; the setup's version resource
(`CompanyName` DriveVault, `ProductName` = the program, `ProductVersion` = the tag's version) must match. Master Pro reads it when the user presses "Check for updates" and once at start-up (setting, on by default). Master Pro's own release (`drivevault-master-pro-v...`) is offered through a banner and updates Master Pro itself.

DriveVault Video Editor Pro is the first product of the suite (image editor, audio editor ... are planned). **DriveVault Master Pro** (repository `DriveVault Master Pro`, version 1.3.2) is the hub:
it lists, opens, installs, updates and uninstalls the programs, in the spirit of Creative Cloud. **Every product installer installs Master Pro first** (it carries
the hub's setup inside and runs it silently when the hub is missing or older). This page is the contract that every product - and the hub - follows.

## Folder layout

| What | Where |
|---|---|
| Suite folder = DriveVault Master Pro | `C:\Program Files\DriveVault` (`DriveVaultMasterPro.exe`, registry `Software\DriveVault\MasterPro`) |
| This product | `C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro` |
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

Exit code 0 = success. Installing a newer version over an old one upgrades in place (same `AppId`); `Version` in the registry tells the hub which version is installed.
`AppId` of Video Editor Pro: `{6E1B7A52-4C3D-4F7E-9A3B-D17E5A0C2F11}` - never change it.

## Brand colours (one accent colour per product)

| Product | Accent | Status |
|---|---|---|
| Video Editor Pro | **#8B5CF6** (violet) | in use |
| Image editor | #3B82F6 (blue) | proposal |
| Audio editor | #10B981 (emerald) | proposal |
| Master Pro (hub) | #F59E0B (amber) | proposal |

The accent colour appears in the product icon, the installer artwork (`scripts\make-brand-assets.ps1 -Accent "#RRGGBB"`), `product.json` and the application theme
(`accentColor` setting default). Neutral surfaces, typography (Inter + Noto Sans Thai) and spacing are shared by all products.

## What every new product installer must do for the hub

1. Install into `<suite folder>\DriveVault <Product Name>` and write `Software\DriveVault\Products\<ProductKey>` with `DisplayName, Version, InstallDir, Executable, Manifest, AccentColor, Publisher` **and `Uninstaller`** (`{uninstallexe}`).
2. Carry `DriveVault-Master-Pro-Setup-x64.exe` and run it silently first when `Software\DriveVault\MasterPro` is missing or older: `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /ALLUSERS|/CURRENTUSER /DIR="<suite folder>" /LANG=<language>`
   (see `installer\windows\drivevault.iss`, sections `[Files]`, `[Run]` and the `NeedMaster` code).
3. Give the setup exe a version resource with `CompanyName = DriveVault` and `ProductName` equal to the product's name: the hub only offers / starts such files.
4. Never delete the suite folder; remove only your own folder, shortcuts and keys. The hub removes the suite folder itself when it is the last one and the folder is empty.

Master Pro knows the planned products (Image Editor Pro `ImageEditorPro` #3B82F6, Audio Editor Pro `AudioEditorPro` #10B981) as "Coming soon"; their registry keys must use these names to be recognised, other keys are listed generically.
