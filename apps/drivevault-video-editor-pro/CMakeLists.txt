cmake_minimum_required(VERSION 3.28)

project(DriveVaultVideoEditorPro
    VERSION 1.11.1
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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 Test LinguistTools)
qt_standard_project_setup(REQUIRES 6.8)

if(MSVC)
    add_compile_options(/W4 /wd4702 /utf-8 /permissive- /Zc:__cplusplus)
endif()

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
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(DV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
