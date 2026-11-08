# DriveVault Video Editor Pro - rules (adds to the repository root file)

Native Windows non-linear video editor: C++20, Qt 6.8 / Qt Quick (QML), FFmpeg 8.1 (LGPL, shared), CMake, Inno Setup. Version 1.9.1, project file schema 3. Thai + English UI. Master spec: `../../docs/spec/DriveVault-Video-Editor-Pro-Claude-Code.md`
(phases 1-6 are done: foundation, editing, pro editing, AI, performance, packaging).

## Layout
```
src/          application code, one static library per folder (see src/CLAUDE.md)   tests/   Qt Test suites (see tests/CLAUDE.md)
resources/    fonts, icons, translations (drivevault_th_TH.ts)                        installer/windows/drivevault.iss   scripts/   build, package, tests, audit
docs/         architecture, roadmap, UI design system, user guides (th/en), release notes, THIRD-PARTY-NOTICES.txt, third-party-licenses.md
```
Output: `build/release/bin/DriveVaultVideoEditorPro.exe` (+ test exes), installer `dist/DriveVault-Video-Editor-Pro-Setup-x64.exe`. Both folders are git-ignored.

## Non-negotiables (from the specification)
- No fake features. Anything not working is labelled (status badges), never mocked. AI is optional and offline; the editor must work with no model and no network.
- No Electron / Tauri / WebView. Heavy media / AI work never runs on the UI thread. Every edit is undoable. User data (projects, exports) is never deleted by the app or the uninstaller.
- Every visible string is translatable: `qsTr` (QML) / `tr` (C++) in English, Thai in `resources/translations/drivevault_th_TH.ts`.
- Preview and export use the same renderer (`Compositor`, `AudioMixer`): what is previewed is what is exported. Proxies and the frame cache are preview-only.

## Commands (from this folder)
```powershell
.\scripts\build.ps1 -Test            # configure + build Release + 14 ctest suites (about 1 min of tests after the build)
.\scripts\package.ps1                # build + tests + windeployqt + runtime + docs + gates + Inno Setup (needs ..\drivevault-master-pro\dist\...Setup-x64.exe; -NoMaster to skip)
.\scripts\test-installer.ps1         # 68 checks: installs Video Editor + the Master Pro it carries, launches, exports, uninstalls, reinstalls, default location, suite rules
.\scripts\test-signing.ps1           # signing pipeline on a copy with a TEST certificate
.\scripts\shots.ps1                  # screenshots of the main screens (also: exe --screenshot ... --select-clip N)
```
Executable options: `--export out.mp4 [--preset 720p]` (headless), `--data-dir`, `--lang th|en`, `--screenshot`, `--screen`, `--workspace`, `--size`, `--select-clip`.

## Translations workflow
Add strings with `qsTr` / `tr`; run `cmake --build build/release --target update_translations`; translate every `type="unfinished"` entry in the `.ts` (0 may remain); rebuild. The lupdate step can leave empty stub files (`Editor`, `Pro`, `Project`, `Video`)
in the **repository root** when a path contains spaces - delete them.

## Gotchas
- QML: never name a property `onXxx`, `clip`, `layer`; ids are file-local; add `Layout.minimumWidth: 0` to sections that hold wide children or the whole panel overflows; `objectName`s are used by the UI test.
- MSVC: no `__int128`; `windows.h` needs `NOMINMAX`; beware the most vexing parse (`std::vector<T> v(size_t(x))`); `QList::append(QVariantList)` flattens a list of lists.
- The project file stays backward compatible: v1 / v2 / v3 files must still load (`ProjectFile.cpp` is the single migration point; bump `kProjectSchemaVersion` only with a migration and a test).
- `LICENSE.txt` and `installer/windows/license-*.txt` are generated from `shared/legal` by `shared/scripts/make-license.ps1` - never edit them by hand. The EULA is drafted, not lawyer-reviewed.
