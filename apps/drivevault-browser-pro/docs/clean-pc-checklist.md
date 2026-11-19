# Browser Pro 0.6.2 clean Windows acceptance

Owner-run on disposable Windows 10 or 11 x64 VM snapshots with no DriveVault or Qt present. Not done by an agent: the installer was built, never installed. Use test pages and your own videos.

1. Compare the setup SHA-256 with the `.sha256` file. Record Windows version, installer hash and date.
2. Fresh snapshot, **Just for me**: Browser Pro and Master Pro install, the cyan card appears in Master Pro as installed and opens the browser; the guide opens from the Start menu.
3. Open an https site (page loads, so the `tls` plug-in is present), a site with a video, and a private window. Check the shield panel shows the four lists with the snapshot date and rule counts without pressing Update.
4. Disconnect the network: pages fail normally, the browser stays usable; press **Update filter lists** offline: it reports a failure and keeps the snapshot.
5. Save a plain video file and an HLS stream (FFmpeg is in `ffmpeg\`): both end as files in the downloads folder, no `.part` left. A YouTube address is not offered; try a DRM stream: refused.
6. Picture in picture on a page with a video; close the main window while it is open (no crash).
7. Master Pro refuses removal while Browser Pro is installed. Uninstall Browser Pro: `%LOCALAPPDATA%\DriveVault\BrowserPro` and the saved videos remain. Then uninstall Master Pro.
8. Restore the VM and repeat as **All users** (HKLM, Program Files, launch from a second account). Test an upgrade over an older Master Pro.

Record the outcome of each item before approving a Release.
