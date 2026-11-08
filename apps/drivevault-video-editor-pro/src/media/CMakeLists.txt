# FFmpeg (LGPL, shared) - the suite's .tools\ffmpeg build by default; override with -DDV_FFMPEG_DIR=<dir>
set(DV_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/../../.tools/ffmpeg" CACHE PATH "FFmpeg shared build (include/, lib/*.lib, bin/*.dll)")
if(NOT EXISTS "${DV_FFMPEG_DIR}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg not found in ${DV_FFMPEG_DIR}. See README.md (Prerequisites) or pass -DDV_FFMPEG_DIR=<dir>.")
endif()

add_library(dv_ffmpeg INTERFACE)
target_include_directories(dv_ffmpeg SYSTEM INTERFACE "${DV_FFMPEG_DIR}/include")
foreach(lib avformat avcodec avutil swscale swresample)
    target_link_libraries(dv_ffmpeg INTERFACE "${DV_FFMPEG_DIR}/lib/${lib}.lib")
endforeach()

# Runtime DLLs next to every executable so tests and the app run straight from the build folder.
file(GLOB DV_FFMPEG_DLLS "${DV_FFMPEG_DIR}/bin/*.dll")
add_custom_target(dv_ffmpeg_dlls ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DV_FFMPEG_DLLS} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMENT "Copying FFmpeg DLLs")

add_library(dv_media STATIC
    FFmpeg.h
    MediaProbe.cpp MediaProbe.h
    HardwareDecode.cpp HardwareDecode.h
    VideoReader.cpp VideoReader.h
    FrameThumbs.cpp FrameThumbs.h
    AudioReader.cpp AudioReader.h
    MediaCache.cpp MediaCache.h
    MediaLibrary.cpp MediaLibrary.h)
target_include_directories(dv_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_media PUBLIC dv_core Qt6::Core Qt6::Gui dv_ffmpeg)
add_dependencies(dv_media dv_ffmpeg_dlls)
