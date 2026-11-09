# DriveVault PDF Pro

Native Windows PDF program of the DriveVault suite (C++20, Qt 6.8 / Qt Quick, PDFium, qpdf). Works on this computer only - nothing is uploaded.
Accent colour red `#EF4444`. The full plan and the phases are in [`docs/PDF-Pro-Plan.md`](docs/PDF-Pro-Plan.md).

## Status: version 0.1.0 - in development, NOT released (Phase 1, reader only)

| Works now (tested) | Not built yet (planned, see the plan) |
|---|---|
| Open a PDF (button, Ctrl+O, drag and drop, recent files, command line) | Text search, select / copy text, outline (bookmarks) |
| Continuous page view, thumbnails, page field and prev / next, Page Up / Down, Home / End | Two page view, tabs, printing |
| Zoom (steps, Ctrl+wheel, 100 %, fit width, fit page) | Merge / split / organize pages (Phase 2) |
| Files with an open password (asks for it, a wrong password is refused) | Compress (Phase 3), add text / images / shapes (Phase 4) |
| Thai / English UI with the same sizes and spacing | Export to images / ZIP, protect / unlock (Phase 5), redaction (Phase 6) |
| Big files: pages are rendered on demand on a thread pool, one image per visible page | Installer, Master Pro integration, release |

Master Pro still shows "PDF Pro - coming soon": it changes only when a usable installer exists.

## Build and run

Prerequisites: the suite's `.tools` folder with Qt 6.8.3, CMake, Ninja, **PDFium** (`.tools\pdfium`, BSD-3, build chromium/8138 from bblanchon/pdfium-binaries)
and, for the password tests, **qpdf** (`.tools\qpdf`, Apache-2.0, 12.4.2 msvc64). See `docs/DEVELOPMENT.md` in the repository root.

```powershell
.\scripts\build.ps1 -Test              # configure + build Release + ctest (3 suites)
..\..\scripts\run-pdf-pro.bat          # open the built program without installing it
```

Executable options: `file.pdf` (open it), `--password text`, `--lang th|en`, `--data-dir dir`, `--size WxH --screenshot out.png` (diagnostics: writes a picture of the window and exits).

## Layout
```
src/core/        Version.h.in (generated version header)
src/pdfengine/   the only code that calls PDFium: PdfDocument (open, page sizes, render, text), PdfiumRuntime (the one lock + init)
src/app/         DocumentController (state for QML), PageImageProvider (image://pdf/...), main.cpp
src/ui/          QML module DriveVault.PdfPro.UI (Main, screens/, components/)
tests/           tst_pdfengine (pixels, text, encryption), tst_controller, tst_ui_flow (real clicks, fails on QML warnings)
resources/       fonts (OFL), icons (made by scripts\make-icon.ps1)
```

## Known limits (honest list)
- Files larger than 4 GB are refused (PDFium file access uses a 32 bit length on Windows).
- No code signing and no installer yet; the third-party notices (PDFium and its bundled libraries, qpdf, Inter, Noto Sans Thai) must be written before any release.
- Very large zoom levels are clamped to a safe image size; the performance targets of Phase 1 (a 500 page file) are not measured yet.
