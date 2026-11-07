# shared/legal - rules (adds to the parent files)

- `EULA.en.txt` and `EULA.th.txt` are the templates of the end-user licence agreement for every DriveVault program (`{{PRODUCT}}` = program name, `{{NOTES}}` = product-specific section 6 from `shared/scripts/make-license.ps1`).
  Keep both languages in step (same sections, same numbers); the Thai text prevails if they differ (section 13).
- After any change run `.\shared\scripts\make-license.ps1`, then rebuild the installers. The generated `LICENSE.txt` and `license-*.txt` files are committed but never edited by hand.
- Facts in the text must stay true of the software: offline / no telemetry, the only internet use is a download the user starts, third-party components and their licences, AI results can be wrong. If a program starts to collect data or use the network, update section 4 first.
- This is a drafted agreement (governing law: Thailand), not legal advice and not reviewed by a lawyer; do not describe it as lawyer-approved. Do not invent legal entity names, addresses or prices - the licensor is named "DriveVault" as in the installers.
