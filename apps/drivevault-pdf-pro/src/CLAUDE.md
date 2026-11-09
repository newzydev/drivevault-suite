# drivevault-pdf-pro/src - rules (adds to the parent files)

- `pdfengine` (library `dp_pdfengine`) is the only place that includes PDFium headers or calls `FPDF_*`. Everything above works with `PdfDocument`, `QImage`, `QString`. New engine features (search, outline, merge ...) are added there with a test in `tests/tst_pdfengine.cpp`.
- `PdfDocument` is thread safe because every method locks `pdfiumMutex()`; do not hold that lock while calling back into Qt code that may re-enter it. Pages are 0-based in C++ and QML.
- `DocumentController` is the QML singleton `Doc` (module `DriveVault.PdfPro.Backend`, registered by `main.cpp`); keep its values simple (strings, ints, lists) and return error codes as strings (`password`, `format`, `file`, `tooLarge`, `unknown`) - the texts live in `Tr.qml`.
- The image provider URL is `image://pdf/<docId>/<page>/<pixels per point * 1000>`; `docId` changes with every open so an old request can never draw the wrong document.
- Settings live in `<dataDir>\settings.ini` (language, recent files); `--data-dir` overrides the folder (tests use it). No other persistence.
- Every new `.qml` file must be listed in `src/ui/CMakeLists.txt`; singletons also in `set_source_files_properties(... QT_QML_SINGLETON_TYPE TRUE)`.
- UI texts: `Tr.s("key")` with both languages in `Tr.qml` (no `qsTr`); always `AppText` (not `Text`) so Thai and English have the same line height; keep `objectName`s stable (`tst_ui_flow` clicks them: `homeOpen`, `toolbarOpen`, `pageField`, `nextPage`, `prevPage`, `zoomIn`, `zoomOut`, `zoomLabel`, `fitWidth`, `fitPage`, `actualSize`, `toggleThumbs`, `closeFile`, `langTh`, `langEn`, `passwordField`, `noticeBanner`, `recent_<n>`, `pageImage_<n>`, `pages`, `thumbnails`).
- `tst_ui_flow` fails on any QML warning: run it after UI changes and fix every warning.
