# UI design system

Tokens live in `src/ui/components/Theme.qml` (singleton). Components are plain QML files in
`src/ui/components`. Never hard-code colours, sizes or durations in screens - add a token.

## Colour tokens

| Token | Dark (default) | Light |
|---|---|---|
| background | `#101114` | `#ECEEF3` |
| backgroundAlt | `#181A20` | `#F6F7FA` |
| surface | `#20232B` | `#FFFFFF` |
| surfaceElevated | `#292D36` | `#FFFFFF` |
| border | `#343844` | `#D3D7E0` |
| text | `#F8FAFC` | `#14161B` |
| textSecondary | `#A1A1AA` | `#565B6B` |
| accent (default) | `#8B5CF6` | same |
| accentHover | accent lightened 22 % | accent darkened 12 % |
| success / warning / error / info | `#22C55E` / `#F59E0B` / `#EF4444` / `#3B82F6` | same |

Accent colour is user-selectable (6 presets + any `#RRGGBB`); `textOnAccent` is derived from the accent's
WCAG relative luminance so text on primary buttons stays readable.
*Note:* QML treats `onXxx` identifiers as signal handlers - never name a property `onSomething`.

## Typography

Inter (Latin) with Noto Sans Thai fallback, both bundled (SIL OFL). Sizes: 11 / 12 / 13 / 15 / 18 / 24 / 36 px
plus the user's text-size offset. Thai gets +1 px because its x-height is smaller.

## Components

`AppButton` (primary / secondary / ghost / danger), `IconButton`, `AppTextField`, `AppComboBox`,
`AppSwitch`, `AppSlider`, `AppScrollBar`, `AppToolTip`, `AppDialog`, `AppMenu` / `AppMenuItem` /
`AppMenuSeparator`, `ToastHost`, `EmptyState`, `StatusBadge`, `PanelHeader`, `SettingRow`, `PathSetting`,
`Section`, `Icon` (+ `IconData.js`), `Logo`.

## Rules

* **Honest status:** anything not implemented is disabled and labelled with `StatusBadge`
  (Available / Not implemented / Model required / Unsupported hardware) plus its phase. Status is never
  colour-only - it always has an icon and text.
* **Focus:** every interactive control shows a 2 px accent focus ring (`visualFocus`).
* **Tooltips:** every icon-only button has a tooltip and an accessible name.
* **Motion:** 110 / 190 / 320 ms; all durations become 0 when "Interface animations" is off.
* **Layout:** panels resize with `SplitView`; sizes are remembered per workspace; View > Reset Panel Layout.
* **Localization:** every visible string goes through `qsTr`.
