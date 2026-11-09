cmake_minimum_required(VERSION 3.28)

project(DriveVaultPdfPro
    VERSION 0.4.0
    DESCRIPTION "DriveVault PDF Pro - read, merge, split, compress, edit, protect and redact PDF files"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(DP_BUILD_TESTS "Build tests" ON)

# Build number: yyyymmddHHMM (UTC) of the configure step.
string(TIMESTAMP DP_BUILD_NUMBER "%Y%m%d%H%M" UTC)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 PrintSupport Test)
qt_standard_project_setup(REQUIRES 6.8)

if(MSVC)
    add_compile_options(/W4 /wd4702 /utf-8 /permissive- /Zc:__cplusplus)
endif()

configure_file(src/core/Version.h.in ${CMAKE_BINARY_DIR}/generated/dp/Version.h @ONLY)

# ---- PDFium (BSD-3, prebuilt by bblanchon/pdfium-binaries) - the suite's .tools\pdfium by default ----------------
set(DP_PDFIUM_DIR "${CMAKE_SOURCE_DIR}/../../.tools/pdfium" CACHE PATH "PDFium build (include/, lib/pdfium.dll.lib, bin/pdfium.dll)")
if(DEFINED ENV{DV_PDFIUM_DIR})
    set(DP_PDFIUM_DIR "$ENV{DV_PDFIUM_DIR}")
endif()
if(NOT EXISTS "${DP_PDFIUM_DIR}/include/fpdfview.h")
    message(FATAL_ERROR "PDFium not found in ${DP_PDFIUM_DIR}. See README.md (Prerequisites) or pass -DDP_PDFIUM_DIR=<dir>.")
endif()
add_library(pdfium SHARED IMPORTED)
set_target_properties(pdfium PROPERTIES
    IMPORTED_IMPLIB "${DP_PDFIUM_DIR}/lib/pdfium.dll.lib"
    IMPORTED_LOCATION "${DP_PDFIUM_DIR}/bin/pdfium.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${DP_PDFIUM_DIR}/include")
# The DLL sits next to the exe and the test programs.
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY "${DP_PDFIUM_DIR}/bin/pdfium.dll" DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# ---- qpdf (Apache-2.0): the file structure - merge, split, reorder, rotate, crop - from .toolsqpdf ----------------------------------
set(DP_QPDF_DIR "${CMAKE_SOURCE_DIR}/../../.tools/qpdf" CACHE PATH "qpdf msvc64 release (include/, lib/qpdf.lib, bin/qpdf30.dll)")
if(DEFINED ENV{DV_QPDF_DIR})
    set(DP_QPDF_DIR "$ENV{DV_QPDF_DIR}")
endif()
if(NOT EXISTS "${DP_QPDF_DIR}/include/qpdf/QPDF.hh")
    message(FATAL_ERROR "qpdf not found in ${DP_QPDF_DIR}. See README.md (Prerequisites) or pass -DDP_QPDF_DIR=<dir>.")
endif()
add_library(qpdf SHARED IMPORTED)
set_target_properties(qpdf PROPERTIES
    IMPORTED_IMPLIB "${DP_QPDF_DIR}/lib/qpdf.lib"
    IMPORTED_LOCATION "${DP_QPDF_DIR}/bin/qpdf30.dll")
target_include_directories(qpdf SYSTEM INTERFACE "${DP_QPDF_DIR}/include")
file(GLOB DP_QPDF_DLLS "${DP_QPDF_DIR}/bin/*.dll")
file(COPY ${DP_QPDF_DLLS} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

add_subdirectory(src/pdfengine)
add_subdirectory(src/pdftools)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(DP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
