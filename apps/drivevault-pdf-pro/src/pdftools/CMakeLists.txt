# The only place that calls qpdf (see src/CLAUDE.md): write a new PDF from chosen pages. No PDFium here, no Qt Quick.
add_library(dp_pdftools STATIC
    PdfTools.cpp PdfTools.h
    SplitPlan.cpp SplitPlan.h
    Compress.cpp Compress.h)
target_include_directories(dp_pdftools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dp_pdftools PUBLIC Qt6::Core Qt6::Gui qpdf)
