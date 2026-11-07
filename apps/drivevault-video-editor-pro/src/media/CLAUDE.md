# src/media - rules (adds to the parent files)

- FFmpeg wrappers live here (`FFmpeg.h` RAII helpers). `VideoReader` returns "the frame whose timestamp is <= t" (timestamps, not frame counters, so variable-frame-rate media stays in sync); `AudioReader` is random-access stereo float at a fixed rate. Both are one-thread objects.
- FFmpeg must stay an **LGPL, shared** build (no `--enable-gpl`, no `--enable-nonfree`); `license-audit.ps1` checks the configuration. Do not add libraries that change that.
- Hardware decoding (`HardwareDecode.*`, D3D11VA) is experimental and **off by default** because it measured slower than the CPU (frames are copied back to memory); it falls back to software automatically. Do not turn it on by default without new benchmark numbers from `tst_phase5`.
- `MediaLibrary` imports in the background (probe, thumbnail, waveform, peaks cache). Cache files are written to a temp name and renamed, with short retries (Windows scanners lock new files) - keep that pattern for any new cache file.
- Never assume a media file exists, is readable or has audio / video: report errors as messages, never crash; offline media is flagged, not removed.
- Tests use files generated with the FFmpeg `lavfi` sources (`tests/unit/TestMedia.h`), not committed media.
