# The only place that calls qpdf (see src/CLAUDE.md). Redaction uses PdfDocument for independent verification.
add_library(dp_pdftools STATIC
    PdfTools.cpp PdfTools.h
    SplitPlan.cpp SplitPlan.h
    Compress.cpp Compress.h
    Overlay.cpp Overlay.h
    Forms.cpp Forms.h FormData.cpp FormData.h
    Signature.cpp Signature.h
    Security.cpp Security.h
    RedactionAudit.cpp RedactionAudit.h
    SelectiveRedaction.cpp SelectiveRedaction.h
    DocumentMaintenance.cpp DocumentMaintenance.h)
target_include_directories(dp_pdftools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dp_pdftools PUBLIC Qt6::Core Qt6::Gui qpdf dp_pdfengine crypt32)
