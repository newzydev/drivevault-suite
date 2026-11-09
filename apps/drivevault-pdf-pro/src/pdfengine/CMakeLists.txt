# The only place that talks to PDFium (see src/CLAUDE.md). Everything above works with PdfDocument.
add_library(dp_pdfengine STATIC
    PdfiumRuntime.cpp PdfiumRuntime.h
    PdfDocument.cpp PdfDocument.h)
target_include_directories(dp_pdfengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(dp_pdfengine PUBLIC Qt6::Core Qt6::Gui pdfium)
# fpdfview.h pulls in windows.h: without this its min / max macros break std::min / std::max
target_compile_definitions(dp_pdfengine PUBLIC NOMINMAX)
