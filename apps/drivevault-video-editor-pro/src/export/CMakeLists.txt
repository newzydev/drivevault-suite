add_library(dv_export STATIC
    Exporter.cpp Exporter.h
    ExportController.cpp ExportController.h
    ProxyManager.cpp ProxyManager.h)
target_include_directories(dv_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_export PUBLIC dv_core dv_media dv_playback dv_project Qt6::Core Qt6::Gui)
