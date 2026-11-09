# DriveVault PDF Pro

Native Windows PDF program of the DriveVault suite (C++20, Qt 6.8 / Qt Quick, PDFium, qpdf). Works on this computer only - nothing is uploaded.
Accent colour red `#EF4444`. The full plan and the phases are in [`docs/PDF-Pro-Plan.md`](docs/PDF-Pro-Plan.md).

## Status: version 0.2.0 - in development, NOT released (Phase 1 done: the reader)

| Works now (tested) | Not built yet (planned, see the plan) |
|---|---|
| Open a PDF (button, Ctrl+O, drag and drop, recent files, command line), several files in **tabs** | Merge / split / organize pages (Phase 2) |
| Continuous page view or **two pages per row**, thumbnails, page field, prev / next, Page Up / Down, Home / End | Compress (Phase 3) |
| Zoom (steps, Ctrl+wheel, 100 %, fit width, fit page), **rotate the view**, **dark pages** (view only), full screen (F11) | Add text / images / shapes, comments, forms (Phase 4) |
| **Find** in the whole file (match case, whole word, highlights, next / previous, Ctrl+F, F3) | Export to images / ZIP, protect / unlock (Phase 5) |
| **Select text with the mouse, copy** (Ctrl+C, right click menu, select all) - across pages | Redaction (Phase 6) |
| **Bookmarks** (outline) and a **comments / markup list**; links in the file (to a page, or to a web address after a question) | Installer, Master Pro integration, release |
| **Printing** (printer, page range, copies, colour, fit to paper) | Reading order / accessibility tools, text-to-speech |
| Files with an open password (asks for it, a wrong password is refused) | OCR, digital signatures (not in the plan) |
| The page, zoom and view mode of each file are remembered | |
| Thai / English UI with the same sizes and spacing | |

Master Pro still shows "PDF Pro - coming soon": it changes only when a usable installer exists.

## Measured (synthetic text files made by the tests, this PC; `tests\tst_perf.cpp` prints the numbers as `PERF ...`)

| What | Result |
|---|---|
| open a 500 page file / first page visible | 13 ms / 3 ms (134 KB file, +7 MB memory) |
| open a 3000 page file / render its last page | 34 ms / 1 ms |
| search a whole 500 page file | 8 ms (500 hits) |
| a 5000 x 7000 pt poster page at 800 % | clamped to 64 million pixels, +about 250 MB while it is shown |

These files contain only a line of text per page. Real files with big scans or many images are slower; they have **not** been measured yet.

## Build and run

Prerequisites: the suite's `.tools` folder with Qt 6.8.3, CMake, Ninja, **PDFium** (`.tools\pdfium`, BSD-3, build chromium/8138 from bblanchon/pdfium-binaries)
and, for the password tests, **qpdf** (`.tools\qpdf`, Apache-2.0, 12.4.2 msvc64). See `docs/DEVELOPMENT.md` in the repository root.

```powershell
.\scripts\build.ps1 -Test              # configure + build Release + ctest (4 suites)
..\..\scripts\run-pdf-pro.bat          # open the built program without installing it
```

Executable options: `file.pdf` (open it), `--password text`, `--lang th|en`, `--data-dir dir`, `--size WxH --screenshot out.png` (diagnostics: writes a picture of the window and exits).

## Layout
```
src/core/        Version.h.in (generated version header)
src/pdfengine/   the only code that calls PDFium: PdfDocument (open, render, text, search, selection helpers, outline, links, annotations), PdfiumRuntime (the one lock + init)
src/app/         DocumentController (tabs and state for QML), PageImageProvider (image://pdf/...), PrintTask, PageRange, main.cpp
src/ui/          QML module DriveVault.PdfPro.UI (Main, screens/, components/)
tests/           tst_pdfengine, tst_controller, tst_ui_flow (real clicks and keys, fails on QML warnings), tst_perf
resources/       fonts (OFL), icons (made by scripts\make-icon.ps1)
```

## Known limits (honest list)
- Files larger than 4 GB are refused (PDFium file access uses a 32 bit length on Windows).
- A page is rendered as one image (at most 64 million pixels, about 245 MB); there is no tiled rendering of only the visible part yet, so extreme zoom on a big page is heavy.
- Thai text in a PDF written by Qt (the test file) keeps every letter but not the exact place of the vowel / tone marks when extracted; real files from Word and other programs were not available to test. The writer for Phase 4 has to do better.
- The mouse cursor is an arrow or a hand over links; there is no text cursor over text. Double click does not select a word.
- Printing was tested to a PDF file; a real printer (and a per-page paper orientation change) was not tried.
- No code signing and no installer yet; the third-party notices (PDFium and its bundled libraries, qpdf, Inter, Noto Sans Thai) must be written before any release.
