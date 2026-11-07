# drivevault-video-editor-pro/src - rules (adds to the parent files)

## Module map (each folder = one static library `dv_<name>`; dependencies point downwards only)
```
ui (QML) -> app (AppContext / AppController / main) -> ai, export, playback -> project -> timeline, media, settings, localization -> core
platform (Windows integration) is used by app only.
```
| Folder | Contents |
|---|---|
| `core` | paths, logging, atomic file writes, rational timecode, version, shared plain types (`ClipData`, `TrackInfo`, `ClipParams`, `RenderSnapshot`, `MediaTypes`) |
| `settings` | schema-driven, validated settings (JSON); add keys in `SettingsManager.cpp` with defaults and ranges |
| `localization` | runtime Thai / English switching |
| `media` | FFmpeg adapters: `MediaProbe`, `VideoReader`, `AudioReader`, thumbnails / waveforms / cache, `MediaLibrary`, `HardwareDecode` |
| `timeline` | `TimelineModel`: clips, selection, every edit as an undoable command (`TimelineModel.cpp`, `TimelineTools.cpp`, `TimelineAi.cpp`) |
| `project` | `.dvproj` (schema 3), `ProjectManager` (media + timeline + tracks + dirty + autosave), `TrackListModel`, recents |
| `playback` | `Compositor`, `Effects`, `AudioMixer`, WinMM `AudioOutput`, `PlaybackEngine`, `FrameView` |
| `export` | `Exporter` (encode, validate, proxy, audio-only, PNG frame), `ExportController`, `ProxyManager` |
| `ai` | offline AI services: silence / scenes, enhancement, tracking, whisper.cpp captions, model manager, assistant, `AiController` |
| `app` | `AppContext` (all QML-facing backend objects), `AppController`, `main.cpp` (command line, headless export) |
| `ui` | QML module `DriveVault.UI` (see `ui/CLAUDE.md`) |

## Rules
- FFmpeg headers only in `media/` and `export/`; QML never touches FFmpeg. New QML-facing objects are registered in `AppContext::registerQmlTypes`.
- Work on a snapshot: renderers and workers receive an immutable `RenderSnapshot` built by `ProjectManager::snapshot()` (each one has a unique `revision`); they never read live models. Decoders are one-thread objects.
- Positions are frame indices at the project frame rate; source time = `frameToMicroseconds(sourceIn + (frame - start))`; audio positions use `frameToSample` (rational, no drift).
- Edits go through `TimelineModel::push` / `editParams` (undo stack). A slider drag merges into one undo step with a `mergeKey`.
- UI thread: models, QML, snapshot building only. Workers: `QThread` + queued signals back; support cancel; report progress.
- Every new CMake source file must be added to the folder's `CMakeLists.txt`; every new library to `src/app/CMakeLists.txt` (`dv_appcore`) and, if tests use it, to `tests/CMakeLists.txt`.
- Log with `qCInfo/qCWarning(lcProject)` (see `core/Logger.h`); no `qDebug` left behind.
