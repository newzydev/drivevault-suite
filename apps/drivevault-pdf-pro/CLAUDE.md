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
.\scripts\build.ps1 -Test        # configure + build Release + 16 ctest suites
..\..\scripts\run-pdf-pro.bat    # smoke start / look at the program (no install)
.\scripts\package.ps1            # release gates and installer (Master Pro is not inside: the installer downloads it from GitHub)
```

## Thai OCR
- `src/app/ThaiOcr.cpp` runs **OpenThai 2.0 OCR Text** (iApp, Apache-2.0; `det.onnx` = PP-OCRv6-tiny detector, `rec.onnx` = Thai CTC line recogniser, `rec_dict.txt`) with ONNX Runtime on the CPU, in this process. Models: `.tools\openthai-ocr` (revision 40e8e9ff, SHA-256 pinned in `scripts\package.ps1`; CMake copies them to `bin\models\openthai-ocr-*`). The OCR **Markdown** model of the same family is a 2.4 GB GPU model and is not used. Per page: detector map (long side 1280, thresholds from `det.yml`) -> components -> boxes that follow the line tilt (`cutLine`) -> `flattenCrop` -> recogniser -> `canonicalThai` + `fixThaiScripts` -> `keepOcrLine` (kept from confidence 0.5, not the model card's 0.8: a dropped line is lost for search). Not ported from the model's Python runtime: page turning (sideways / upside down), deskew above 2.5 degrees, gutter splitting, column reading order. Measured: on a real 8-page office document the same recall as the PaddleOCR models (97.4 % / 97.5 %) at 3x the speed (0.48 s a page); `tests/tst_ocrreal.cpp` repeats that on any PDF (`DV_TEST_REAL_PDF`, numbers only) and `tests/tst_ocr.cpp` measures the character error rate on generated scans (Noto Sans Thai and the Windows Thai fonts) and has regression floors: re-measure before changing a constant (`std::vector<T> v(size_t(x))` is a function declaration; `emit` is a Qt macro).

## AI assistant (version 1.19.0, floating since 1.20.0)
- Floating AI card at the bottom of the page area (since 1.20.0; `ui/screens/AiPanel.qml`, a child of the page background in `ViewerScreen.qml`: only the question bar while the conversation is empty, grows up to half of the page area, then scrolls; the "−" button makes it a small pill (`collapsed`) and "+" brings it back (1.21.0); the side panel opens by default and has no AI tab; QML singleton `Assistant` = `src/app/DocumentAssistant`): questions and summaries of the open document. Everything runs on this computer: `LlamaServer` starts `ai\llama\llama-server.exe` (llama.cpp b12170 **Vulkan** build, `-ngl 99`, CPU fallback `-ngl 0`; 127.0.0.1, random port, API key, killed with the program by a job object) with `Qwen3.5-4B-Q4_K_M.gguf`; `ChatStream` talks to its OpenAI style `/v1/chat/completions` (SSE, `enable_thinking=false`).
- Questions: the page texts are cut into pieces (`DocumentSearch`, BM25 over Thai 2/3-grams + Latin words), the best ones go to the model with their page numbers (`AssistantPrompts`), and the answer is shown with the pages it names (`citedPages`). A summary is map-reduce over the whole text. The assistant only reads; a document without text offers the OCR tool.
- The model is **not shipped** (2.7 GB, GitHub limit 2 GB): the installer task `aimodels` downloads it into `{app}\ai\models` (pinned revision + SHA-256, the same values as `ModelDownloader.cpp` - change both together; silent installs never download) and the panel has a Download button (`<data dir>\models`). The 23 engine files are pinned by SHA-256 in `scripts\package.ps1`.
- Tests: `tests/tst_assistant.cpp` runs everything against a fake server (retrieval, prompts, SSE, download with a loopback server); a real model is only used by hand (`LlamaServer` with `.tools\llm`).

## Printing (1.21.0)
- `ui/screens/PrintDialog.qml` (options on the left, a preview of the page on the paper at the right) -> `Doc.startPrint({printer, pages, currentPage, copies, color, scaleMode: fit | shrink | actual | custom, scalePercent, orientation: auto | portrait | landscape, duplex})` -> `PrintTask` (QPrinter, each page rendered by PDFium; "shrink" only shrinks pages that do not fit, "custom" is cut at the margins, `duplex` = `QPrinter::DuplexLongSide`, ignored for a PDF file). `Doc.openPrinterPreferences(printer)` starts the driver's own settings window (`rundll32 printui.dll,PrintUIEntry /e /n <printer>`, only for a printer in `QPrinterInfo::availablePrinterNames()`; the choices become the printer's default for the user and the next job picks them up - not tried with every driver; the preview paper is refreshed when the program is active again). `Doc.paperInfo(printer)` gives the paper for the preview (A4 when the printer says nothing); the preview uses the same rules as `PrintTask`, keep them equal. Measured only by printing to a PDF file (`tst_controller`) - a real printer and the both-sides setting were not tried.

## Gotchas
- `fpdfview.h` includes `windows.h`: `NOMINMAX` is defined on `dp_pdfengine` (PUBLIC), keep it.
- PDFium is not thread safe: every call holds `pdfiumMutex()` (inside `PdfDocument`). Never call `FPDF_*` from anywhere else.
- Do not name a QML type or singleton like a Qt Quick / Controls type (`TextMetrics`, `TabBar` are taken: the compiler then uses the Qt one); a Button already has `icon`, `text`, `font` ... as properties: the Thai line height singleton is `ThaiMetrics`.
- `ListView` / `Repeater` delegates are not QObject children of the window: tests find them by walking `QQuickItem::childItems()`.
