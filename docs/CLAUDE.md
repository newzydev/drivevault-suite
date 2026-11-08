# docs - rules (adds to the repository root file)

- `DEVELOPMENT.md` is the developer guide (layout, tool setup, build / package commands, status); the root `README.md` is for users and must not grow developer instructions.
- `suite-integration.md` is the **single source of truth** for the contract between the programs and the Master Pro hub (folders, registry keys, `product.json`, silent switches, colours).
  Change the contract here first, then in the installers (`.iss`), the hub (`Hub.cpp`) and the installer test (`test-installer.ps1`) - all in the same change.
- `spec/` holds the owner's master specification (`DriveVault-Video-Editor-Pro-Claude-Code.md`). It is read-only for agents: follow it, never edit it. If it conflicts with reality, report the conflict.
- Program-specific documents (user guides, release notes, architecture, roadmap, third-party notices) live in each app's `docs/` folder, not here.
- Write facts that can be verified; mark anything untested as untested. No marketing claims.
