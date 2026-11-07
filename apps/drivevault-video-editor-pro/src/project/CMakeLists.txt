add_library(dv_project STATIC
    ProjectData.h
    ProjectFile.cpp ProjectFile.h
    TrackListModel.cpp TrackListModel.h
    RecentProjectsModel.cpp RecentProjectsModel.h
    ProjectManager.cpp ProjectManager.h)
target_include_directories(dv_project PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_project PUBLIC dv_core dv_settings dv_media dv_timeline Qt6::Core Qt6::Gui)
