add_library(dv_localization STATIC
    LanguageManager.cpp LanguageManager.h)
target_include_directories(dv_localization PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_localization PUBLIC dv_core dv_settings Qt6::Core)
