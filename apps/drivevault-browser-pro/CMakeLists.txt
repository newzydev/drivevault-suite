cmake_minimum_required(VERSION 3.28)

project(DriveVaultBrowserPro
    VERSION 1.3.1
    DESCRIPTION "DriveVault Browser Pro - a web browser with a built-in ad blocker"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(DB_BUILD_TESTS "Build tests" ON)
string(TIMESTAMP DB_BUILD_NUMBER "%Y%m%d%H%M" UTC)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 QuickDialogs2 WebEngineQuick PrintSupport Test)
qt_standard_project_setup(REQUIRES 6.8)

set(DB_PDFIUM_DIR "${CMAKE_SOURCE_DIR}/../../.tools/pdfium" CACHE PATH "PDFium build")
add_library(browser_pdfium SHARED IMPORTED)
set_target_properties(browser_pdfium PROPERTIES
    IMPORTED_IMPLIB "${DB_PDFIUM_DIR}/lib/pdfium.dll.lib"
    IMPORTED_LOCATION "${DB_PDFIUM_DIR}/bin/pdfium.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${DB_PDFIUM_DIR}/include")
file(COPY "${DB_PDFIUM_DIR}/bin/pdfium.dll" DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

if(MSVC)
    add_compile_options(/W4 /wd4702 /utf-8 /permissive- /Zc:__cplusplus)
endif()

configure_file(src/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)
set(oauth_header "${CMAKE_BINARY_DIR}/generated/GoogleDesktopOAuth.h")
if(NOT EXISTS "${oauth_header}")
    file(WRITE "${oauth_header}" [=[
#pragma once
namespace dv::br::desktopOAuth {
inline constexpr char key[] = "";
inline constexpr char iv[] = "";
inline constexpr char ciphertext[] = "";
}
]=])
endif()

# ---- core (no WebEngine; also used by the tests) -------------------------------------------------------------------
add_library(browser_core STATIC
    src/core/UrlUtil.cpp src/core/UrlUtil.h
    src/core/BookmarkStore.cpp src/core/BookmarkStore.h
    src/core/HistoryStore.cpp src/core/HistoryStore.h
    src/core/BrowserSettings.cpp src/core/BrowserSettings.h
    src/core/MediaPolicy.cpp src/core/MediaPolicy.h src/core/MediaSaver.cpp src/core/MediaSaver.h
    src/adblock/NetRule.h src/adblock/FilterParser.cpp src/adblock/FilterParser.h src/adblock/RuleSet.cpp src/adblock/RuleSet.h src/adblock/ListManager.cpp src/adblock/ListManager.h
    src/adblock/Cosmetic.cpp src/adblock/Cosmetic.h src/adblock/StandIns.cpp src/adblock/StandIns.h src/adblock/PageScript.cpp src/adblock/PageScript.h)
target_include_directories(browser_core PUBLIC src/core src/adblock ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(browser_core PUBLIC Qt6::Core Qt6::Network)

# ---- application ---------------------------------------------------------------------------------------------------------
set_source_files_properties(qml/Theme.qml qml/Tr.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_executable(DriveVaultBrowserPro WIN32
    src/app/main.cpp src/app/BrowserApp.cpp src/app/BrowserApp.h src/app/DownloadModel.cpp src/app/DownloadModel.h
    src/app/ProfileSession.cpp src/app/ProfileSession.h
    src/app/AdBlockInterceptor.cpp src/app/AdBlockInterceptor.h src/app/AdBlockController.cpp src/app/AdBlockController.h src/app/StandInHandler.cpp src/app/StandInHandler.h src/app/MediaController.cpp src/app/MediaController.h src/app.rc)
qt_add_qml_module(DriveVaultBrowserPro
    URI DriveVault.Browser
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/BrowserWindow.qml
        qml/Theme.qml
        qml/Tr.qml
        qml/LineIcon.qml
        qml/IconButton.qml
        qml/WindowButton.qml
        qml/GearIcon.qml
        qml/BButton.qml
        qml/NewTabPage.qml
        qml/SidePanel.qml
        qml/BrowserSettingsPage.qml
    RESOURCES
        resources/icons/app.png
        resources/workspace/coast.jpg
        resources/workspace/google-favicon.png
        resources/adblock-page.js
        resources/pip-on.js
        resources/pip-off.js
        resources/fonts/Inter-Variable.ttf
        resources/fonts/NotoSansThai-Variable.ttf)
target_include_directories(DriveVaultBrowserPro PRIVATE src/app "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(DriveVaultBrowserPro PRIVATE browser_core browser_pdfium Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::WebEngineQuick Qt6::WebEngineCore Qt6::PrintSupport bcrypt crypt32)

if(DB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

