# drivevault-master-pro/src - rules (adds to the parent files)

- `Hub` is a QObject exposed to QML as the singleton `Hub` (module `DriveVault.Master.Backend`). Its `Options` (registry root, data dir, app dir) exist so tests can use a fake registry key and temp folders - keep new state injectable that way; never read the real registry path directly elsewhere.
- Registry access only through `QSettings(NativeFormat)` below `HKEY_LOCAL_MACHINE` / `HKEY_CURRENT_USER`; read-only (the hub never writes product keys - installers own them).
- `WinUtil` is the only place with Win32 calls (`GetFileVersionInfo`, `ShellExecuteEx`). `runAndWait` uses ShellExecute so that installers whose manifest needs administrator rights prompt for it; it blocks, so call it from a worker thread only.
- Settings live in `<dataDir>\settings.ini` (language, user installer folders). No other persistence.
- Anything that starts a process must validate the file first (exists, `.exe`, DriveVault version resource). No shell command strings, no arguments from user text.
  **Exception for the AI (`src/ai/`, see the app `CLAUDE.md`)**: `llama-server.exe` from the known engine folders, and the user's own `claude` / `codex` found on PATH; both with FIXED arguments (only a validated session id, a number and file paths of the user's own choice are ever arguments), the user's text goes in on stdin / in the HTTP body. Nothing else may use this exception.
- `src/ai/` talks to the network only from `HttpChat` (a chat request the user sent), `ModelDownloader` (a Download the user pressed) and `AiService::probeLink` (a Check the user pressed, HEAD only). A key travels only in the header of its own request, over https or to this computer. Keep conversations and settings in `<data>\ai\*.json` (keys encrypted); never log a prompt, an answer or a key.
- `Version.h` is generated from `CMakeLists.txt` (`project(... VERSION x.y.z)`); `app.rc` carries the same version by hand - change both together, and `AppVersion` comes from CMake in `package.ps1`.
- New code needs a test in `tests/tst_hub.cpp` (use `fake_setup.exe`, never a real installer).
