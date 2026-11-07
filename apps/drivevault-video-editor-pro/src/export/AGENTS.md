# src/export - rules (adds to the parent files)

- `Exporter::run` renders with a fresh `Compositor` / `AudioMixer` (no cache, no proxies) -> encoder -> `<name>.dvpart` -> **validation** (read back: size, fps, duration, audio, first / last frame decodable) -> rename. A failed or cancelled export leaves no file. Never skip validation.
- Encoders: probed by really opening them (NVENC, QSV, AMF, Media Foundation, OpenH264); Auto picks the first that works. Audio is AAC. Containers mp4 / mov / mkv, audio-only M4A (`ipod` muxer), range export (`startFrame` / `endFrame`), PNG frame export.
- `ExportController` is the QML-facing job (worker thread, progress / ETA, cancel); `ProxyManager` builds preview proxies with `Exporter::buildProxy` (<= 960 px, short GOP, same fps, no audio, not for variable-frame-rate media).
- Preview proxies must never be used by `run`. Bitrate defaults come from `defaultBitrateKbps`; keep estimates honest.
- Tests: `tst_pipeline` (export of real media, every available encoder, cancel, range, audio-only, frame) and `tst_phase5` (proxy). Decode the output again to check pictures and sound.
