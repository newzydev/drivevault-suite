# DriveVault Master Pro

The hub of the DriveVault suite (think "Creative Cloud" for DriveVault programs). It finds the installed DriveVault programs, opens them, shows their version,
installs / updates them from setup files and uninstalls them. Every program has its own accent colour (Video Editor Pro violet, Image Editor blue, Audio Editor
emerald; Master Pro amber). Native Windows program: C++20, Qt 6.8 / Qt Quick, CMake. Languages: ไทย / English.

```
C:\Program Files\DriveVault                                  <- DriveVault Master Pro (this program, DriveVaultMasterPro.exe)
C:\Program Files\DriveVault\Common                           <- every other program lives below this folder (keeps the suite folder tidy)
C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro   <- the Video Editor
C:\Program Files\DriveVault\Common\DriveVault <next program>     <- future programs
```

## It is installed automatically

Every program installer carries `DriveVault-Master-Pro-Setup-x64.exe` inside and runs it silently first when Master Pro is missing or older (same scope - all users
or current user - and language). Master Pro has its own uninstaller; while any DriveVault program is installed it **refuses** to be uninstalled (also silently). When
the last program is gone it can be uninstalled and removes the suite folder if it is empty. The contract with the programs (folders, `product.json`, registry
`Software\DriveVault\Products\<key>` incl. the `Uninstaller` value, silent switches, colours) is documented in
`docs\suite-integration.md` at the repository root.

## What it does (and honestly does not)

* Reads `Software\DriveVault\Products` in HKLM and HKCU (refreshes every 5 s, so installs from elsewhere show up), validates that the program file still exists
  ("Needs repair" when it does not) and also lists programs it does not know yet.
