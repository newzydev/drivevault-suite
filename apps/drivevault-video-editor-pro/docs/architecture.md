# Architecture (version 1.11.0)

DriveVault Video Editor Pro is a **native Windows desktop application**: C++20, Qt 6 (Qt Quick / QML), FFmpeg, CMake.
No Electron, Tauri, WebView or web server is involved.

## Module map

```
src/
  core/          dv_core          Paths, logging, atomic file writes, rational timecode, version,
                                  shared plain types (MediaInfo, ClipData, TrackInfo, RenderSnapshot)
  settings/      dv_settings      Schema-driven, validated, persisted settings (JSON)
  localization/  dv_localization  Runtime Thai / English switching (Qt translations)
  media/         dv_media         FFmpeg adapters: MediaProbe, VideoReader (frame at time), AudioReader (random-access PCM),
                                  thumbnails / waveform peaks / cache, MediaLibrary (async import model)
  timeline/      dv_timeline      TimelineModel: clips, selection, every edit as an undoable command (QUndoStack)
  project/       dv_project       .dvproj (schema v2), ProjectManager (media + timeline + tracks + dirty + autosave),
                                  recent projects, track model
  playback/      dv_playback      Compositor (frame of the timeline), AudioMixer, AudioOutput (WinMM), PlaybackEngine, FrameView
  export/        dv_export        Exporter (FFmpeg encode + validation), ExportController (QML facing job)
  platform/      dv_platform      Windows integration (dark title bar), system info, crash logging
  app/           dv_appcore       AppContext (backend objects exposed to QML), AppController
                 dv_resources     Fonts, icons, compiled translations
                 DriveVaultVideoEditorPro.exe    main(): command line (incl. headless --export), fonts, engine start-up
  ui/            dv_ui            static QML module "DriveVault.UI" (all screens and components)
tests/
  unit/          Qt Test (timecode, files, settings, project file, project manager, language, media, timeline, pipeline)
  integration/   tst_ui_flow: the real QML UI offscreen, real clicks and drags
  performance/   tst_perf: 1080p numbers
```

Dependencies point downwards only: `ui (QML) -> Backend objects -> project -> timeline / media / settings -> core`.
FFmpeg headers are included only inside `media/` and `export/`; QML never touches FFmpeg.

## Data flow

* **Immutable render snapshot.** `ProjectManager::snapshot()` copies everything a renderer needs (tracks with hide / mute flags,
  clips, media paths and info, fps, sample rate) into a `RenderSnapshot`. It is rebuilt whenever clips, tracks or media change and
  handed to worker threads, which never read live models.
* **One renderer for preview and export.** `Compositor::render(snapshot, frame, size)` and `AudioMixer::render(snapshot, sample, n)`
  are used by `PlaybackEngine` (preview) and `Exporter`, so what you see is what you export. Clip positions are frame indices at the
  project frame rate; source time = `frameToMicroseconds(sourceIn + (frame - start))`; audio sample positions use
  `frameToSample` (rational, no drift). `VideoReader` returns "the frame whose timestamp is <= t", so variable-frame-rate media stays in sync.
* **Playback.** `PlaybackEngine` runs a `RenderWorker` thread (newest request wins; replaced requests are counted as dropped frames).
  `AudioOutput` (WinMM, 4 buffers sized from the latency setting) pulls the mixer on its own thread; the device position is the master
  clock, a UI timer maps it to a frame and moves the playhead. Without an audio device it falls back to a monotonic timer.
* **Editing.** Every operation is "remove these clip versions, add those" pushed on a `QUndoStack`; linked clips (same `linkId`) are edited
  together; `clampMove` / `clampTrimStart/End` give the UI snapped, overlap-free positions during drags and the commit re-validates.
* **Project file.** JSON with `schemaVersion` 2: media (absolute + relative path, probed info), tracks, clips. `fromJson` validates every
  reference (clip -> media, clip -> track, positive durations) and is the single migration point (v1 files open as an empty timeline).
  Saving is atomic (`QSaveFile`) with a `.bak`; auto save writes a separate recovery file.
* **Export.** `ExportController` validates the request, runs `Exporter::run` on a worker thread: composite -> swscale (RGB -> BT.709 YUV)
  -> encoder (first that opens: NVENC, QSV, AMF, Media Foundation, OpenH264) and mixed audio -> AAC; written to `<name>.dvpart`, validated by
  reading it back (`MediaProbe` + decode of first and last frame), then renamed into place. Cancel / failure removes the partial file.
