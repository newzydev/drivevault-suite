# DriveVault PDF Pro - rules (adds to the repository root file)

Native Windows PDF program: C++20, Qt 6.8 / Qt Quick (QML), PDFium (reading / rendering), qpdf (file structure and encryption), ONNX Runtime (Thai OCR), CMake. Version 1.2.0 has a GitHub Release.
The plan with all phases and the owner's decisions is `docs/PDF-Pro-Plan.md` - read it first; do not build features that are not in the plan and do not label a planned feature as available.

## Layout
```
src/pdfengine/  the only code that touches PDFium (see src/CLAUDE.md)     src/app/  DocumentController, PageListModel, PageImageProvider, main.cpp
src/pdftools/   the only code that touches qpdf (write a new file from pages)
src/ui/         QML module DriveVault.PdfPro.UI                           tests/    Qt Test suites (see tests/CLAUDE.md)
resources/      fonts, icons                                              scripts/  build.ps1, make-icon.ps1
```
Output: `build/release/bin/DriveVaultPdfPro.exe` (+ `pdfium.dll` copied next to it, + test exes). Git-ignored.

## Non-negotiables
- No fake features: anything not working is not shown or is labelled. No Electron / Tauri / WebView. Heavy work never runs on the UI thread. Files are never modified in place (save as a new file / atomic replace).
- Offline only; nothing downloads unless the user presses a button. Passwords are never logged, stored or written to settings.
- Redaction (Phase 6) must really remove the content and put a black box in its place, and the result must be verified by extracting the text again (plan section 3.7). Unlocking needs the correct password; there is no password guessing.
- Licences: PDFium (BSD-3), qpdf (Apache-2.0). Never add AGPL / GPL libraries (MuPDF, Poppler, Ghostscript, iText).

## Commands (from this folder)
```powershell
.\scripts\build.ps1 -Test        # configure + build Release + 14 ctest suites
..\..\scripts\run-pdf-pro.bat    # smoke start / look at the program (no install)
.\scripts\package.ps1            # release gates and installer, after building Master Pro's setup
```

## Gotchas
- `fpdfview.h` includes `windows.h`: `NOMINMAX` is defined on `dp_pdfengine` (PUBLIC), keep it.
- PDFium is not thread safe: every call holds `pdfiumMutex()` (inside `PdfDocument`). Never call `FPDF_*` from anywhere else.
- Do not name a QML type or singleton like a Qt Quick / Controls type (`TextMetrics`, `TabBar` are taken: the compiler then uses the Qt one); a Button already has `icon`, `text`, `font` ... as properties: the Thai line height singleton is `ThaiMetrics`.
- `ListView` / `Repeater` delegates are not QObject children of the window: tests find them by walking `QQuickItem::childItems()`.
