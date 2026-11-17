# DriveVault Remote Desktop Pro - rules (adds to the repository root file)

Native Windows program (C++20, Qt 6.8 / Qt Quick, CMake) to see and control another Windows PC, like AnyDesk / TeamViewer. **Version 0.1.0 = phase 0 skeleton: it connects to nothing.**
The plan with every phase, the security rules and the owner's decisions is `docs/Remote-Desktop-Pro-Plan.md` - read it first; do not build what is not in the plan and do not label a planned feature as available.
Master Pro lists the program as "Coming soon" (catalog key `RemoteDesktopPro`, accent `#06B6D4`) until a release exists.

## Layout
```
src/net/     Stun (the STUN Binding request / answer parser, tested)           src/main.cpp, qml/Main.qml   the empty window that says "Not implemented yet"
tools/natcheck/  rd_natcheck: asks public STUN servers what the NAT does to one UDP port (phase 0 measurement)
tests/       tst_stun (Qt Test, no network)     resources/product.json     scripts/build.ps1
```
Later phases add `src/capture`, `src/codec`, `src/input`, `src/session`, `service/` (see the plan, section 3).

## Non-negotiables
- No fake features. Nothing is captured, opened or sent until the user turns it on; the program makes no network request at start-up. Status labels match reality.
- Never hidden: whoever is controlled sees a permanent banner and has a disconnect button (plan 3.3). No "silent" or "invisible" mode, ever.
- Passwords / passcodes are never logged or stored as text (a permanent password is kept only as a salted slow hash or a PAKE verifier). The content of a session (pictures, keys, clipboard) is never written to disk or logged.
- Do not invent a protocol or a cipher: use a standard, reviewed library / construction (plan 3.3). The directory / relay server is in another repository; do not edit it without the owner's yes in that session (plan section 7). Never write the server's address or other private data of the owner into files that are committed.
- Licences: only MIT / BSD / Apache-2.0 / LGPL-through-DLL. No GPL / AGPL (x264 is GPL: not allowed). The release gates of `package.ps1` apply once there is one.
- Tests and measurements are silent (`DV_NO_AUDIO=1`) and never control the real mouse / keyboard of the owner. Network tests use loopback; a test that needs the real Internet is opt-in and says so.

## Commands (from this folder)
```powershell
.\scripts\build.ps1 -Test        # configure + build Release + ctest (tst_stun)
build\release\bin\rd_natcheck.exe   # phase 0: what does this network's NAT do to a UDP port (sends 1 STUN request to each public server)
```