* **Settings / localization**: unchanged from Phase 1 (`AppSettings.values.<key>`, `qsTr` + `drivevault_th_TH.ts`).

## AI module (Phase 4)

`src/ai` (`dv_ai`) is an optional layer on top of the editing core: nothing in `timeline/`, `playback/` or `export/` depends on it.

* `AudioAnalysis` - level curve of the **timeline mix** (same `AudioMixer` as preview / export), pure `findSilentRuns`, scene changes
  from the `Compositor` at 64x36 (cuts already at clip edges are ignored).
* `AudioEnhancer` - STFT spectral subtraction (noise learned from the quietest tenth), 80 Hz high-pass, level normalisation; writes a
  16-bit WAV into `<project>\AI`, which is imported and swapped into the clip (`TimelineModel::replaceMedia`, undoable).
* `Tracker` - template matching (SAD, search window) on a reduced grey picture; the result becomes Position keyframes
  (`TimelineModel::setKeyframes`) to stabilise a clip or make another clip follow the subject.
* `Transcriber` - mixes the timeline to 16 kHz mono, runs `whisper-cli.exe` (separate process: progress parsed from its output, cancel
  kills it), parses the SRT; `AiController::applyCaptions` adds one subtitle clip per phrase in a single undo step.
* `ModelManager` - ggml models in the model folder: list / import (validated by magic + size) / delete / select / download on demand
  (`QNetworkAccessManager`, only when the user presses Download).
* `Assistant` - deterministic keyword parser (Thai + English) producing typed commands; it is not a language model and can only call
  the services above.
* `AiController` ("Ai" in QML) - one worker thread at a time with progress / cancel; analyses only produce results, `apply*` calls
  (behind confirmation dialogs) edit through the undo stack (`removeRanges` ripples every track together).

## Performance layer (Phase 5)

* **Frame cache** (`Compositor::setCacheBudget`): keyed by snapshot revision + frame + size. `ProjectManager::snapshot()` stamps every snapshot with a new
  revision, so any edit, track change or media change invalidates everything; only the preview worker enables it (the exporter never renders a frame twice).
  `RenderWorker` reads ahead up to six frames while no newer request waits; read-ahead renders are not counted as viewer misses.
* **Proxies** (`Exporter::buildProxy`, `ProxyManager`): `SnapshotClip::proxyPath` is filled through a lookup the manager installs in `ProjectManager`; the
  `Compositor` uses it only after `setUseProxy(true)` (preview). Files live in `<cache>\proxy\<sha1(path|size|mtime)>.mp4`, so a changed source never
  matches an old proxy.
* **Hardware** (`media/HardwareDecode`): devices are probed by creating them; `VideoReader` attaches the shared D3D11VA device for pictures of 3000 px or more
  when allowed, copies each frame to NV12 and converts as usual; a codec / driver that refuses simply decodes in software. Counters feed the statistics.
* **Metrics**: `PlaybackEngine` exposes fps, dropped frames, render time, cache hit rate, hardware use and proxy use; `tst_phase5` prints the benchmark numbers.

## Key decisions

| Decision | Reason |
|---|---|
| FFmpeg LGPL **shared** build, own thin adapters (no MLT in Phase 2) | LGPL-safe dynamic linking; MLT would add GPL-module risk and an extra abstraction for features Phase 2 does not need. Revisit for transitions. |
| Encoder auto-detection by actually opening each H.264 encoder | Feature detection must reflect real driver / codec availability, with software fallback (see the project rules: no fake features). |
| WinMM `waveOut` for audio output | Built into Windows, no extra dependency or Qt Multimedia module; device position gives a reliable master clock. |
| Own compositor (QPainter + swscale) | Simple, deterministic and shared by preview and export; effects get a GPU path in Phase 5 if needed. |
| `QUndoStack` with one generic replace-clips command | Exact undo for every edit with very little code; no per-operation inverse logic to get wrong. |
| Linked clips via `linkId` | Matches NLE behaviour (video + its audio move / trim / split together) without a separate "group" object. |
| Peaks cache + thumbnail cache keyed by path + size + mtime | Re-import and project reopen are instant; cache is trimmed to a configurable size. |
| `QT_SCALE_FACTOR` before `QGuiApplication` / `qt_add_lupdate` explicit | Same Phase 1 reasons. |

## Threading

UI thread: models, QML, snapshot building. Worker threads: media probe / thumbnails / waveforms (`QThreadPool`, cancel flags),
`RenderWorker` (preview frames), audio feeder (WinMM), export thread. Decoders are never shared between threads
(`VideoReader` / `AudioReader` documented as one-thread objects); results return through queued signals.
