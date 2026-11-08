add_library(dv_ai STATIC
    AiTypes.h
    AudioAnalysis.cpp AudioAnalysis.h
    AudioEnhancer.cpp AudioEnhancer.h
    Tracker.cpp Tracker.h
    Transcriber.cpp Transcriber.h
    Assistant.cpp Assistant.h
    EditPlanner.cpp EditPlanner.h
    LocalLlm.cpp LocalLlm.h
    ModelManager.cpp ModelManager.h
    AiController.cpp AiController.h)
target_include_directories(dv_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_ai PUBLIC dv_core dv_media dv_playback dv_project dv_timeline dv_settings Qt6::Core Qt6::Gui Qt6::Network)
