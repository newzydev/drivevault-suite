add_library(dv_playback STATIC
    Compositor.cpp Compositor.h
    Effects.cpp Effects.h
    AudioMixer.cpp AudioMixer.h
    AudioOutput.cpp AudioOutput.h
    PlaybackEngine.cpp PlaybackEngine.h
    FrameView.cpp FrameView.h)
target_include_directories(dv_playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_playback PUBLIC dv_core dv_media dv_project dv_settings Qt6::Core Qt6::Gui Qt6::Quick)
if(WIN32)
    target_link_libraries(dv_playback PRIVATE winmm)
endif()
