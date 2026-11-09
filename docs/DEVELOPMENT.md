# DriveVault suite - developer guide

For people who build the programs. Users: see the [README](../README.md).

| Folder | What |
|---|---|
| [`apps/drivevault-video-editor-pro`](../apps/drivevault-video-editor-pro) | **DriveVault Video Editor Pro** (C++20, Qt 6 / QML, FFmpeg). Accent colour violet `#8B5CF6`. |
| [`apps/drivevault-master-pro`](../apps/drivevault-master-pro) | **DriveVault Master Pro** - the hub that lists, opens, installs, updates (also from GitHub Releases) and uninstalls the programs. Accent colour amber `#F59E0B`. |
| [`shared/scripts`](../shared/scripts) | Build environment (`env.ps1`), signing, clean-machine dependency check, installer artwork - used by every app. |
| [`shared/legal`](../shared/legal) | The EULA templates (English + Thai); `make-license.ps1` generates every app's licence texts. |
| [`docs`](.) | [`suite-integration.md`](suite-integration.md) (the contract between the programs and the hub). |
| `.tools` (not in git) | Portable toolchain: CMake, Ninja, Qt 6.8.3, Inno Setup, FFmpeg, whisper.cpp + speech model, Python. |

Installed layout (all users; "just for me" uses `%LOCALAPPDATA%\Programs\DriveVault` instead):

```
C:\Program Files\DriveVault                                        DriveVault Master Pro
C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro     Video Editor Pro (every program lives below Common)
```

Every program installer carries the Master Pro setup and installs it first, so the hub is always there.

## First-time setup on a new machine

1. Windows 10/11 x64, **Visual Studio 2022 or Build Tools** with the *Desktop development with C++* workload, and PowerShell 5.1+.
2. Create `.tools\` at the repository root with: CMake >= 3.28 (`.tools\cmake`), Ninja (`.tools\ninja`), Qt 6.8.3 `msvc2022_64`
   (`.tools\Qt\6.8.3\msvc2022_64`, e.g. `pip install aqtinstall` then `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O .tools\Qt`),
   Inno Setup 6 (`.tools\InnoSetup`), an **LGPL shared** FFmpeg build with headers and import libraries (`.tools\ffmpeg`, e.g. BtbN `ffmpeg-n8.1-latest-win64-lgpl-shared`),
   and optionally whisper.cpp (`.tools\whisper\bin\Release\whisper-cli.exe`, models in `.tools\whisper\models`). `scripts\env.ps1` also accepts tools on `PATH`
   and `DV_TOOLS_DIR` / `DV_QT_DIR` / `DV_FFMPEG_DIR` overrides.
   PDF Pro (in development) also needs **PDFium** (`.tools\pdfium` with `include`, `lib\pdfium.dll.lib`, `bin\pdfium.dll`; the win-x64 build of bblanchon/pdfium-binaries, BSD-3;
   `DV_PDFIUM_DIR` override) and, for its password tests, **qpdf** (`.tools\qpdf`, the msvc64 release zip, Apache-2.0).

## Build, test, package

```powershell
.\scripts\build-all.ps1            # Master Pro + Video Editor Pro: build, all tests, both installers, installer test
.\scripts\build-all.ps1 -Sign      # the same, signed (needs DV_SIGN_PFX or DV_SIGN_THUMBPRINT)
```

Order matters: the Video Editor and PDF Pro installers contain the Master Pro installer, so Master Pro is packaged first. Each app has its own scripts
(`apps\<app>\scripts\build.ps1 -Test`, `package.ps1`, ...) and README.

Outputs: `apps\drivevault-master-pro\dist\DriveVault-Master-Pro-Setup-x64.exe`, `apps\drivevault-video-editor-pro\dist\DriveVault-Video-Editor-Pro-Setup-x64.exe` (not in git).

## Status

Both programs are covered by automated tests (Video Editor 14 suites, Master Pro 2, 68 installer checks). Not code-signed yet (Windows SmartScreen shows a warning: "More info" -> "Run anyway"; signing needs a purchased certificate and is a later step), not tested on a separate clean PC / VM.
The licence agreement (EULA, English + Thai, in `shared/legal`, governed by Thai law) is drafted but has not been reviewed by a lawyer. See each app's README for details.

## Releases

Rules for versions, commit messages and GitHub Releases are in the repository's `CLAUDE.md` / `AGENTS.md` ("Git and versions"). The download table of the README is part of every release: update it in the commit that bumps a version.
