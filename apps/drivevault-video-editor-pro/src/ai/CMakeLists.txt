add_library(dv_ai STATIC
    AiTypes.h
    AudioAnalysis.cpp AudioAnalysis.h
    AudioEnhancer.cpp AudioEnhancer.h
    Tracker.cpp Tracker.h
    Transcriber.cpp Transcriber.h
    OpenThaiAsr.cpp OpenThaiAsr.h
    OpenThaiInstaller.cpp OpenThaiInstaller.h
    Assistant.cpp Assistant.h
    ModelManager.cpp ModelManager.h
    AiController.cpp AiController.h)
target_include_directories(dv_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_ai PUBLIC dv_core dv_media dv_playback dv_project dv_timeline dv_settings Qt6::Core Qt6::Gui Qt6::Network)
if(DV_ONNX_AVAILABLE)
    target_compile_definitions(dv_ai PUBLIC DV_ONNX_AVAILABLE)
    target_include_directories(dv_ai SYSTEM PRIVATE "${DV_ONNX_DIR}/include")
    target_link_libraries(dv_ai PUBLIC "${DV_ONNX_DIR}/lib/onnxruntime.lib")
endif()
