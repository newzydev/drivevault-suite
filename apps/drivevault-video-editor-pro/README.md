# DriveVault Video Editor Pro

*Professional Power. Beautiful Simplicity.*

Native Windows desktop video editor - C++20, Qt 6 / Qt Quick, FFmpeg, CMake. UI languages: ไทย / English.

## Status: Version 1.11.4 - Phase 6 product packaging (on top of the Phase 3 editor, the Phase 4 AI tools and the Phase 5 performance work)

Import -> edit -> grade / animate / title -> mix -> save -> reopen -> export works end to end and is covered by automated tests.
Anything not listed as implemented is visibly disabled or labelled with the phase that delivers it.

| Implemented | Not implemented yet |
|---|---|
| **Media import** (dialog, folder, drag & drop): MP4, MOV, MKV, AVI, WebM, MP3, WAV, AAC, FLAC, PNG, JPG, WebP ... via FFmpeg; probe, background import, thumbnails, waveforms, relink | Neural noise suppression, GPU compositing / zero-copy decoding (later) |
| **Multitrack timeline**: add / remove video, audio and **subtitle** tracks (up to 16), move, trim, split, delete, ripple delete, snapping, linked video + audio, copy / cut / paste, group / ungroup, multi-selection moves, track hide / mute / lock / **solo** | GPU decode / proxy editing / frame cache tuning (Phase 5) |
| **Pro edit tools**: ripple trim (`B`), roll (`N`), slip (`Y`), slide (`U`), markers (`M`, rename / delete, next / previous), In / Out range (`I` / `O`) | Speed ramps / reverse, nested sequences, multicam, source monitor (later) |
| **Per-clip inspector with keyframes**: opacity, position, scale, rotation; exposure, brightness, contrast, saturation, temperature, tint; lift / gamma / gain (RGB); RGB + luma **curves**; **.cube LUT** import; blur, sharpen, vignette, crop. Keyframe diamond on every parameter, linear / ease / hold interpolation, one undo step per edit | Production installer signing, license audit (Phase 6) |
| **Transitions**: cross dissolve, dip to black, fade from / to black, wipe, slide, zoom (in on the incoming clip, fade / dip out) | Audio cross-fade as part of a transition (audio uses per-clip fades) |
| **Titles, lower thirds, captions and subtitles** (Thai + English text, font / size / colour / outline / shadow / alignment / position), **SRT import / export** | |
| **Audio**: per-clip volume (keyframable), 3-band EQ, noise gate, fade in / out; per-track gain / pan / mute / solo; master soft limiter; **mixer panel with live level meters** | |
| **Undo / Redo** of every edit (slider drags merge into one step), unsaved-changes guard, `.dvproj` schema **v3** (v1 / v2 files open with defaults), auto save + crash recovery | |
| **Preview**: one renderer for preview and export (effects, transitions, titles, mixer are all in it), frame-accurate stepping, play / pause with audio (WinMM clock), preview quality Full / 1/2 / 1/4 | |
| **Export** MP4 / MOV / MKV (H.264 + AAC) and **audio-only M4A**; **presets** (YouTube 1080p / 4K, Instagram / TikTok, Web 720p, Archive, Audio only) + your own saved presets; **In / Out range**; **current frame as PNG**; subtitles as SRT; encoder choice, progress / ETA / cancel, atomic output, automatic validation of the exported file; headless `--export` | |
| **AI workspace** (optional, 100 % offline): **auto captions** with whisper.cpp (Thai / English / auto-detect, progress + cancel, editable subtitle clips, SRT export) with a **model manager** (list / import / select / delete / download on request); **silence detection** (threshold + minimum length, highlighted on the timeline, removed only after confirmation, one undo step); **smart cut suggestions** (scene changes + pauses, split on click); **audio enhancement** (spectral noise reduction, rumble filter, level evening - classical DSP, labelled as such); **object tracking** (template matching) that stabilises a clip or makes another clip follow the subject via Position keyframes; **assistant** that turns Thai / English requests into typed commands (keyword rules, not a chat model) | |
| **Performance** (Phase 5): preview **frame cache** (LRU, a quarter of the memory limit up to 1 GB, never serves a frame of an older edit) with **read-ahead** while playing; **proxy editing** (H.264, <= 960 px, short GOP, built in the background with progress / cancel, preview only - export always reads the originals); **hardware detection** by really opening the devices (decoders: D3D11VA, DXVA2, CUDA, QSV; encoders: NVENC / QSV / AMF / MF) and an optional, experimental **Direct3D 11 decoder** for 4K pictures with automatic CPU fallback; live statistics (fps, dropped frames, render time, cache hit rate, GPU / proxy use) in Settings -> Performance; thumbnail writing made robust under load (fixes an intermittent import failure) | |
| **Product packaging** (Phase 6): installer to `C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro` (all users; per-user choice in the wizard), a shared **DriveVault** Start Menu folder, `product.json` + registry entry for the future **DriveVault Master Pro** hub, self-contained runtime (Visual C++ DLLs app-local - earlier installers silently relied on them being on the PC), brand-coloured wizard, Thai / English **user guide**, release notes, third-party notices, a **license audit** (FFmpeg is LGPL-only, no GPL-only Qt, every file has an owner) with a dependency manifest, a **clean-machine import check**, signing pipeline | |
| Thai / English UI with runtime switching (about 930 strings), Settings, logging, installer / uninstaller | |

