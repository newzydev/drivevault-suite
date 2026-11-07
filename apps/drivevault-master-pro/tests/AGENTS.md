# drivevault-master-pro/tests - rules (adds to the parent files)

- `tst_hub` (logic, fake registry key `Software\DriveVaultMasterTest`, temp folders, `fake_setup.exe`) and `tst_window` (runs the real exe offscreen with `--registry-root` and `--screenshot`, then checks product colours in the picture).
- `fake_setup.exe` has a DriveVault version resource (ProductName "DriveVault Video Editor Pro", version 2.0.0), appends its command line to the file named by `DM_FAKE_MARKER` and exits with `DM_FAKE_EXIT`. Use it for any "run a setup / uninstaller" test.
- Tests write registry keys only below their own `...Test` root and remove them in `cleanup`. Never write to `Software\DriveVault`.
- Run: `.\scripts\build.ps1 -Test` (ctest). Qt Test output is lost when stdout is redirected: use `tst_hub.exe -o file,txt`.
- The installer side of the hub (install by a program installer, refusal, uninstall, clean-up) is covered by `apps/drivevault-video-editor-pro/scripts/test-installer.ps1`; extend that script when installer behaviour changes.
