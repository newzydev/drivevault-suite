# drivevault-video-editor-pro/src/ui - rules (adds to the parent files)

- QML module `DriveVault.UI` (static); backend objects come from `DriveVault.Backend` as singletons: `AppSettings, Localization, Recents, Projects, App, SysInfo, WinIntegration, Playback, ExportJob, Ai, Proxies`.
  Every new `.qml` file must be listed in `ui/CMakeLists.txt`; `Theme.qml` is a singleton.
- Design system: `docs/ui-design-system.md`. Use `Theme` tokens (colours, radii, font sizes, durations), the shared components in `components/` (`AppButton`, `IconButton`, `AppSlider`, `AppMenu` ...) and icons from `IconData.js`. No hard-coded colours except brand / clip colours defined in `Theme`.
- Texts: `qsTr` only. Use `%n` plural forms for counts. Thai text is rendered with Noto Sans Thai (Theme font sizes get +1 for Thai).
- Folders: `screens/` (Home, New project, dialogs), `editor/` (Edit screen, timeline, inspector, AI panel, export workspace, workspace bar, menus), `settings/` (pages), `components/`.
- Inspector sections are `ParamSection` + `ParamRow`s bound to `Projects.timeline.paramValue(...)`; refresh them through the `rev` counter, not by polling. Workspaces decide which sections show (see `InspectorPanel.qml`).
- Rules that bit before: no property named `onXxx`, `clip`, `layer`; an outer id is shadowed inside delegates; `Layout.minimumWidth: 0` on sections; do not assign `undefined` to typed properties (give `info` objects defaults); `required property` in delegates.
- Keep `objectName`s stable (`tst_ui_flow` clicks them): e.g. `trackSolo<N>`, `tool_<name>`, `preset_<id>`, `aiSilence`, `exportStart`, `param_<key>`, `key_<key>`.
- Never block the UI: long actions call backend jobs that report progress and can be cancelled.
- Status badges tell the truth: `available`, `modelRequired`, `unsupported`, `notImplemented`, `info`.
- `tst_ui_flow` fails on any QML warning - run it after UI changes and fix every warning.