## Prerequisites

* Windows 10 / 11 x64
* Visual Studio 2022 or Build Tools with the **Desktop development with C++** workload (MSVC 14.4x, Windows SDK)
* CMake >= 3.28 and Ninja
* Qt 6.8 (open-source, `msvc2022_64`) with Qt Quick, Quick Controls 2, Quick Dialogs and Linguist tools
* **FFmpeg shared build (LGPL)** with headers and import libraries, e.g. BtbN `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip`
* Inno Setup 6 (only for the installer)

`scripts\env.ps1` finds the compiler through `vswhere` and looks for CMake, Ninja, Inno Setup and Qt in a
suite's `.tools\` folder (at the repository root) first (`.tools\cmake`, `.tools\ninja`, `.tools\InnoSetup`,
`.tools\Qt\<version>\msvc2022_64`), otherwise on `PATH`. FFmpeg is expected in `.tools\ffmpeg`
(`include\`, `lib\*.lib`, `bin\*.dll`) or in the folder given by `-DDV_FFMPEG_DIR=<dir>` / `$env:DV_FFMPEG_DIR`.
Set `DV_QT_DIR` to use another Qt install. `.tools\` is git-ignored.
Portable Qt without the online installer: `pip install aqtinstall`, then
`aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O .tools\Qt`.

## Build, run, test

```powershell
.\scripts\build.ps1 -Test          # configure + build Release + run all tests
.\build\release\bin\DriveVaultVideoEditorPro.exe
```

If Qt is not on `PATH`, run the executable from a shell where you first did `. .\scripts\env.ps1`.
Other options: `-Config Debug`, `-Clean`, `-UpdateTranslations` (runs lupdate).

Command line: `DriveVaultVideoEditorPro.exe [project.dvproj] [options]`

| Option | Meaning |
|---|---|
| `--export <file.mp4\|mov\|mkv> [--preset 720p\|1080p\|1440p\|4k]` | Render the opened project without showing the UI; exit code 0 = valid file written |
| `--select-clip <index>` | Diagnostic, with `--screenshot`: select that clip first so the inspector is shown |
| `--data-dir <dir>` | Keep settings / logs / recents / cache in a folder (tests, portable runs) |
| `--lang th\|en` | UI language for this run |
| `--screenshot <png> [--screen home\|editor\|settings\|newproject\|about] [--page ...] [--workspace ...] [--size WxH]` | Diagnostic: render a screen to a file and exit |

### Tests

`ctest --test-dir build\release --output-on-failure` runs 14 suites (≈ 40 s):

* unit: timecode, file utils, settings, project file (v1 -> v2 migration), project manager, language,
  **media** (probe, frame-accurate seeking, audio decoding, waveform, thumbnails, import / relink / sort),
  **timeline** (44 tests: basic edits, ripple / roll / slip / slide, copy / paste, groups, keyframes, params, titles, SRT, markers, tracks),
  **effects** (16 tests: the compositor and the mixer measured on rendered pixels and audio samples - opacity, transform, colour, curves, LUT, blur, transitions, titles incl. Thai, volume, pan, solo, EQ, gate, limiter)
* `tst_ai` - silence runs, SRT parsing, assistant phrases (Thai + English, injection-style text is rejected), noise reduction measured in dB, **silence detection on a real file and cutting it (contiguous result, one undo)**, scene change at the exact frame, the **tracker following a moving square**, stabilise keyframes, model import / select / download (file:// stand-in) / delete, and **real captions: Windows speech synthesis -> whisper.cpp + the base model -> text contains the spoken words -> subtitle clips -> SRT** plus cancelling a running transcription
* `tst_phase5` - frame cache (identical pictures, hit vs miss time, no stale frame after an edit, LRU budget, scrub speed), proxy (built, same aspect / fps / duration, close to the original, decodes faster than a 4K original, cancel leaves no file, clearing), hardware decoding (same picture as software, GPU frames counted, safe fallback) and a **40-minute project with 2400 clips** (snapshot, far render, random scrubbing, edit, save, reopen - timings are printed with `PERF`)
* `tst_pipeline` - real media: import -> edit -> save -> reopen -> missing / relink -> preview -> **export with every available
  encoder** -> the exported file is decoded again and its picture (red -> blue -> yellow) and sound are compared with the
  edited sequence; cancel leaves no file; playback runs in real time with the audio clock and stops at the end
* `tst_ui_flow` - loads the real QML UI offscreen and **clicks / drags the real controls**: create project, double-click media
  to add it, inspector sliders and keyframe diamonds, tool keys, markers, In / Out, copy / paste, mixer buttons, export presets, `Ctrl+K` split, `Ctrl+Z` / `Ctrl+Shift+Z`, `Delete`, drag a clip, drag a trim handle, play / pause, export from the
  Export workspace, unsaved-changes dialog, settings, language switch, both themes; fails on any QML warning
* `tst_perf` - 1080p: composite fps, seek latency, real-time playback, export throughput (numbers are printed)

`scripts\shots.ps1 -OutDir build\shots [-Project x.dvproj]` renders every main screen in both languages to PNG.

### Installer and release

```powershell
.\scripts\package.ps1              # build + 14 test suites + windeployqt + runtime + docs + gates + Inno Setup  (add -Sign for a release)
# -> dist\DriveVault-Video-Editor-Pro-Setup-x64.exe (+ .sha256, dependency-manifest.json)   (about 66 MB incl. Master Pro)
.\scripts\test-installer.ps1       # 68 checks: install (+ Master Pro installed automatically) -> launch -> export -> Master Pro lists the program -> uninstall -> reinstall -> default location -> suite / hub rules
.\scripts\check-dependencies.ps1 -AppDir dist\stage   # clean-machine proxy: every import of every shipped file resolves
.\scripts\license-audit.ps1 -StageDir dist\stage      # owner + license of every file, FFmpeg must be LGPL, notices present
.\scripts\test-signing.ps1         # signing pipeline on a copy with a throw-away TEST certificate
.\scripts\make-brand-assets.ps1 -Accent "#8B5CF6"     # wizard artwork in the product colour
```

Where it installs: **`C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro`** (with **DriveVault Master Pro** in `C:\Program Files\DriveVault`, see below) (all users, administrator rights asked once). "Just for me" in the
wizard (or `/CURRENTUSER`) installs to `%LOCALAPPDATA%\Programs\DriveVault\Common\DriveVault Video Editor Pro`. The `DriveVault` folder is shared by the future
products and is removed only when empty. The hub contract (folder layout, `product.json`, registry keys, silent switches, colours) is in
[`docs/suite-integration.md`](../../docs/suite-integration.md).

**DriveVault Master Pro** (the hub, `apps\drivevault-master-pro` in this repository) is carried inside this installer and installed **automatically first** when it is missing or older (same all-users / current-user scope and language), so every program always has the hub around it. The installer therefore contains `DriveVault-Master-Pro-Setup-x64.exe`: build Master Pro first (`package.ps1` of that repository), then this one (`-NoMaster` skips it for experiments). Master Pro stays when this program is uninstalled and refuses to be uninstalled while any DriveVault program is installed.

The installer: Windows 10+ x64 check, language choice (ไทย / English), license page, install folder, optional `.dvproj` association, optional desktop
shortcut, Start Menu folder **DriveVault**, registered uninstaller. The uninstaller never touches projects, asks before deleting settings / logs / cache and
leaves other DriveVault products alone.

**Honest status of the release**: the installer is **not code-signed** (a certificate has to be bought; `package.ps1 -Sign` does the signing once
`DV_SIGN_PFX` / `DV_SIGN_THUMBPRINT` is set), so Windows SmartScreen will warn. It was tested by the scripts above on development machines, **not** on a separate
clean PC or a virtual machine, and the all-users path under `C:\Program Files` needs administrator rights and was not run by the tests (the per-user default path was).

## Where things are stored

| What | Where |
|---|---|
| Settings, recent projects, logs, auto-save copies, thumbnail / waveform cache | `%LOCALAPPDATA%\DriveVault\VideoEditorPro` |
| Projects (default) | `Documents\DriveVault Projects\<name>\<name>.dvproj` (media stay where they are; referenced by relative + absolute path) |
| Exports (default) | `Videos\DriveVault Exports` |

## Keyboard shortcuts

`Ctrl+N` new, `Ctrl+O` open, `Ctrl+S` save, `Ctrl+Shift+S` save as, `Ctrl+I` import media, `Ctrl+M` export workspace, `Ctrl+,` settings,
`Space` play / pause, `←` / `→` previous / next frame, `Home` / `End` start / end, `Ctrl+K` split, `Q` / `W` trim start / end to playhead,
`Delete` delete, `Shift+Delete` ripple delete, `Ctrl+A` select all, `Ctrl+C` / `Ctrl+X` / `Ctrl+V` copy / cut / paste, `Ctrl+G` / `Ctrl+Shift+G` group / ungroup,
`V` select, `B` ripple, `N` roll, `Y` slip, `U` slide, `M` marker, `Shift+M` / `Ctrl+Shift+M` next / previous marker, `I` / `O` In / Out, `Alt+X` clear range, `Ctrl+Z` undo, `Ctrl+Shift+Z` / `Ctrl+Y` redo, `+` / `-` timeline zoom
(also `Ctrl` + mouse wheel), `Esc` leave the full-screen monitor, `F1` about.

## Known limitations of the 1.0.0 release

* Not code-signed; not tested on a clean machine / VM; the elevated (Program Files) install was not exercised by the automated tests.
* The GNU GPL v3 text that the LGPL v3 incorporates is referenced by URL in the notices, not bundled (the LGPL text is). Qt source: download.qt.io, offer in the notices.
* The FFmpeg DLLs contain further libraries; their list in the notices comes from the build's configuration and has to be re-verified at each FFmpeg update. H.264 / AAC patent
  licensing is the distributor's responsibility.
* "DriveVault Master Pro" does not exist yet; only the integration contract and the product manifest / registry entry are in place.
* Automatic updates are not implemented (a newer installer upgrades in place).

## Known limitations of Phase 5

* **GPU decoding is experimental and off by default.** It is correct (same picture as the CPU, automatic fallback) but on the test computer (NVIDIA, D3D11VA) it
  was *slower* than CPU decoding even for 4K H.264 (about 570 ms vs 320 ms for 60 frames) because each picture is copied back to system memory for the CPU
  compositor. A real GPU speed-up needs GPU compositing / zero-copy rendering, which is not built. Hardware **encoding** is automatic and works.
* The frame cache is in memory only (lost when the project is edited or closed) and holds rendered preview frames, not source frames.
* Proxies are not made for variable-frame-rate media (the timing would drift) and are not created automatically; use the Inspector button or
  Settings -> Performance. Deleting proxies of a file that the preview still has open can fail until the project is closed.
* Preview export parity: the preview may use proxies, the export never does - small colour differences from the H.264 proxy are expected in the preview.
* Timings were measured on one machine; other hardware will differ (see the `PERF` lines of `tst_phase5`).

## Known limitations of Phase 4

* **Captions**: accuracy is that of whisper.cpp and the chosen model (the `base` model is fast but weaker on Thai, names, music and noise - try
  `small`). The whole timeline mix is transcribed, so music under speech can produce wrong text; whisper may invent text in long pauses.
  Phrase timing is whisper's, not word-accurate. The speech engine is a CPU build (no GPU).
* **Models are not in the installer** (size): download them in the AI workspace (explicit click, needs internet once) or import a `ggml-*.bin`. Without a model the
  feature shows "Model required" and everything else still works.
* **Audio enhancement** is spectral noise reduction, not a neural speech enhancer: it removes steady noise well, not voices, music or sudden noise, and can
  leave slight "watery" artefacts at high strength. It writes a WAV copy into `<project>\AI` (the original is untouched; Undo swaps back).
* **Silence / cut analysis** works on the mix of the whole timeline; removing silence cuts every track together (all tracks must be unlocked) and does
  not shift markers. Scene detection compares small greyscale pictures, so slow dissolves and flashes can be missed or over-reported (use the sensitivity).
* **Tracking** follows a rectangle by appearance (no scale / rotation, can drift when the subject is hidden or changes a lot); at most 900 frames per run.
  Titles cannot be tracked or made to follow (their position is not keyframable yet).
* **Assistant**: a fixed set of Thai / English phrases (see the hint text), not a conversational model; the "track" request asks you to choose the region first.
* One AI task runs at a time; analyses are CPU-only.

## Known limitations of Phase 3

* Transitions live on the incoming clip's left edge (and fade / dip on the outgoing edge); the outgoing neighbour must end exactly where
  the incoming clip starts. A transition does not cross-fade audio - use the per-clip fades.
* Effects run on the CPU (QPainter / own kernels). the performance suite measures plain 1080p only - heavy stacks (large blur + LUT + curves) were not benchmarked and may
  preview below real time; use preview quality 1/2 or 1/4. A GPU path is Phase 5.
* Slide works on a single ungrouped clip with a neighbour on both sides; roll needs a clip that starts exactly where the other ends.
* Curves use smooth (cosine) interpolation between control points, baked into lookup tables; the colour pipeline is 8-bit sRGB, no HDR / colour management.
* Subtitles are burned into the picture (no soft-sub tracks in MP4 / MKV yet); the SRT export is the way to keep them separate.
* Software decoding only (hardware decode is Phase 5); very large (4K+) sources will preview better at 1/2 or 1/4 quality.
* No source-clip monitor yet; no speed change / reverse.

## Documentation

[`docs/architecture.md`](docs/architecture.md) - [`docs/ui-design-system.md`](docs/ui-design-system.md) -
[`docs/roadmap.md`](docs/roadmap.md) - [`docs/third-party-licenses.md`](docs/third-party-licenses.md)

## Translation workflow

Source strings are English (`qsTr` in QML, `tr` in C++). `.\scripts\build.ps1 -UpdateTranslations` extracts new
strings into `resources/translations/drivevault_th_TH.ts`; translate the entries marked `unfinished` (Qt Linguist
or a text editor); the next build compiles and embeds them. Never hard-code visible text.