* **Open**, **Folder**, **Uninstall** (starts the program's own uninstaller; projects are never deleted), **Install / Update** from setup files found in
  `<Master>\installers`, `%LOCALAPPDATA%\DriveVault\MasterPro\installers` and folders you add, or a file you choose. Only setup files whose version resource says
  *CompanyName = DriveVault* are started; **signatures are not verified** (the installers are not signed yet). A newer setup file than the installed version
  shows "Update available".
* Image Editor and Audio Editor are shown as **Coming soon** - they do not exist yet. Programs are installed from setup files on this PC. Since 1.1.0 the **Check for updates** button asks the GitHub Releases of `newzydev/drivevault-suite` which versions exist (only when you press it - once when the program opens too, unless switched off) and offers **Download and update / install**: the setup file comes from the official release address only, its SHA-256 is compared with the release's `.sha256` file, its version resource must be DriveVault + the right program + the released version, and then it runs like any other setup file (and is deleted after a successful run). There is no online store and no automatic update.

## AI pages (since version 1.8.0)

The tabs **AI Q&A** and **Agentic AI** use a language model that runs **on your own PC** (llama.cpp with a GGUF model from Hugging Face). There is no API key, no account and no other AI. Nothing is sent anywhere unless you turn the web on and the AI searches (see below).

* **The model** - the installer downloads Qwen3.5-4B for you (about 2.7 GB; clear that task to skip it). In the AI settings you can download others from the catalogue (Qwen3.5 0.8B / 2B / 4B / 9B and the Thai-focused Typhoon 2.5, each only when you press Download, checked against a fixed size and SHA-256) or paste a `huggingface.co` link to a `.gguf` file (Check asks Hugging Face for its size and checksum; nothing is downloaded until you press Download). The engine (llama.cpp, Vulkan build) is part of the Master Pro installer (`ai\llama`).

* **In the chat** - under the message box you choose the model and the **effort**: *Quick* (no reasoning, short answers), *Normal*, or *Deep* (the model reasons before it answers, about 3 times slower). On the Agentic AI page the AI works in a folder you choose and you pick its **mode** (Shift+Tab switches it): *Read only*, *Plan* (it looks and writes a plan; nothing changes or runs until you approve the plan), *Ask first* (the default: every file change - shown as a diff - and every command is shown to you and happens only if you allow it) or *Accept edits* (it changes files without asking; commands still ask; a changed file is first saved in `.drivevault-backup`). It can list, find, read, search and edit files, keeps a **task list** for bigger jobs, reads the `CLAUDE.md` of the folder, and can run a command (`cmd.exe`, shown to you first, every time; it runs with your permissions and is not limited to the folder). It never deletes files by itself. Commands: `/help`, `/clear`, `/mode`, `/effort`, `/web`, `/todos`, `/init`; Esc stops the work. With the **Web** switch on (the default) it can search the web (DuckDuckGo) and read a page: only the search words and the page address go out, never your files or the conversation.

Conversations are saved in `%LOCALAPPDATA%\DriveVault\MasterPro\ai`. Known limits: the web search uses DuckDuckGo's HTML page, which may change; the answers are only as good as the model you choose (a 4B model on a laptop is a small model); the Thai-focused Typhoon model has not been tested here.

## Build, run, test

```powershell
.\scripts\build.ps1 -Test          # configure + build Release + tst_hub + tst_window (uses the suite's portable tools in <suite>\.tools)
.\build\release\bin\DriveVaultMasterPro.exe
.\scripts\package.ps1              # build + tests + windeployqt + app-local VC runtime + clean-PC import check + Inno Setup -> dist\DriveVault-Master-Pro-Setup-x64.exe
```

Diagnostics: `--list-products <file.json>` writes what the hub sees; `--screenshot <png> [--size WxH] [--lang th|en]`; `--registry-root <key>` and `--data-dir <dir>` for tests.

Tests: `tst_hub` (version resources, registry scan incl. broken / unknown programs, setup discovery, update detection, running a setup / uninstaller with exit code and
silent switches through a test double, only-DriveVault files are started, persistence, document lookup) and `tst_window` (the real window offscreen against a fake
registry: every program shows its own colour; Thai differs from English). The installer behaviour of Master Pro (install by the program installer, registry, Start Menu,
refusal while a program is installed, uninstall, empty-folder clean-up) is tested by `..\DriveVault Video Editor Pro\scripts\test-installer.ps1` (68 checks).

## Status

VERSION 1.21.0 (a Browser tab with Chrome-like permissions, and the AI can open, read and look at its own web pages; 1.20.0: the AI checks its own work once before it finishes; 1.19.3: the notes about a cut answer are hidden from the step list; 1.19.2: files are written in small parts, one Allow per file; 1.19.1: a long job no longer flickers, scrolls by itself or loses the model server; a "Background tasks" part in the right panel for the commands the AI runs - also commands that run for ever - with a stop button; the answer is read as it is written: a live line shows what the AI is writing, and a file that is too long for one answer is written in parts with append_file instead of failing; the conversation is compacted like Claude Code: the model writes a summary - /compact, a button, or by itself when the history is long; the agent page groups its conversations by folder with a "+" per folder, and its right panel shows the environment with the git branch, the context used, the changed files and the connectors; the agent page shows a list of what the AI does with a tick for every finished step, and its plan as a checklist; updates close the program, show the installer and open the program again; an agent that works until the job is done, with a clock and animations; a Claude Code style agent: edit_file with a diff, task list, plan approval, commands that always ask, slash commands, CLAUDE.md of the folder; modes of the agent: read only / plan / ask first / accept edits; the AI pages use only the model on this PC, with a model menu and an effort level in the chat; the installer downloads a model and carries the llama.cpp engine; the local AI can search the web and works in a folder; tabs at the top; "Check for updates" in the search bar). Not code-signed (`package.ps1 -Sign` uses the shared signing script). Not tested on a separate clean PC / VM,
and the all-users path under `C:\Program Files` needs administrator rights and was not run by the automated tests (the per-user path was). `LICENSE.txt` and the installer licence pages are the drafted EULA generated from `shared/legal`
(not yet reviewed by a lawyer).
