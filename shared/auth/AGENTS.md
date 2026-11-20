# shared/auth - the Google account of the suite (adds to the parent files)

One Google sign-in for **every** DriveVault program (Master Pro, Video Editor Pro, PDF Pro, later ones). Owner's request 2026-11-20: sign in with Google, show the picture and name in the title bar of every program, and a sign-in in one program is a sign-in in all of them. **The sign-in is optional** (owner's decision 2026-11-20, after being asked): every program works without it; nothing is locked.

## What it is - and is not
- A **local identity for display**: Google id (`sub`), name, e-mail, a 128 px picture. It is not a login to any DriveVault server (there is none) and nothing checks it later. **No token is kept** (the access token is used once, to read the profile).
- Stored in ONE file per Windows user: `%LOCALAPPDATA%\DriveVault\Account\account.bin`, encrypted with Windows DPAPI (this user only, extra entropy `DriveVault.Account.v1`). Every program watches that folder (`QFileSystemWatcher` + a 2 s look) so a sign-in or sign-out in one program shows in the others at once, running or started later. Signing out removes the file (all programs).
- The uninstallers never remove it (like the other user data).

## Pieces
```
src/GoogleAccount.*   QObject for QML (signedIn, name, email, picture, busy, error; signIn / cancelSignIn / signOut / clearError). The OAuth trip: system browser (never an embedded engine - Google refuses those), loopback listener on 127.0.0.1 (state + PKCE S256, one callback, 3 min limit), token exchange, userinfo (email_verified required), picture only from *.googleusercontent.com without credentials.
src/OAuthConfig.*     decrypts the embedded Desktop OAuth client (AES-256-CBC, BCrypt) from the generated header.
qml/DvAccountChip.qml the title-bar button / avatar + name / card with "Sign out" - the SAME file in every program; colours and texts are properties the program fills from its own Theme and Tr. Never name a property `onXxx` (QML takes it for a signal).
tests/tst_account.cpp reading the file, odd / damaged files, two instances following each other (sign-in and sign-out), the embedded-pair decryption. Registered by `dv_account_add_tests()` in each program's tests.
```
`shared/scripts/make-oauth-header.ps1` writes `<build>/generated/dv_account/GoogleDesktopOAuth.h` at **every configure** (CMake `execute_process` in `CMakeLists.txt` here). The Google client id + secret are **not in the repository**: they come from `$env:DV_GOOGLE_OAUTH_FILE`, else a `client_secret_*.json` ("installed" = Desktop app; it is preferred) or `GOOGLE_LOGIN_CLIENT_ID.txt` in the folder ABOVE the repository - the txt of the owner also holds the pair of an older NON-Desktop client (using it gave `redirect_uri_mismatch` on 2026-11-20). Without a file the header is empty and the program builds, but its sign-in says "not available". **Hiding the pair in the exe (key next to the ciphertext) is obfuscation, not secrecy** - Google says the secret of a Desktop client cannot be kept secret; never claim more. Never commit the credential files, the header, or print the secret in a log / chat.

## How a program uses it
```cmake
add_subdirectory("${CMAKE_SOURCE_DIR}/../../shared/auth" "${CMAKE_BINARY_DIR}/dv_account")   # before the QML module's directory
dv_account_qml(DV_ACCOUNT_QML)        # in the directory of the qt_add_qml_module; add ${DV_ACCOUNT_QML} to its QML_FILES
target_link_libraries(<exe> PRIVATE dv_account)
dv_account_add_tests()                # after enable_testing()
```
In C++: one `dv::account::GoogleAccount` per process, registered as a QML singleton `Account`; in the title bar `DvAccountChip { account: Account ... }` (see the three programs). Texts (Thai + English) go in the program's own table.

## Rules
- **Tests and smoke starts must never touch the real account**: `DV_ACCOUNT_DIR=<dir>` replaces the folder and then the file is plain JSON `account.json` (so a test can write `{"sub":"1","name":"Test","email":"a@b.c"}`). Every program's test properties set it; set it yourself for a hand started exe (`$env:DV_ACCOUNT_DIR = "$env:TEMP\acct-test"`). Never type a real password or sign in with the owner's account in a test.
- The real sign-in opens the system browser and talks to Google: do it only when the owner asks. The OAuth consent screen of the Google project may be in "Testing" (only listed test users can sign in) - that is a Google console setting, not code.
- Do not add server-side checks, tokens, refresh logic or a "login required" wall without a new owner decision. Do not store more than the four fields.
- A change here changes **every** program: build and run the UI tests of each (`scripts\build.ps1` of each app, `tst_account`, the UI test) - not `build-all.ps1` (owner only).

## Per-account data (owner's request 2026-11-20: history follows the signed-in account)
Master Pro: chats of both AI pages. PDF Pro: recent files. Video Editor Pro: recent projects. Each program asks `GoogleAccount::scopeKey()` ("" = nobody signed in) and listens to `changed()`:
- Nobody signed in = the shared data exactly where it always was (nothing moved, nothing renamed). Signed in = a copy below `accounts/<key>` (Master Pro `<data>\ai\accounts\<key>\conversations-*.json`, Video Editor `<data>\accounts\<key>\recent-projects.json`, PDF Pro the ini section `accounts/<key>`).
- The first time an account is used it starts from a COPY of the shared data (the shared data is never changed by that); another account starts from the same copy, never from a previous account's data. Switching back restores the account's own data. Never put the Google id, name or e-mail in a path (the key is a hash).
- Settings (model, effort, language, theme, window size) are NOT per account.
- Master Pro holds the switch back while an AI job runs (`AiService::applyHistoryScope`, retried on `busyChanged`).
- A new per-account store needs a test like `recentProjectsFollowTheAccount` / `recentFilesFollowTheAccount` / `theChatHistoryFollowsTheAccount` (separate lists, switching back, copy on first use, restart).
