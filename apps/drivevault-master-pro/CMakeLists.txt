cmake_minimum_required(VERSION 3.28)

project(DriveVaultMasterPro
    VERSION 1.26.0
    DESCRIPTION "DriveVault Master Pro - installs, launches and updates the DriveVault programs"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(DM_BUILD_TESTS "Build tests" ON)
string(TIMESTAMP DM_BUILD_NUMBER "%Y%m%d%H%M" UTC)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 QuickDialogs2 WebEngineQuick Test)
qt_standard_project_setup(REQUIRES 6.8)

if(MSVC)
    add_compile_options(/W4 /wd4702 /utf-8 /permissive- /Zc:__cplusplus)
endif()

configure_file(src/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)

# ---- backend (also used by the tests) ----------------------------------------------------------------------
add_library(master_core STATIC src/Hub.cpp src/Hub.h src/WinUtil.cpp src/WinUtil.h src/Releases.cpp src/Releases.h
    src/ai/Streams.cpp src/ai/Streams.h src/ai/HttpChat.cpp src/ai/HttpChat.h
    src/ai/ProcessUtil.cpp src/ai/ProcessUtil.h src/ai/LlamaServer.cpp src/ai/LlamaServer.h
    src/ai/ModelDownloader.cpp src/ai/ModelDownloader.h src/ai/ModelStore.cpp src/ai/ModelStore.h
    src/ai/AiService.cpp src/ai/AiService.h
    src/ai/AgentTools.cpp src/ai/AgentTools.h src/ai/Compactor.cpp src/ai/Compactor.h src/ai/GitInfo.cpp src/ai/GitInfo.h src/ai/CommandRunner.cpp src/ai/CommandRunner.h src/ai/CommandPool.cpp src/ai/CommandPool.h src/ai/MessageModel.cpp src/ai/MessageModel.h src/ai/LocalAgent.cpp src/ai/LocalAgent.h src/ai/WebTools.cpp src/ai/WebTools.h
    src/browser/BrowserBridge.cpp src/browser/BrowserBridge.h)
target_include_directories(master_core PUBLIC src src/ai src/browser ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(master_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick)
if(WIN32)
    target_link_libraries(master_core PUBLIC shell32 version advapi32 user32)
endif()

# ---- application ------------------------------------------------------------------------------------------------
set_source_files_properties(qml/Theme.qml qml/Tr.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_executable(DriveVaultMasterPro WIN32 src/main.cpp src/app.rc)
qt_add_qml_module(DriveVaultMasterPro
    URI DriveVault.Master
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Tr.qml
        qml/ProductCard.qml
        qml/TabSwitcher.qml
        qml/AiWorkspace.qml
        qml/BrowserView.qml
        qml/AiSettingsDialog.qml
        qml/MField.qml
        qml/GearIcon.qml
        qml/LineIcon.qml
        qml/IconButton.qml
        qml/AiSwitch.qml
        qml/AboutDialog.qml
        qml/MButton.qml
        qml/MMenu.qml
        qml/MMenuItem.qml
        qml/WindowButton.qml
    RESOURCES
        resources/icons/app.png
        resources/fonts/Inter-Variable.ttf
        resources/fonts/NotoSansThai-Variable.ttf)
target_link_libraries(DriveVaultMasterPro PRIVATE master_core Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::WebEngineQuick)

if(DM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
