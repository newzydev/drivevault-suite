# drivevault-video-editor-pro/tests - rules (adds to the parent files)

| Suite | What it covers |
|---|---|
| `unit/tst_timecode, tst_fileutils, tst_settings, tst_projectfile, tst_projectmanager, tst_language` | foundations, project file (v1-v3), autosave / recovery, Thai / English |
| `unit/tst_media` | probe, seeking, audio decoding, waveform, thumbnails, import / relink |
| `unit/tst_timeline` | every timeline operation incl. undo, locks, links, keyframes, titles, SRT, markers |
| `unit/tst_effects` | compositor + mixer measured on pixels and samples |
| `unit/tst_ai` | silence, scenes, cutting, enhancement, tracking, model manager, assistant, real whisper captions |
| `unit/tst_phase5` | frame cache, proxy, hardware decode, a 40-minute / 2400-clip project (prints `PERF` numbers) |
| `unit/tst_pipeline` | import -> edit -> save -> reopen -> preview -> export with every encoder -> decode and compare |
| `integration/tst_ui_flow` | the real QML UI offscreen, real clicks / drags / keys; fails on any QML warning |
| `performance/tst_perf` | 1080p composite fps, seek, real-time playback, export throughput |

- Add a test with every feature or bug fix; prefer real data (media generated with `TestMedia.h` and FFmpeg `lavfi`) over mocks; measure results (pixels, samples, decoded output) rather than only checking return values.
- Run: `.\scripts\build.ps1 -Test` or `ctest --test-dir build\release --output-on-failure` after `. shared\scripts\env.ps1`. UI test needs `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software` (set by CMake).
- Qt Test + custom message handler swallows `qInfo`; Qt Test output is lost when stdout is redirected - use `-o file,txt`. Print numbers with `fprintf(stderr, "PERF ...")`.
- Tests must clean up: temp dirs (`QTemporaryDir`), registry keys, undo stack, settings overrides (`paths::setOverrideRoot`). A flaky test is a bug: find the cause (one was a thumbnail write failing under load).
- New suites: `dv_add_test(tst_name)` in `tests/CMakeLists.txt`.
