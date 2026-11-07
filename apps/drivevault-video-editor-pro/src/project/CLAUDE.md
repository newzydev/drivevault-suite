# src/project - rules (adds to the parent files)

- `ProjectFile.cpp` is the only place that reads / writes `.dvproj` JSON and the single migration point: schema 3 now; v1 and v2 files must keep loading with defaults. A schema change needs: version bump in `core/Version.h.in`, migration, a test in `tests/unit/tst_projectfile.cpp`, a note in `docs`.
- Loading validates everything (clip -> media, clip -> track, positive durations, known kinds) and returns a readable error; a damaged file never crashes or half-loads.
- Saving is atomic (`QSaveFile`) with a `.bak`; auto-save writes a separate recovery file (never the project file); recovery is offered on the next open.
- Media references store absolute and relative paths (relative to the project folder); missing media are flagged and can be relinked.
- `ProjectManager::snapshot()` builds the immutable render snapshot (new `revision` each call, `proxyPath` through the proxy lookup). Keep it cheap (about 0.2 s for 2400 clips) - it runs on every edit.
- Tracks: at least one video and one audio track, at most one subtitle track, at most 16; only empty tracks can be removed.
