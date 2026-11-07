# src/playback - rules (adds to the parent files)

- `Compositor::render(snapshot, frame, size)` and `AudioMixer::render(snapshot, sample, n)` are shared by preview and export: a visual or audio change belongs here once, and `tst_effects` must measure it on real pixels / samples.
- One instance = one thread (they own decoders and filter state). `Compositor` frame cache (`setCacheBudget`) and proxies (`setUseProxy`) are for the preview worker only; the exporter must keep them off.
- The cache key includes `snapshot.revision`: never reuse frames across revisions, never keep a snapshot revision 0 in the cache.
- Effect order is fixed (tone + saturation + LUT + vignette -> blur / sharpen -> crop); transitions are drawn on the incoming clip; titles are rasterised with QPainter (Thai via Noto Sans Thai / Leelawadee UI).
- Audio chain: per-clip EQ / gate / volume / fades -> track gain / pan / solo / mute -> master soft limiter. Preview audio clock (WinMM) is the master clock; `PlaybackEngine` maps it to frames.
- Real-time budget: a 1080p frame should stay well below the frame time; `tst_perf` / `tst_phase5` print `PERF` numbers - compare them when touching hot paths.
