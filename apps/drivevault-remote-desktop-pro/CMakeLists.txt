cmake_minimum_required(VERSION 3.28)

project(DriveVaultRemoteDesktopPro
    VERSION 0.1.0
    DESCRIPTION "DriveVault Remote Desktop Pro - see and control another Windows PC (phase 0: skeleton and measurements, no remote control yet)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

string(TIMESTAMP RD_BUILD_NUMBER "%Y%m%d%H%M" UTC)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)

if(MSVC)
    add_compile_options(/W4 /wd4702 /utf-8 /permissive- /Zc:__cplusplus)
endif()

configure_file(src/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)

# ---- STUN helper (phase 0 measurement and, later, the address discovery of the program) ---------------------------------
add_library(rd_stun STATIC src/net/Stun.cpp src/net/Stun.h)
target_include_directories(rd_stun PUBLIC src/net)
target_link_libraries(rd_stun PUBLIC Qt6::Core Qt6::Network)

# ---- phase 0 tool: what kind of NAT is this network behind? (one UDP request to each public STUN server, no data of the user) ----
add_executable(rd_natcheck tools/natcheck/natcheck.cpp)
target_link_libraries(rd_natcheck PRIVATE rd_stun)

# ---- phase 0 tool: a real NAT punching test between two PCs (no Qt, C++ runtime linked in: one exe that can be copied to a second PC) ----
if(WIN32)
    add_executable(rd_punch tools/punch/punch.cpp)
    set_property(TARGET rd_punch PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_link_libraries(rd_punch PRIVATE ws2_32)
endif()

# ---- application (phase 0: an empty window that says it is not implemented) ---------------------------------------------------
qt_add_executable(DriveVaultRemoteDesktopPro WIN32 src/main.cpp src/app.rc)
qt_add_qml_module(DriveVaultRemoteDesktopPro
    URI DriveVault.RemoteDesktop
    VERSION 1.0
    QML_FILES qml/Main.qml)
target_include_directories(DriveVaultRemoteDesktopPro PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(DriveVaultRemoteDesktopPro PRIVATE Qt6::Quick Qt6::QuickControls2)

# ---- tests ----------------------------------------------------------------------------------------------------------------
enable_testing()
find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
add_executable(tst_stun tests/tst_stun.cpp)
target_link_libraries(tst_stun PRIVATE rd_stun Qt6::Test)
add_test(NAME tst_stun COMMAND tst_stun)
set_tests_properties(tst_stun PROPERTIES ENVIRONMENT "DV_NO_AUDIO=1;QT_QPA_PLATFORM=offscreen")
if(WIN32)
    add_test(NAME punch_selftest COMMAND rd_punch --selftest)     # loopback only, no Internet
endif()
