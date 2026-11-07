# DriveVault suite - instructions for AI coding agents

This file is read by AI coding agents (`CLAUDE.md` by Claude Code, `AGENTS.md` by Codex). **The two files are identical in every folder** and are
checked by `scripts\check-agent-files.ps1` - edit one, then copy it over the other (`Copy-Item CLAUDE.md AGENTS.md`). Each sub folder that has these files adds rules for
that folder; the rules of the parent folders always apply too. Read the nearest file before changing code in a folder.

## What this repository is

A suite of native Windows desktop programs (C++20, Qt 6.8 / Qt Quick, CMake; no Electron / Tauri / WebView):

| Folder | Program |
|---|---|
| `apps/drivevault-video-editor-pro` | DriveVault Video Editor Pro 1.0.0 (FFmpeg, whisper.cpp for offline captions). Accent colour violet `#8B5CF6`. |
| `apps/drivevault-master-pro` | DriveVault Master Pro 1.0.0 - the hub that lists / opens / installs / updates / uninstalls the programs. Accent colour amber `#F59E0B`. |
| `shared/scripts` | Build environment, signing, clean-machine check, installer artwork used by every app. |
| `scripts` | Suite-level scripts: `build-all.ps1` (everything), `check-agent-files.ps1` (CLAUDE.md = AGENTS.md). |
| `docs` | `suite-integration.md` (contract between programs and hub), `spec/` (the owner's master specification - read-only). |
| `.tools` (not in git) | Portable toolchain: CMake, Ninja, Qt 6.8.3 msvc2022_64, Inno Setup, FFmpeg (LGPL shared), whisper.cpp + models, Python. |

Planned (do not pretend they exist): Image Editor Pro, Audio Editor Pro. New programs go in `apps/drivevault-<name>` (lower case, words joined with `-`).

## How to work here

- **Language**: the owner writes Thai and wants answers in Thai (code, identifiers, commit-style text and these instruction files stay English). User-visible app text is Thai + English.
- **Be honest**: never mock a feature and call it done. Report what was built, what was tested (and how), what was not tested, and known limits. If a test fails, say so with the output.
  Status labels in the UI must match reality (Available / Model required / Coming soon / Not implemented).
- **Git**: do not run `git init`, `git commit`, `git push` or change branches unless the owner asks - the owner creates the repository and commits in VS Code.
- **Downloads and installs** (tools, models, packages) need the owner's explicit permission first; say what, from where and how big. Nothing in the apps downloads anything unless the user presses a button.
- **Destructive actions** (deleting folders, uninstalling, overwriting) only when asked or clearly part of the task; look at the target first. Never touch the owner's projects / exports / user data.
- **Scope**: do what was asked; keep changes small and consistent with the surrounding code (naming, comment density, idiom). Do not add speculative features.
- **Verify before you say "done"**: build and run the relevant tests; for installer / packaging changes run `scripts\build-all.ps1` (or at least the app's `package.ps1` and `test-installer.ps1`).

## Commands (PowerShell 5.1, from the repository root)

```powershell
.\scripts\build-all.ps1                       # Master Pro, then Video Editor: build + tests + installers + installer test (66 checks)
.\scripts\build-all.ps1 -SkipBuild            # package existing release builds
.\scripts\build-all.ps1 -Sign                 # signed release (needs DV_SIGN_PFX / DV_SIGN_THUMBPRINT)
.\scripts\check-agent-files.ps1               # CLAUDE.md and AGENTS.md identical everywhere?
.\apps\<app>\scripts\build.ps1 -Test          # one app: configure + build Release + ctest
```

Order matters: the Video Editor installer carries the Master Pro installer, so Master Pro is packaged first.

## Platform quirks that cost time before

- Windows only. The repository path may contain spaces on other machines; scripts must quote paths. Use `cmd /c "... > file"` to capture native program output in PowerShell; Qt Test needs `-o file,txt`
  for results when output is redirected. Qt programs run from a shell need `. shared\scripts\env.ps1` (Qt on PATH).
- Bash / perl one-liners mangle backslashes and quotes: write scripts and regex patches with the editor tools (or `.pl` / `.ps1` files), not inline shell strings.
- PowerShell variable names are case-insensitive (`$Dir` and `$dir` are the same variable): `env.ps1` loops over `$dir`; give your parameters other names.
- `Get-ChildItem`-style globbing in scripts must exclude `build/`, `dist/`, `.tools/`.

## Contracts that must not drift (details in `docs/suite-integration.md`)

- Install layout: Master Pro in `C:\Program Files\DriveVault`, every program in `C:\Program Files\DriveVault\Common\DriveVault <Name>` (per-user: `%LOCALAPPDATA%\Programs\DriveVault\...`).
- Registry `Software\DriveVault\Products\<Key>` (DisplayName, Version, InstallDir, Executable, Manifest, AccentColor, Publisher, Uninstaller) + `product.json` in each program folder;
  `Software\DriveVault\MasterPro` for the hub. Every program installer carries and silently runs the Master Pro setup first when it is missing / older.
- Inno Setup `AppId` values never change once released: Video Editor `{6E1B7A52-4C3D-4F7E-9A3B-D17E5A0C2F11}`, Master Pro `{B3A7C1D4-5E2F-4A86-9C10-7D2E8F4A6B35}`.
- Setup exes carry a version resource with `CompanyName = DriveVault` and `ProductName = <program name>`.
- Release gates (run by each `package.ps1`): clean-machine import check, license audit (FFmpeg LGPL-only, no GPL-only Qt, every file has an owner), notices present. Do not bypass them.

## Licence texts

The EULA (English + Thai) lives in `shared/legal/EULA.en.txt` and `EULA.th.txt`; `shared/scripts/make-license.ps1` generates each app's `LICENSE.txt` and the installer pages (`installer/windows/license-en.txt`, `license-th.txt`, UTF-8 with BOM).
Edit the templates (or the per-product section 6 in the script), run the script, rebuild the installers. Never hand-edit the generated files. Governing law is Thailand; the text is drafted, not lawyer-reviewed.

## Open items the owner knows about

Code signing certificate (installers are unsigned), a test on a clean PC / VM (incl. the elevated `C:\Program Files` install), a lawyer's review of the EULA (a drafted text, not yet reviewed),
bundling the GNU GPL v3 text referenced by the LGPL, an online catalog / auto-update for Master Pro, GPU compositing for hardware decoding.
