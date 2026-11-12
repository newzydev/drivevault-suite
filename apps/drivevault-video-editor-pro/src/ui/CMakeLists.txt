set_source_files_properties(components/Theme.qml components/ThaiMetrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Static QML module with the whole UI. Backend objects (AppSettings, Projects, ...) are registered
# at runtime under "DriveVault.Backend" by the application.
qt_add_qml_module(dv_ui
    STATIC
    URI DriveVault.UI
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/DriveVault/UI
    QML_FILES
        Main.qml
        components/Theme.qml
        components/ThaiMetrics.qml
        components/AppText.qml
        components/IconData.js
        components/Icon.qml
        components/Logo.qml
        components/WindowButton.qml
        components/TitleBar.qml
        components/AppButton.qml
        components/IconButton.qml
        components/AppToolTip.qml
        components/AppTextField.qml
        components/AppComboBox.qml
        components/AppSwitch.qml
        components/AppSlider.qml
        components/AppScrollBar.qml
        components/AppDialog.qml
        components/AppMenu.qml
        components/AppMenuItem.qml
        components/AppMenuSeparator.qml
        components/ToastHost.qml
        components/EmptyState.qml
        components/StatusBadge.qml
        components/PanelHeader.qml
        components/SettingRow.qml
        components/PathSetting.qml
        components/Section.qml
        screens/HomeScreen.qml
        screens/NewProjectDialog.qml
        screens/ConfirmDialog.qml
        screens/AboutDialog.qml
        editor/EditorScreen.qml
        editor/EditorMenuBar.qml
        editor/WorkspaceBar.qml
        editor/MediaPanel.qml
        editor/ToolPanel.qml
        editor/MasterMeter.qml
        editor/MediaItemView.qml
        editor/TimelineClip.qml
        editor/ClipLevelLine.qml
        editor/PreviewScene.qml
        editor/PreviewTile.qml
        editor/TransformBox.qml
        editor/ProgramMonitor.qml
        editor/InspectorPanel.qml
        editor/ParamRow.qml
        editor/ParamSection.qml
        editor/CurvesEditor.qml
        editor/TitleEditor.qml
        editor/MixerPanel.qml
        editor/MixerKnob.qml
        editor/MixerFader.qml
        editor/AiPanel.qml
        editor/ModelManagerView.qml
        editor/TimelinePanel.qml
        editor/EditorStatusBar.qml
        editor/ExportWorkspace.qml
        settings/SettingsDialog.qml
        settings/GeneralPage.qml
        settings/AppearancePage.qml
        settings/PerformancePage.qml
        settings/EditingPage.qml
        settings/AudioPage.qml
        settings/AiPage.qml
        settings/ExportSettingsPage.qml
        settings/AboutPage.qml)

target_link_libraries(dv_ui PRIVATE Qt6::Quick Qt6::QuickControls2)
