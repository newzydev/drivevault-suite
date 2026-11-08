# drivevault-video-editor-pro/installer - rules (adds to the parent files)

- `windows/drivevault.iss` is compiled by `scripts\package.ps1` with `AppVersion`, `StageDir`, `OutputDir` and (unless `-NoMaster`) `MasterSetup` + `MasterVersion`. Do not run `iscc` by hand without these defines.
- Install location: `{autopf}\DriveVault\Common\DriveVault Video Editor Pro` (admin default; `dialog commandline` overrides the scope). The folder is **fixed**: `DisableDirPage=yes`, only the command line `/DIR` changes it (tests). `SuiteDir` (Pascal) = parent of the `Common` folder = where Master Pro lives.
- The Master Pro setup is extracted to `{tmp}` and run silently **before** anything else needs it (`NeedMaster`: missing or older version in HKLM or HKCU), with the same scope switch and language as this setup.
- Registry for the hub (`Software\DriveVault\Products\VideoEditorPro` incl. `Uninstaller`), the `.dvproj` association (component `assoc`), Start Menu folder `DriveVault`, optional desktop icon. The uninstaller never deletes projects and asks before deleting settings / cache; it must not remove other DriveVault programs or Master Pro.
- `AppId` `{6E1B7A52-4C3D-4F7E-9A3B-D17E5A0C2F11}` never changes. Wizard pictures: `shared\scripts\make-brand-assets.ps1` (accent `#8B5CF6`).
- Any change here must pass `scripts\test-installer.ps1` (68 checks), after rebuilding Master Pro first if its installer changed.
