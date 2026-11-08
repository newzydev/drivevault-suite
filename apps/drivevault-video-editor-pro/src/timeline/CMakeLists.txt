add_library(dv_timeline STATIC
    TimelineModel.cpp TimelineModel.h TimelineTools.cpp TimelineAi.cpp TimelineViewModel.cpp TimelineViewModel.h)
target_include_directories(dv_timeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_timeline PUBLIC dv_core Qt6::Core Qt6::Gui)
