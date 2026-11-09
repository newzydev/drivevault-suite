#pragma once

#include <QMutex>

namespace dp::pdf {

// PDFium is not thread safe: every call into it (rendering on worker threads, page sizes on the UI thread ...) holds this one mutex.
QMutex& pdfiumMutex();

// Initialises the library on first use and shuts it down in shutdown() (call it after the last document is closed, before exit).
void ensurePdfiumInitialized();
void shutdownPdfium();

} // namespace dp::pdf
