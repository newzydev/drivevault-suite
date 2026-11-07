# shared - rules (adds to the repository root file)

Everything here is used by **every** app, so a change can break all of them: keep it generic (no program-specific names, paths or rules), and after any change run `scripts\build-all.ps1`.

## scripts/

| File | Purpose / rules |
|---|---|
| `env.ps1` | Dot-sourced by every script. Imports the MSVC x64 environment once (`VSCMD_VER` guard), adds PATH entries once (`Add-ToPath`; a growing PATH broke child processes before), finds Qt in `.tools\Qt`. Sets `$SuiteRoot` (repository root) and `$DvRoot` (root of the app whose script sourced it: `apps\<app>`). Overrides: `DV_TOOLS_DIR`, `DV_QT_DIR`. |
| `check-dependencies.ps1` | Clean-machine proxy: every import of every shipped dll / exe must be shipped or a Windows inbox DLL. The Visual C++ runtime is **not** accepted from System32. Needs `dumpbin` (from `env.ps1`). Exit code 1 lists what is missing. |
| `sign.ps1` | Authenticode signing with `signtool`. Real certificates come from `DV_SIGN_PFX` + `DV_SIGN_PASSWORD` or `DV_SIGN_THUMBPRINT`; `-SelfSigned` makes a throw-away TEST certificate (not trusted, removed from the store afterwards). **Never commit certificates, keys or passwords.** |
| `make-brand-assets.ps1` | Draws installer wizard pictures in a program's accent colour (`-Accent -Title -Logo -Out`). |
| `make-license.ps1` | Generates every app's `LICENSE.txt` and installer licence pages from `shared/legal/EULA.*.txt` (+ the per-product section in the script). Run it after editing the templates; commit the results. **The script itself must stay UTF-8 with BOM** (it contains Thai; PowerShell 5.1 reads a BOM-less file as ANSI and the Thai text comes out garbled); the templates are read as UTF-8. |

Rules: scripts must work from any current directory; quote paths; return a non-zero exit code on failure; do not assume a program name.
