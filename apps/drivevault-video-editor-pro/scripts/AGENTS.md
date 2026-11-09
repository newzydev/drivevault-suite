# drivevault-video-editor-pro/scripts - rules (adds to the parent files)

| Script | Purpose |
|---|---|
| `build.ps1` | configure (Ninja, Release / Debug) + build + optional `-Test` / `-UpdateTranslations` |
| `package.ps1` | build + tests -> `dist\stage` (windeployqt, app-local VC runtime, FFmpeg libs only avcodec / avformat / avutil / swscale / swresample, whisper-cli + DLLs, the Whisper base model (`ai\models`), licenses, docs, `product.json`) -> **gates** -> Inno Setup -> `.sha256` |
| `license-audit.ps1` | every staged file must have an owner / license; FFmpeg LGPL-only; no GPL-only Qt module; license files and docs present; writes `dist\dependency-manifest.json` (also shipped in `licenses\`) |
| `test-installer.ps1` | **owner only, agents never run it**; 68-check install / launch / export / uninstall / reinstall / default-location / hub test (per-user, in a temp folder, minimal PATH) |
| `test-signing.ps1` | signing pipeline test on a copy with a throw-away certificate |
| `shots.ps1` | screenshots of the main screens in both languages |

- Scripts source `shared\scripts\env.ps1` and use `$SuiteRoot` / `$DvRoot`. The gates in `package.ps1` run with `$ErrorActionPreference = 'Stop'` on purpose: a crash inside a gate must stop packaging.
- When files are added to or removed from the package, update the owner rules in `license-audit.ps1` and `docs/THIRD-PARTY-NOTICES.txt` in the same change.
- `package.ps1 -Sign` signs the exe and the installer with the certificate named in `DV_SIGN_PFX` / `DV_SIGN_THUMBPRINT`; without it the output says it is unsigned - keep that note.
- `test-installer.ps1` can run on a PC that has a real all-users DriveVault (HKLM): it never touches it (per-user installs in a temp folder and `%LOCALAPPDATA%`), marks the 8 checks that need a PC without DriveVault as SKIP (the summary says how many were skipped - they are not passes; run them on a clean PC / VM before a release when possible), removes only what it created (what did not exist before it started) and adds one check that the real install is unchanged (69 checks then). `DV_TEST_REQUIRE_CLEAN=1` makes it exit with code 3 instead.
- `test-installer.ps1` installs into the user's profile (`%LOCALAPPDATA%\Programs\DriveVault`) for the default-location check and removes everything again; it never needs administrator rights and never touches real user data.
