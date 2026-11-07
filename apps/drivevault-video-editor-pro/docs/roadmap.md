# Roadmap

| Phase | Scope | Status |
|---|---|---|
| 1 | Native desktop foundation: build, design system, Home, editor shell, settings, localization, logging, persistence, prototype installer | **Done** |
| 2 | Functional editing MVP: media import / probe / thumbnails / waveforms, preview + A/V-synced playback, multitrack clips, move / split / trim, undo / redo, project save / load with media references, MP4 export | **Done** (see README for scope and limits) |
| 3 | Professional tools: ripple / roll / slip / slide, copy / paste, markers, add tracks, effects, transitions, keyframes, titles / subtitles, color, audio mixing / fades, export presets | **Done** (see README for scope and limits) |
| 4 | AI: whisper.cpp captions, silence detection, smart cuts, audio enhancement, assistant, tracking | **Done** (offline; see README for scope and limits) |
| 5 | Performance & reliability: GPU decode / encode detection, proxies, frame cache, long-project tests, crash recovery, profiling | **Done** (see README for the measured results and limits) |
| 6 | Release: production installer, code signing, clean-machine testing, license audit, documentation | **Done** for the packaging; signing needs a purchased certificate and a real clean-PC test is still open (see README) |

## After 1.0.0 - open items

1. Buy a code-signing certificate and run `package.ps1 -Sign`; then test the installer on a clean Windows 10 / 11 PC or VM (including the elevated Program Files install and SmartScreen).
2. Bundle the GNU GPL v3 text (needs a one-time download approved by the project owner) and re-verify the FFmpeg library list against the builder's manifest.
3. **DriveVault Master Pro** (hub) exists in `apps/drivevault-master-pro` (done): next steps there are an online catalog / automatic updates and verifying installer signatures once the installers are signed.
4. Performance: GPU compositing so that hardware decoding becomes a real gain, neural noise suppression, heavy effect-stack benchmarks, multi-threaded export.
5. Features: titles that follow a tracked subject, speed change / reverse, source monitor, automatic updates.
