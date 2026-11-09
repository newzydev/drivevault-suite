#include "PdfiumRuntime.h"

#include <fpdfview.h>

#include <QMutexLocker>

namespace dp::pdf {

namespace {
bool g_initialized = false;
}

QMutex& pdfiumMutex()
{
    static QMutex mutex;
    return mutex;
}

void ensurePdfiumInitialized()
{
    QMutexLocker lock(&pdfiumMutex());
    if (g_initialized)
        return;
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
    g_initialized = true;
}

void shutdownPdfium()
{
    QMutexLocker lock(&pdfiumMutex());
    if (!g_initialized)
        return;
    FPDF_DestroyLibrary();
    g_initialized = false;
}

} // namespace dp::pdf
