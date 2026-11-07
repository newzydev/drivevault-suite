# ---- Embedded resources: fonts, icons, compiled translations ---------------------------------------
add_library(dv_resources STATIC resources.cpp)
target_link_libraries(dv_resources PUBLIC Qt6::Core)

qt_add_resources(dv_resources "dv_assets"
    PREFIX /
    BASE ${CMAKE_SOURCE_DIR}/resources
    FILES
        ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Variable.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/NotoSansThai-Variable.ttf
        ${CMAKE_SOURCE_DIR}/resources/icons/app.png)

# Translations. qt_add_translations() is deferred and mishandles repository paths with spaces,
# so lupdate / lrelease are wired up explicitly. Relative paths keep lupdate's project file clean.
file(GLOB_RECURSE DV_TRANSLATION_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h ${CMAKE_SOURCE_DIR}/src/*.qml)
set(DV_TS_FILES ${CMAKE_SOURCE_DIR}/resources/translations/drivevault_th_TH.ts)
qt_add_lupdate(dv_resources
    TS_FILES ${DV_TS_FILES}
    SOURCES ${DV_TRANSLATION_SOURCES}
    LUPDATE_OPTIONS -no-obsolete)
qt_add_lrelease(dv_resources
    TS_FILES ${DV_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE DV_QM_FILES)
qt_add_resources(dv_resources "dv_translations"
    PREFIX /i18n
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${DV_QM_FILES})

# ---- Application core: backend objects exposed to QML --------------------------------------------
add_library(dv_appcore STATIC
    AppController.cpp AppController.h
    AppContext.cpp AppContext.h)
target_include_directories(dv_appcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dv_appcore PUBLIC
    dv_core dv_settings dv_localization dv_project dv_platform dv_playback dv_export dv_ai dv_resources
    Qt6::Gui Qt6::Qml)

# ---- Executable ---------------------------------------------------------------------------------------
qt_add_executable(dv_app WIN32 main.cpp app.rc)
set_target_properties(dv_app PROPERTIES OUTPUT_NAME "DriveVaultVideoEditorPro")
target_link_libraries(dv_app PRIVATE
    dv_appcore dv_ui dv_uiplugin
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

install(TARGETS dv_app RUNTIME DESTINATION .)
