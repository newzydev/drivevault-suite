set_source_files_properties(
    components/Theme.qml components/Tr.qml components/ThaiMetrics.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Static QML module with the whole UI. The backend object "Doc" is registered by main.cpp under "DriveVault.PdfPro.Backend".
qt_add_qml_module(dp_ui
    STATIC
    URI DriveVault.PdfPro.UI
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/DriveVault/PdfPro/UI
    QML_FILES
        Main.qml
        components/Theme.qml
        components/Tr.qml
        components/ThaiMetrics.qml
        components/AppText.qml
        components/PageView.qml
        components/PMenu.qml
        components/PMenuItem.qml
        components/LineIcon.qml
        components/PButton.qml
        components/ConfirmDialog.qml
        components/LanguageSwitch.qml
        components/PdfTitleBar.qml
        components/WindowButton.qml
        components/ToolEntry.qml
        components/ToolSearch.qml
        screens/HomeScreen.qml
        screens/PreferencesDialog.qml
        screens/AboutPdfDialog.qml
        screens/ViewerScreen.qml
        screens/PasswordDialog.qml
        screens/FileTabs.qml
        screens/SearchBar.qml
        screens/SidePanel.qml
        screens/LinkDialog.qml
        screens/PrintDialog.qml
        screens/OrganizerDialog.qml
        screens/SplitDialog.qml
        screens/CompressDialog.qml
        screens/EditBar.qml
        screens/MarkDialog.qml
        screens/FormDialog.qml
        screens/ImageExportDialog.qml
        screens/OcrDialog.qml
        screens/ReplaceTextDialog.qml
        screens/SecurityDialog.qml
        screens/RedactionBar.qml
        screens/MaintenanceDialog.qml
        screens/BatchDialog.qml
        screens/CreatePdfDialog.qml)
target_link_libraries(dp_ui PRIVATE Qt6::Quick Qt6::QuickControls2)
