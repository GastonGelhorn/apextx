# How the NB4 screen is customized

This note maps every on-radio customization to the code and the file that
stores it. The theme editor (`tools/apextx-theme-editor.py`) preserves the
firmware's YAML representations and labels previews from a different firmware
revision as approximations.

## Desktop workspace

Flasher separates **Firmware & resources** (Update, Components, Install or
recover) from **Personalization** (Theme, Screens, Appearance). Switching
between those workspaces retains the loaded editor and its drafts. The window
uses the available width; a wide editor keeps the preview next to independently
scrolling controls, while narrow windows stack the preview and controls.

The theme library supports searching and category filtering, with 35 presets
and 29 background choices (including no background and an imported image).
Procedural backgrounds are rendered to PNG for both radio orientations.
Colours open a visual picker with hue, saturation, brightness and common
swatches; hexadecimal input remains under Advanced. Apply records one undo
action, while Cancel restores the original colour. Widget RGB overrides use
the same picker and can still refer to a theme colour.

Six card styles adjust the existing palette and opacity settings. Instruments
that require an opaque surface keep it; corners and geometry remain those of
the firmware. Screens include eight ready-made designs, filtered by the
connected firmware's capabilities, plus a gallery of 22 common widgets grouped
by function. A design occupies a free screen slot and preserves existing ones.
Duplicate screen retains all options, including unknown future fields. The
ten-screen limit applies to both actions, and additions can be undone.

The preview can be exported as PNG. The bottom toolbar shows the draft state and keeps
history and save actions within reach. Orientation preview remains temporary.
Desktop PNG, theme ZIP and performance exports are saved under
`~/ApexTX-Themes/exports/` with unique names; the result shows the actual path.

When testing a desktop build alongside an existing application, use
`tools/build-flasher.py --dist-dir dist/ui-refresh` with the usual dependency
arguments, then `tools/test-flasher-package.py --dist-dir dist/ui-refresh`.
Close the existing Flasher before opening another build; both share the same
operation lock.

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

Absent entries keep the built-in colours; explicit black remains a colour.
The theme folder can contain `background.png` (320x480) and a separate
`background_480x320.png` for landscape. Animated instrument cards stay opaque;
static panels use the selected opacity. Saving from the radio preserves the
supported instrument colours and card opacity. Firmware without support skips
the extension section.

## Where new looks belong

- Colours, background image and card opacity are part of a **theme**.
- A different steering, throttle or channel display is a **widget**: register
  it with `Nb4WidgetFactory`, then it can be placed in any zone of any screen,
  exactly like the existing `NB4SteerBar` or `NB4ThrDial`. Added this way:
  `NB4SteerWheel`, `NB4SteerCar`, `NB4SteerBig`, `NB4ThrRing`,
  `NB4ThrSegments`, `NB4ThrSplit`, `NB4ThrBar` and `NB4Aux` (CH3/CH4).

## Editor in ApexTX Flasher

Personalization is a workspace in ApexTX Flasher, sharing the local authenticated server,
USB identity, operation lock and application lifecycle. It works offline for
preview and ZIP export. On the radio open **Display > Editor** to enter USB
storage, including when the cable is already connected. Selecting a preview
orientation never writes `radio.yml`; use Save changes to apply it.

The radio exports `APEXTX-EDITOR.json` before storage handover. Protocol 1
contains the USB serial, hardware target, firmware revision, registered layout
and widget IDs, widget option descriptors from their actual factories,
canonical source tokens and current model filename. The app checks these
against the connected mass-storage device before saving; a legacy or stale
volume remains readable but cannot authorize writes. The packaged catalogue
supplies the layout maps and translated labels. Unknown options are preserved.
Use the storage-folder field for a mount point that automatic discovery misses.

Theme, model and appearance changes are drafts. Undo/redo retains up to 100
steps per document. Save draft writes an offline project under
`~/ApexTX-Themes/drafts/`; Open draft restores its themes, screens and appearance
without writing the radio. Switching cars with pending screen edits offers Save,
Discard or Cancel. A native-window close checks pending drafts after the
existing firmware-update close guard. Theme Save commits pending screens and
appearance together. Backups and transaction journals live on the computer in
`~/ApexTX-Backups/theme-editor/<transaction-id>/`. Each replacement is staged,
flushed and verified before replacing its old file. This is per-file replacement,
not a claim of filesystem-wide atomicity on FAT. A partial transaction reports
its backup folder. The Recover an interrupted save panel can resume or restore
it without discarding the current draft. `/api/editor/recover` accepts that folder, target volume and
`restore: true` to restore, or `false` to resume, after identity/hash checks.
A removed managed background is backed up and deleted; unrelated theme files
are retained. Conflicting folder names or externally changed files are refused.

The native firmware preview runs in an isolated worker with authenticated
loopback IPC, independently of stdin/stdout or a console. LVGL access shares
the simulator interface mutex. Frames from replaced workers are discarded;
worker failure exposes the approximation with an error badge. NB4 and NB4+
use separate preview builds. A revision mismatch is labelled in the editor.
Instrument cards are opaque; static surfaces retain theme opacity.

### Local builds

Build from the repository root. The device build must follow `AGENTS.md`, use
Arm GNU 14.2.Rel1 and explicitly select original NB4 hardware. NB4 enables
firmware LTO to fit the complete four-language UI in internal flash.

For each native preview use the native toolchain and matching host compiler:

```sh
cmake -S . -B build/nb4-customization/native \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/native.cmake \
  -DEdgeTX_SUPERBUILD=0 -DNATIVE_BUILD=1 -DPCB=PL18 -DPCBREV=NB4 \
  -DNB4_PLUS_HW=OFF -DDISABLE_COMPANION=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/nb4-customization/native --target nb4-preview -j8
cmake -S . -B build/nb4plus-preview/native \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/native.cmake \
  -DEdgeTX_SUPERBUILD=0 -DNATIVE_BUILD=1 -DPCB=PL18 -DPCBREV=NB4 \
  -DNB4_PLUS_HW=ON -DDISABLE_COMPANION=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/nb4plus-preview/native --target nb4-preview -j8
```

Then use `tools/build-macos-flasher.sh`, `tools/build-windows-flasher.ps1` or
`tools/build-linux-flasher.sh`. The former standalone Windows editor build
entry point forwards to the shared Flasher. Linux packages are portable tarballs;
GTK/WebKit, libusb and the appropriate USB device permissions remain host
requirements. Automatic app updates remain supported on Windows and macOS.
CI builds preview libraries and packages on each target OS; host builds do not
substitute for running those platform checks.

### Physical performance check

No hardware result is implied by a native render or packaged-app self-test.
Use the original NB4, the verified normal-update image and the workflow in
`AGENTS.md`. Confirm the Home screen after installation.

For portrait and landscape separately, use the same screen and 30 seconds of
the same steering/throttle motion with (1) no background, (2) background with
opaque instruments and (3) translucent static surfaces. Also capture 30 seconds
at rest with timers stopped. Opening Editor exports the last thirty one-second
windows to `APEXTX-UI-PERF.json`; the Performance JSON button downloads it.
Save each capture separately on the computer. Compare the same serial/build:

```sh
.venv/bin/python tools/nb4-editor-perf.py no-background.json opaque.json
.venv/bin/python tools/nb4-editor-perf.py idle.json --idle
```

The comparator omits two windows at each end for navigation. It requires zero
repainted pixels/areas in the idle sample and at most 10% mean UI-loop and LVGL overhead
for the background case. Keep the captures with the firmware SHA-256 and note
any physical restart/theme issue; simulator timing is not a hardware measurement.
