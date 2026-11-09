# ---- Embedded resources: fonts and the icon -------------------------------------------------------------------
qt_add_library(dp_resources STATIC)
qt_add_resources(dp_resources "dp_assets"
    PREFIX /
    BASE ${CMAKE_SOURCE_DIR}/resources
    FILES
        ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Variable.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/NotoSansThai-Variable.ttf
        ${CMAKE_SOURCE_DIR}/resources/icons/app.png)

# ---- Application core: backend objects for QML ------------------------------------------------------------------
add_library(dp_appcore STATIC
    DocumentController.cpp DocumentController.h
    PageImageProvider.cpp PageImageProvider.h
    PageRange.cpp PageRange.h
    PrintTask.cpp PrintTask.h
    PageListModel.cpp PageListModel.h)
target_include_directories(dp_appcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dp_appcore PUBLIC dp_pdfengine dp_pdftools Qt6::Gui Qt6::Qml Qt6::Quick Qt6::PrintSupport)

# ---- Executable ----------------------------------------------------------------------------------------------------
qt_add_executable(dp_app WIN32 main.cpp app.rc)
set_target_properties(dp_app PROPERTIES OUTPUT_NAME "DriveVaultPdfPro")
target_link_libraries(dp_app PRIVATE
    dp_appcore dp_ui dp_uiplugin dp_resources
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

install(TARGETS dp_app RUNTIME DESTINATION .)
