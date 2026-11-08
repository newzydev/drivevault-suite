# src/ai - rules (adds to the parent files)

- AI is an **optional module** on top of the editing core: nothing in `timeline`, `playback` or `export` may depend on it. Everything runs offline on this PC; nothing is uploaded.
- Analyses (silence, scenes, captions, tracking, enhancement) only produce results; **applying them is a separate user-confirmed call** (`apply*`) and is one undo step. Never edit the project from an analysis.
- Labels are honest: enhancement is classical spectral noise reduction (not a neural model); the assistant is a deterministic keyword parser (Thai + English) producing typed commands - not a language model (there is no language model in the program); tracking is template matching. Captions can be wrong; say so in the UI.
- whisper.cpp runs as a **separate process** (`whisper-cli.exe`, found via `whisperExecutable()`: `<app>\ai\whisper`, `DV_WHISPER_DIR`, or the suite `.tools`). Progress is parsed from its output; cancel kills it. The Whisper base model (ggml) is installed with the program in `<app>\ai\models`; other models are downloaded (explicit click, Hugging Face) or imported by the user; `ModelManager` validates imports (magic + size).
- The assistant may only call the services in `AiController`; no shell commands, no file access outside the project, no text from the user passed to a process.
- One AI task at a time (`AiController::busy`), on a worker thread, with progress and cancel.
- Tests in `tests/unit/tst_ai.cpp` use real data: generated audio / video, the Windows speech synthesiser -> real whisper + model (skipped only when whisper or a model is absent).
