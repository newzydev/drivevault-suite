# DriveVault PDF Pro

Native Windows PDF program of the DriveVault suite (C++20, Qt 6.8 / Qt Quick, PDFium, qpdf). Works on this computer only - nothing is uploaded.
Accent colour red `#EF4444`. The full plan and the phases are in [`docs/PDF-Pro-Plan.md`](docs/PDF-Pro-Plan.md).

## Status: version 0.6.0 - in development, NOT released (Phases 1-4 done)

| Works now (tested) | Not built yet (planned, see the plan) |
|---|---|
| **Organize pages** (button "Pages"): thumbnails, select, **drag to reorder**, delete, duplicate, turn 90 degrees, **crop margins**, insert blank pages, add pages of other files, undo / redo; saves a **new** file (nothing is changed in place, an existing file is replaced only after a question) | Fonts: subsetting / merging (not done, not planned soon) |
| **Edit** (button "Edit"): put **text** (Thai and English), **pictures**, rectangles, ellipses, lines, arrows, **pen**, highlight / underline / strike-out, **stamps**, **sticky notes** and **polygons** on any page; select, move, resize, turn, front / back, colour, fill, line width, opacity, font, size, bold / italic, alignment; a watermark, page numbers, header and footer on every page; undo / redo; saved as a **new file** (the things become part of the pages) |
| **Fill in forms** (button "Form"): lists the fields of an AcroForm (text, tick boxes, lists, radio groups), fill them in and save a **new copy** that every viewer shows; the reader now also draws form fields and their values |
| **Autosave**: unsaved edits are written to the data folder a moment after each change and offered again (restore or delete) when the file is opened after a crash or an unsaved close |
| **Compress** (button "Compress"): levels Safe / Balanced / Smallest / custom (picture dpi, JPEG quality, remove metadata and thumbnails, fast web view); a real before / after (sizes, pictures changed) and a page side by side **before saving**; a file that would not get smaller is not written |
| **Merge files** (Home screen or "Add files" in the organizer; protected files ask for their password; delete the pages you do not want after adding) | Add text / images / shapes, comments, forms (Phase 4) |
| **Split** (button "Split"): by page ranges, every N pages, by top level bookmarks, or by a maximum file size; live list of the files that will be written; a failed or cancelled split leaves no files | Export to images / ZIP, protect / unlock (Phase 5) |
| Open a PDF (button, Ctrl+O, drag and drop, recent files, command line), several files in **tabs** | Redaction (Phase 6) |
| Continuous page view or **two pages per row**, thumbnails, page field, prev / next, Page Up / Down, Home / End | |
| Zoom (steps, Ctrl+wheel, 100 %, fit width, fit page), **rotate the view**, **dark pages** (view only), full screen (F11) | |
| **Find** in the whole file (match case, whole word, highlights, next / previous, Ctrl+F, F3) | |
| **Select text with the mouse, copy** (Ctrl+C, right click menu, select all) - across pages | |
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
src/pdftools/    the only code that calls qpdf: Overlay (paint the things the user adds and burn them into pages), Forms (read and fill AcroForm fields), Compress (smaller copy of a file), PdfTools (write a new file from a list of pages: merge, reorder, rotate, crop, blank pages, split by size), SplitPlan (split plans, file names)
src/app/         DocumentController (tabs, split), PageListModel (the organizer list with undo), Compressor (compress dialog backend), EditorModel + OverlayImageProvider (the edit bar), FormFiller (form dialog backend), PageImageProvider (image://pdf/...), PrintTask, PageRange, main.cpp
src/ui/          QML module DriveVault.PdfPro.UI (Main, screens/, components/)
tests/           tst_pdfengine, tst_pdftools, tst_compress, tst_editor, tst_forms, tst_organizer, tst_controller, tst_ui_flow (real clicks and keys, fails on QML warnings), tst_perf
resources/       fonts (OFL), icons (made by scripts\make-icon.ps1)
```

## Known limits (honest list)
- Edit: the things you add are **burned into the page contents** when you save (the result cannot be edited again as objects; keep the original). Not done: keeping them as editable annotations. Unsaved edits are autosaved to `recovery` in the data folder (a picture that was added is stored by path: if the picture file is gone, saving says so). The program also asks before a file with unsaved edits is closed. Editing is off while the view is rotated. Edits belong to the open tab and are lost when it is closed after the question.
- Added text is drawn as **outlines** of the letters (shaped by Qt, so Thai vowels and tone marks stack correctly and it looks the same everywhere); the text is also written as an invisible text layer, so search and copy give back the exact Unicode text, but the selection boxes of added text are approximate. Fonts whose licence forbids embedding are refused. The Thai line breaking of long text without spaces is by Qt (it may break inside a word).
- A saved copy of a protected file stays protected with the same password.
- Forms: only classic AcroForm fields; an XFA (dynamic) form is detected but not filled. Text fields take Latin-1 characters (the standard form fonts have no Thai); a value that cannot be shown is refused and listed, not written as garbage. Fields are filled in a list, not by clicking on the page. Signature fields and buttons are not supported. Checked on a generated form only: forms from Word, Acrobat or other tools were not available.
- Compress touches only embedded images: 8 bit RGB / grey (also ICC based) pictures stored as JPEG or Flate, at least 32 x 32 pixels and 20 KB. Not touched: black/white scans (CCITT / JBIG2), JPEG 2000, CMYK, indexed colours, pictures with a stencil mask or a Decode array, and anything in a file that cannot be parsed. Pictures that look like drawings (few colours) are never turned into JPEG; they are only made smaller when shown at a lower resolution than they have. Fonts are not touched. The resolution is measured from how the picture is placed on the page (including inside form XObjects); a picture whose use cannot be found is not reduced.
- Compress was measured on generated files only (a synthetic photo: 1.2 MB -> 20 KB at 150 dpi, JPEG 70; a real photo or scan will shrink much less). Real scans, big catalogues and files from Office were not available to test. The JPEG writer is the one in Qt (`imageformatsqjpeg.dll` must be shipped with the installer in Phase 8).
- A protected file stays protected after compress (same password), unlike merge / split which write an unprotected file.
- Organize / merge / split write a new file from the pages: **bookmarks and form fields are not copied** (the dialog says so after saving), links to pages that are not in the new file are removed, a protected source gives an **unprotected** result (protect / unlock is Phase 5). Comments and markup on the pages are kept.
- Crop makes the visible page smaller; the content outside the box stays inside the file (it is not removed - use redaction, Phase 6, for that).
- There is no page range box when adding a file: add it, then delete the pages you do not want. Thumbnails are drawn one by one (no limit tests with thousands of pages in the organizer yet).
- The merge / split dialogs were tested with generated files and by real mouse drags offscreen; files from other programs (scans, forms, Office exports) were not available.
- Files larger than 4 GB are refused (PDFium file access uses a 32 bit length on Windows).
- A page is rendered as one image (at most 64 million pixels, about 245 MB); there is no tiled rendering of only the visible part yet, so extreme zoom on a big page is heavy.
- Thai text in a PDF written by Qt (the test file) keeps every letter but not the exact place of the vowel / tone marks when extracted; real files from Word and other programs were not available to test. The writer for Phase 4 has to do better.
- The mouse cursor is an arrow or a hand over links; there is no text cursor over text. Double click does not select a word.
- Printing was tested to a PDF file; a real printer (and a per-page paper orientation change) was not tried.
- No code signing and no installer yet; the third-party notices (PDFium and its bundled libraries, qpdf, Inter, Noto Sans Thai) must be written before any release.
