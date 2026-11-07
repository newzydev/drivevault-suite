add_library(dv_platform STATIC
    SystemInfo.cpp SystemInfo.h
    WindowsIntegration.cpp WindowsIntegration.h
    CrashHandler.cpp CrashHandler.h)
target_include_directories(dv_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_platform PUBLIC dv_core Qt6::Core Qt6::Gui)
if(WIN32)
    target_link_libraries(dv_platform PRIVATE dwmapi)
endif()
