add_library(dv_settings STATIC
    SettingsManager.cpp SettingsManager.h)
target_include_directories(dv_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_settings PUBLIC dv_core Qt6::Core Qt6::Gui)
