# src/timeline - rules (adds to the parent files)

- `TimelineModel` owns clips (`ClipData`), selection, markers, In / Out and the `QUndoStack`. Every operation is "remove these clip versions, add those" (`push(text, remove, add, mergeKey)`); never mutate `m_clips` directly.
- Linked clips (same `linkId`: video + its audio, or a user group) are edited together (`group()`, `movementSet()`); locked tracks refuse edits (`trackLockedForClip`).
- Parameters / keyframes: registry in `core/ClipParams.cpp` (`params::all()`); only non-default values are stored; keyframe frames are clip-relative; titles are clips of type `title` (subtitles on a `subtitle` track).
- AI-assisted edits (`removeRanges`, `replaceMedia`, `setKeyframes`, `addSubtitles` in `TimelineAi.cpp`) must stay one undo step each (macro or single push). `removeRanges` ripples **all** tracks together and refuses locked tracks.
- Methods return `""` on success or a translated user-facing reason; they never throw.
- A new operation needs: an undo-exact implementation, a test in `tests/unit/tst_timeline.cpp` (including undo and linked / locked cases), and a `Q_INVOKABLE` only if QML needs it.
- Keep it fast with thousands of clips (`tst_phase5` has a 2400-clip test): avoid O(n^2) per edit on hot paths.
