configure_file(Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/dv/core/Version.h @ONLY)

add_library(dv_core STATIC
    AppPaths.cpp AppPaths.h
    Logger.cpp Logger.h
    Timecode.cpp Timecode.h
    ClipParams.cpp ClipParams.h
    FileUtils.cpp FileUtils.h)
target_include_directories(dv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(dv_core PUBLIC Qt6::Core)
