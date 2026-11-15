# Connecting Claude Code or Codex (MCP)

DriveVault Video Editor Pro has a built-in **Model Context Protocol (MCP)** server. When you switch it on, an AI coding assistant on **this computer** (Claude Code, Codex, or any MCP client) can work on the project that is open in the editor.
It is **off** until you switch it on. Nothing is sent over the Internet: the server listens on `127.0.0.1` only and every request must carry your private token.

เซิร์ฟเวอร์ MCP ในตัวโปรแกรม ให้ผู้ช่วย AI บนเครื่องเดียวกัน (Claude Code, Codex หรือโปรแกรมอื่นที่รองรับ MCP) ทำงานกับโปรเจกต์ที่เปิดอยู่ได้ ปิดอยู่เป็นค่าเริ่มต้น รับเฉพาะการเชื่อมต่อจากเครื่องนี้ และต้องใช้โทเคนส่วนตัว

## Switch it on

Settings > AI > **Connect Claude Code / Codex (MCP)** > *Enable MCP server*. The page shows the address (default `http://127.0.0.1:38491/mcp`) and two copy buttons.

**Claude Code** - press *Copy command* and run it once in a terminal (it contains the token):

```
claude mcp add --transport http drivevault-video-editor http://127.0.0.1:38491/mcp --header "Authorization: Bearer <token>"
```

**Codex** - press *Copy config*, add it to `~/.codex/config.toml`, and put the token in the environment variable named in the last line:

```toml
[mcp_servers.drivevault_video_editor]
url = "http://127.0.0.1:38491/mcp"
bearer_token_env_var = "DRIVEVAULT_MCP_TOKEN"
```

*Create new token* makes the old token stop working at once. Switching the server off closes the port.

## What the assistant can do

Positions are **frame numbers** at the project frame rate (`get_project_info` returns it). Every edit is one undo step (`undo` / `redo` are tools too); markers are the one exception.

| Tool | What it does |
|---|---|
| `get_project_info`, `list_tracks`, `list_media`, `list_clips` | read the project (read only) |
| `import_media`, `add_clip`, `move_clip`, `trim_clip`, `split_at`, `delete_clips` | edit the timeline |
| `add_subtitle`, `add_marker`, `set_playhead` | subtitles, markers, playhead |
| `undo`, `redo`, `save_project` | history and saving |
| `create_project`, `open_project` | refuse to replace a project with unsaved changes unless `discard_unsaved` is true |
| `get_ai_status`, `detect_silence`, `apply_silence_cuts`, `transcribe`, `apply_captions` | the offline AI helpers (need *Enable AI features*); long jobs run in the background and the assistant polls `get_ai_status`; nothing is changed until the `apply_*` call |

Not available through MCP (yet): export / render, effects, keyframes, tracking, audio enhancement, titles other than subtitles. The server cannot run programs, delete files or reach the network.

## The AI cursor

While an assistant works, the editor shows a mouse pointer with a caption that moves to what is being edited and says what it is doing (for example "Cutting at frame 120"). Each edit waits a moment (about 0.7 s) so that the pointer arrives first; reading tools only change the caption.
It is only a display: the pointer does not click for the assistant, and the edit is made by the tool, not by the pointer. Switch it off in Settings > AI > *Show the AI cursor* (setting `mcpCursor`).

## Safety notes

- Anyone who has the token can edit the open project from this computer: treat it like a password. Do not paste it in public places.
- The assistant edits the live project. Save before you let it work, so that you can go back (Undo, or reopen the saved file).
- Browser pages cannot use the server (requests with an `Origin` header are refused).
