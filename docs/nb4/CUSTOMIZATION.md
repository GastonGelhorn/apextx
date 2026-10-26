# How the NB4 screen is customized

This note maps every on-radio customization to the code and the file that
stores it. The theme editor (`tools/apextx-theme-editor.py`) edits exactly
these files and nothing else, so the radio and the editor always agree.

## Radio-wide: `RADIO/radio.yml`

| Setting on the radio | YAML key | Code |
| --- | --- | --- |
| Appearance > palette or external theme | `selectedTheme` (name) | `theme_manager.cpp`, `nb4_palettes.cpp` |
| Appearance > accent | `nb4Accent` (0 = palette accent, 1-8 = blue, orange, green, red, yellow, cyan, magenta, white) | `nb4ApplyAccent()` |
| Appearance > orientation | `nb4Orientation` (0 portrait, 1 landscape) | `LayoutFactory::loadCustomScreens()` |
| Quick access setup | `nb4QuickAccessVersion`, `nb4QuickAccess/<slot>/val` | see MENUS.md |
| Lights | `nb4LedMode`, `nb4LedColor` | `nb4_leds.cpp` |
| (legacy) Home designs and their cards | `nb4Home`, `nb4Cards[28]` (7 designs x 4 metric slots) | `nb4_home_templates.cpp`; kept for compatibility |

Built-in palettes: ApexTX Dark, Light, Sun, Red, Green, Orange, Mono. Their
names are reserved; external themes with these names are not listed again.

## Per car: `MODELS/<model>.yml`

```yaml
screenData:
  0:
    LayoutId: "ApexTXRacing"
    layoutData:
      zones:
        0:
          widgetName: "ApexSteering"
        1:
          widgetName: "ApexThrottle"
        2:
          widgetName: "ApexTimer"
        3:
          widgetName: "ApexStats"
      options:            # layout options; ApexTXRacing has none
        0:
          type: Bool
          value:
            boolValue: 1
topbarData:
  zones:
    0:
      widgetName: "..."
nb4ScreenVersion: 1
```

- Up to 10 screens. Screen 0 is Home and uses **ApexTX Racing** by default
  (`nb4SetRacingHomeData()`): four zones (steering, throttle/brake, timer,
  statistics) plus fixed header and RF/TX/RX indicators. Any other layout can
  be chosen instead.
- Layouts (zone maps in sixtieths of the main area, `layouts/*.cpp`):
  `Layout1x1`, `1x2`, `1x3`, `1x4`, `1x6`, `2x1`, `2x2`, `2x3`, `2x4`, `1P2`,
  `1P3`, `1P4`, `2P1`, `2P3`, `4P2`, `4P2B`, `Layout1x1AM` (app mode) and
  `ApexTXRacing`.
- Common layout options (index order): top bar, flight mode, sliders, trims,
  mirror. The main area shrinks for the top bar and, if any decoration is on,
  by a padding on each side (`Layout::getMainZone()`).
- Widgets offered in every zone (`WidgetFactory::getRegisteredWidgets()`):
  ApexTX `ApexSteering`, `ApexThrottle`, `ApexTimer`, `ApexStats`, `NB4Racing`,
  `NB4SteerBar`, `NB4SteerDial`, `NB4ThrColumn`, `NB4ThrDial`, `NB4Lap`,
  `NB4Link`, `NB4Session`, `NB4Trims`, `NB4Battery` (min/max),
  `NB4Sensor` (source, min/max), and the EdgeTX widgets `Gauge`, `ModelBmp`,
  `Outputs`, `Radio Info`, `Date Time`, `Internal GPS`, `Text`, `Timer`,
  `Value`. Widget options are stored under `widgetData/options/<n>` with a
  `type` and a typed `value`.

## Theme: `THEMES/<Name>/theme.yml` (+ `background.png`)

The `summary` and `colors` sections are the existing format. An optional `nb4:`
section adds:

```yaml
nb4:
  STEERING: 0x00E5FF      # Nb4Ui::steeringColor()
  THROTTLE: 0xFF2E88      # Nb4Ui::throttleColor()
  BRAKE: 0xFFB300         # Nb4Ui::brakeColor()
  AUX: 0xB388FF           # Nb4Ui::auxColor(), CH3/CH4 widget
  CARD_OPACITY: 82        # percent (30-100), only over a background image
  BACKGROUND: neon        # informational, written by the editor
```

Absent entries keep the built-in colours. A `background.png` of 320x480 in the
theme folder (already loaded by `ThemeFile::applyBackground()`) now shows
behind the Home screen and the ApexTX widgets in portrait; cards, panels and
wells take the card opacity. Firmware without support skips the section:
unknown nodes are walked as virtual levels by `YamlTreeWalker::toChild()`.
The section is read only; saving a theme on the radio does not write it.

## Where new looks belong

- Colours, background image and card opacity are part of a **theme**.
- A different steering, throttle or channel display is a **widget**: register
  it with `Nb4WidgetFactory`, then it can be placed in any zone of any screen,
  exactly like the existing `NB4SteerBar` or `NB4ThrDial`. Added this way:
  `NB4SteerWheel`, `NB4SteerCar`, `NB4SteerBig`, `NB4ThrRing`,
  `NB4ThrSegments`, `NB4ThrSplit`, `NB4ThrBar` and `NB4Aux` (CH3/CH4).

## Theme editor

`tools/apextx-theme-editor.py` (Windows app: `tools/build-windows-theme-editor.ps1`)
edits themes, the screens of each car and the radio-wide appearance and light
settings. Display & interface > Appearance > Connect to Theme Editor selects USB
storage, so the radio appears as a drive as soon as the cable is in.

- Layouts and widgets come from `tools/apextx-theme-editor-catalog.json`,
  generated from the sources by `tools/nb4-editor-catalog.py`. Regenerate it
  after adding a layout or widget.
- `tools/nb4_editor_files.py` rewrites only `screenData` of a model and single
  keys of `radio.yml`, refuses files changed since they were read, refuses
  cars without `nb4ScreenVersion: 1`, and stores a backup under
  `~/ApexTX-Backups/theme-editor/` before every write.
- The preview is drawn by the firmware itself: `nb4-preview.dll` is the NB4
  firmware built for the PC (`radio/src/targets/simu`, target `nb4-preview`,
  no Qt or SDL). `tools/nb4_preview.py` runs it in a worker process on a
  private copy of the storage (the connected radio's `radio.yml` and car, or
  `tools/nb4-preview-defaults/`, the firmware's own factory files) and restarts
  it on every change. Steering, throttle and CH3/CH4 of the simulated drive go
  to its analog inputs; the zones it reports are the grid the editor outlines.
  Build it with MinGW-w64 (w64devkit) and pass it to the app build:

  ```
  cmake -S . -B build/nb4-preview -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/native.cmake     -DEdgeTX_SUPERBUILD=0 -DNATIVE_BUILD=1 -DPCB=PL18 -DPCBREV=NB4 -DNB4_PLUS_HW=ON     -DCMAKE_BUILD_TYPE=Release -DDISABLE_COMPANION=ON     -DCMAKE_C_FLAGS=-mno-ms-bitfields -DCMAKE_CXX_FLAGS=-mno-ms-bitfields
  cmake --build build/nb4-preview --target nb4-preview
  strip -s build/nb4-preview/nb4-preview.dll
  pwsh tools/build-windows-theme-editor.ps1 -PreviewDll build/nb4-preview/nb4-preview.dll
  ```

  `-mno-ms-bitfields` keeps the persistent structures at their radio size.
  Without the DLL the editor draws an approximation.
