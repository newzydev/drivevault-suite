# drivevault-video-editor-pro/scripts - rules (adds to the parent files)

| Script | Purpose |
|---|---|
| `build.ps1` | configure (Ninja, Release / Debug) + build + optional `-Test` / `-UpdateTranslations` |
| `package.ps1` | build + tests -> `dist\stage` (windeployqt, app-local VC runtime, FFmpeg libs only avcodec / avformat / avutil / swscale / swresample, whisper-cli + DLLs, licenses, docs, `product.json`) -> **gates** -> Inno Setup -> `.sha256` |
| `license-audit.ps1` | every staged file must have an owner / license; FFmpeg LGPL-only; no GPL-only Qt module; license files and docs present; writes `dist\dependency-manifest.json` (also shipped in `licenses\`) |
| `test-installer.ps1` | 66-check install / launch / export / uninstall / reinstall / default-location / hub test (per-user, in a temp folder, minimal PATH) |
| `test-signing.ps1` | signing pipeline test on a copy with a throw-away certificate |
| `shots.ps1` | screenshots of the main screens in both languages |

- Scripts source `shared\scripts\env.ps1` and use `$SuiteRoot` / `$DvRoot`. The gates in `package.ps1` run with `$ErrorActionPreference = 'Stop'` on purpose: a crash inside a gate must stop packaging.
- When files are added to or removed from the package, update the owner rules in `license-audit.ps1` and `docs/THIRD-PARTY-NOTICES.txt` in the same change.
- `package.ps1 -Sign` signs the exe and the installer with the certificate named in `DV_SIGN_PFX` / `DV_SIGN_THUMBPRINT`; without it the output says it is unsigned - keep that note.
- `test-installer.ps1` exits with code 3 ("SKIPPED") when an all-users Master Pro (HKLM) is installed on the PC: the program installer would skip its Master step and the Master checks would fail for the wrong reason. Run it on a PC / VM without DriveVault (or set `DV_TEST_ALLOW_REAL_MASTER=1` knowing those checks will fail).
- `test-installer.ps1` installs into the user's profile (`%LOCALAPPDATA%\Programs\DriveVault`) for the default-location check and removes everything again; it never needs administrator rights and never touches real user data.
