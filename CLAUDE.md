# DriveVault suite - instructions for AI coding agents

This file is read by AI coding agents (`CLAUDE.md` by Claude Code, `AGENTS.md` by Codex). **The two files are identical in every folder** and are
checked by `scripts\check-agent-files.ps1` - edit one, then copy it over the other (`Copy-Item CLAUDE.md AGENTS.md`). Each sub folder that has these files adds rules for
that folder; the rules of the parent folders always apply too. Read the nearest file before changing code in a folder.

## What this repository is

A suite of native Windows desktop programs (C++20, Qt 6.8 / Qt Quick, CMake; no Electron / Tauri / WebView):

| Folder | Program |
|---|---|
| `apps/drivevault-video-editor-pro` | DriveVault Video Editor Pro 1.1.0 (FFmpeg, whisper.cpp for offline captions). Accent colour violet `#8B5CF6`. |
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
- **Git**: when a task is finished and verified, commit and push to `main` automatically, without waiting to be asked (see "Git and versions"). Still never `git init`, change branches, force push or `reset --hard` without asking.
- **Downloads and installs** (tools, models, packages) need the owner's explicit permission first; say what, from where and how big. Nothing in the apps downloads anything unless the user presses a button.
- **Destructive actions** (deleting folders, uninstalling, overwriting) only when asked or clearly part of the task; look at the target first. Never touch the owner's projects / exports / user data.
- **Scope**: do what was asked; keep changes small and consistent with the surrounding code (naming, comment density, idiom). Do not add speculative features.
- **Verify before you say "done" - only what the change needs** (tests are slow and their output costs context):
  1. Docs / agent files / comments only: no build. Run `check-agent-files.ps1` if a CLAUDE.md changed.
  2. Code or QML of **one** app: that app's `scripts\build.ps1 -Test` only (never the other app, never packaging). A smoke start (exe `--screenshot` exits 0 and writes a picture) is enough proof that the program opens; do not drive long UI / export scenarios by hand.
  3. Installer, packaging, `shared/scripts`, version resource or licence changes, and before a release the owner announces: `scripts\build-all.ps1` (or the app's `package.ps1` + `test-installer.ps1`). **Not** before every commit.
  4. Redirect long output to a file (`cmd /c "... > %TEMP%\x.log 2>&1"`) and read only the tail or the failing lines. Run slow steps in the background.
  5. **Silent**: tests and smoke starts must never play sound (`DV_NO_AUDIO=1`, already set by CMake for every test; set it yourself when starting an exe or test binary by hand).

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

## Git and versions

**Branches.** Single branch `main` for now (the first commit `302e890` is on it); the remote is `origin` (GitHub, private).

**Automatic commit and push.** When the work is finished and verified (the checks that "Verify before you say done" asks for this kind of change - not the full `build-all` every time; `scripts\check-agent-files.ps1` passes), the agent commits and pushes to `origin main` on its own, without waiting for the owner:
- Stage only the files of this task (`git status` first; never `git add -A` blindly) - never `build/`, `dist/`, `.tools/`, certificates, keys or the owner's data. If something unexpected shows up as changed, leave it out and say so.
- Bump the version and add the release-notes line first when the change needs it (rules below), then one commit per finished task with the message format below; end the message with the attribution line the session gives.
- Never `--no-verify`, force push, `reset --hard`, `git init` or change branches without asking. If the push is rejected (remote has new commits), `git pull --rebase`, re-check, push again; stop and ask if there are conflicts.
- Do not commit half-done work, failing tests or a broken build. If something fails, report it instead.
- Tell the owner in the summary: commit hash, message, and the new version if bumped.

**Commit message** (English, first line at most about 72 characters): `<type>(<scope>): v<new version> <short summary>`
- `type`: `feat` = new feature (minor), `fix` = bug fix / small change (patch), `feat!` or `fix!` = breaking change (major), and the following need no version number: `docs`, `chore`, `build`, `test`, `refactor`.
- `scope`: `video-editor`, `master-pro`, `suite` (shared scripts, docs, agent files - spans several programs), `shared`. A change that touches one program's behaviour uses that program's scope and version.
- Examples: `fix(video-editor): v1.0.1 export dialog kept the old preset`, `feat(master-pro): v1.1.0 update check from a local folder`, `docs(suite): explain commit and version rules`.
- The body (optional) says why, and lists what was tested. The first commit `feat: initial DriveVault suite ...` predates this rule.

**Versions** (SemVer `MAJOR.MINOR.PATCH`, one number **per program**; the suite itself has no version):
| Change | Bump | Example |
|---|---|---|
| bug fix, wording, small visual tweak | patch | 1.0.0 -> 1.0.1 |
| new feature, new setting, new file format support | minor | 1.0.1 -> 1.1.0 |
| not compatible with what users have (project file or settings cannot be read any more, install layout / registry contract changes) | major | 1.1.0 -> 2.0.0 |
- Any commit that changes what the program does bumps that program's version **in the same commit**. Docs / agent files / scripts / comments / tests only: no bump.
- There is no bump script yet, so update **every** place by hand and check with a search for the old number (exclude `build/`, `dist/`, `.tools/`, `docs/spec/`): `CMakeLists.txt` (`project(... VERSION ...)`, the single source for the exe / version resource where used), `src/**/app.rc`, `installer/windows/*.iss` (the `#define AppVersion` default - the Video Editor one is only a fallback, `package.ps1` passes the real number; Master Pro's is the real one), `README.md`, user guides, `docs/release-notes.txt`, and the tests / `scripts/test-installer.ps1` that assert the number. Keep the Inno `AppId` unchanged.
- The Video Editor installer carries Master Pro: when Master Pro's version changes, the installers must be rebuilt with `scripts\build-all.ps1` before the owner publishes a release, so both agree (`NeedMaster` installs the newer one) - not on every commit.
- Add an entry at the **top** of the program's `docs/release-notes.txt` (`vX.Y.Z (YYYY-MM-DD): ...`) in the same commit; Keep the unsigned-installer note while installers are unsigned.
- **Publishing a release** (what Master Pro's "Check for updates" reads; contract in `docs/suite-integration.md`). The owner does it on GitHub (Releases > Draft a new release; the `gh` CLI is not installed), after the version commit is pushed and `package.ps1` / `scripts\build-all.ps1` passed:
  1. Tag `<slug>-v<x.y.z>` on `main`, one per program release: `drivevault-video-editor-pro-v1.0.1`, `drivevault-master-pro-v1.1.0` (the program folder name below `apps/` + `-v` + version) (not `v1.0.1` alone, because two programs share the repository). Title = program name + version; description = the new `release-notes.txt` entry.
  2. Attach **both** files from the program's `dist\`: `DriveVault-<Name>-Setup-x64.exe` and `DriveVault-<Name>-Setup-x64.exe.sha256`, unchanged. Without the `.sha256` asset Master Pro ignores the release.
  3. "Set as the latest release", not a pre-release or draft. The installer attached must be built from the tagged commit.
  Agents never create tags or releases; they remind the owner of these steps when a version was bumped.
- Say the new version number in the summary every time you finish a change that bumps it.

## Open items the owner knows about

Code signing certificate (installers are unsigned), a test on a clean PC / VM (incl. the elevated `C:\Program Files` install), a lawyer's review of the EULA (a drafted text, not yet reviewed),
bundling the GNU GPL v3 text referenced by the LGPL, an online catalog / auto-update for Master Pro, GPU compositing for hardware decoding.
