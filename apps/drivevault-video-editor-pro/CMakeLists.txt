cmake_minimum_required(VERSION 3.28)

project(DriveVaultVideoEditorPro
    VERSION 1.17.0
    DESCRIPTION "DriveVault Video Editor Pro - Professional Desktop Video Editor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(DV_BUILD_TESTS "Build unit tests" ON)

# Build number: yyyymmddHHMM (UTC) of the configure step.
string(TIMESTAMP DV_BUILD_NUMBER "%Y%m%d%H%M" UTC)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 QuickDialogs2 Test LinguistTools)
qt_standard_project_setup(REQUIRES 6.8)

if(MSVC)
    add_compile_options(/W4 /wd4702 /utf-8 /permissive- /Zc:__cplusplus)
endif()

# ONNX Runtime (CPU) runs the OpenThai speech model. It is optional: without it the program builds and uses whisper.cpp only.
set(DV_TOOLS_DIR "${CMAKE_SOURCE_DIR}/../../.tools")
if(DEFINED ENV{DV_TOOLS_DIR})
    set(DV_TOOLS_DIR "$ENV{DV_TOOLS_DIR}")
endif()
set(DV_ONNX_DIR "${DV_TOOLS_DIR}/onnxruntime-win-x64-1.24.4" CACHE PATH "ONNX Runtime CPU SDK (include/, lib/onnxruntime.lib, lib/onnxruntime.dll)")
set(DV_ONNX_AVAILABLE OFF)
if(EXISTS "${DV_ONNX_DIR}/include/onnxruntime_cxx_api.h" AND EXISTS "${DV_ONNX_DIR}/lib/onnxruntime.lib" AND EXISTS "${DV_ONNX_DIR}/lib/onnxruntime.dll")
    set(DV_ONNX_AVAILABLE ON)
    file(COPY "${DV_ONNX_DIR}/lib/onnxruntime.dll" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    message(STATUS "ONNX Runtime found: ${DV_ONNX_DIR}")
else()
    message(STATUS "ONNX Runtime not found in ${DV_ONNX_DIR}: the OpenThai speech model is not built in")
endif()

# The Google account of the suite (one file per Windows user, shared with the other programs): shared/auth
add_subdirectory("${CMAKE_SOURCE_DIR}/../../shared/auth" "${CMAKE_BINARY_DIR}/dv_account")

add_subdirectory(src/core)
add_subdirectory(src/settings)
add_subdirectory(src/localization)
add_subdirectory(src/media)
add_subdirectory(src/timeline)
add_subdirectory(src/project)
add_subdirectory(src/platform)
add_subdirectory(src/export)
add_subdirectory(src/playback)
add_subdirectory(src/ai)
add_subdirectory(src/mcp)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(DV_BUILD_TESTS)
    enable_testing()
    dv_account_add_tests()
    add_subdirectory(tests)
endif()
