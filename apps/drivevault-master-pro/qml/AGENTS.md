# drivevault-master-pro/qml - rules (adds to the parent files)

- Module `DriveVault.Master` is built into the exe (`qt_add_qml_module` in the app `CMakeLists.txt`); every new `.qml` file must be added to `QML_FILES` there, singletons also to `set_source_files_properties(... QT_QML_SINGLETON_TYPE TRUE)`.
- `Theme.qml` holds the neutral tokens (same values as the other DriveVault programs) and the hub accent `#F59E0B`; a program card takes its colour from `product.accent` - never hard-code another program's colour in a component.
- All texts through `Tr.s("key")` with both languages in `Tr.qml`. Do not use `qsTr` here (there is no translation file).
- Do not name properties `onXxx`, `clip`, `layer`, or any Item property; ids are file-local - talk to the parent with signals (`error`, `uninstallRequested`, `browseRequested`).
- Backend values from `Hub.products` are plain maps: use `Boolean` fields as `=== true`, and strings must exist (the backend fills `""`).
- Keep `objectName`s stable (`launch_<key>`, `install_<key>`, `update_<key>`, `uninstall_<key>`, `folder_<key>`, `browse_<key>`, `card_<key>`, `status_<key>`): `tst_window` and diagnostics rely on them.
- Layout must work from 760 x 560; the footer shows installer folders and the status line; the timer refresh (5 s) must not run while a task is busy.
