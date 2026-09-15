# Interface gallery

These images are rendered by the native LVGL test target from the same source
used to build the radio firmware. The model, controls, lap times, receiver
voltage, and telemetry values are deterministic simulated data. No layout is a
separate design mockup.

## Racing home

| Portrait | Landscape |
| --- | --- |
| ![Racing home in portrait orientation](images/home-portrait.png) | ![Racing home in landscape orientation](images/home-landscape.png) |

The selectable ApexTX Racing layout keeps steering, throttle and brake, trims,
RF status, both batteries, and race timing visible together. Its four instrument
zones are editable; navigation and status indicators remain fixed. It can rotate
at runtime without changing the model.

These Home captures intentionally have no receiver connected. Missing RF and
receiver-battery readings are shown explicitly rather than filled with defaults.

## Home editing and display

| Screens list | Home editor |
| --- | --- |
| ![Explicit screen list](images/screens.png) | ![Editable Racing Home](images/home-editor.png) |

| Editable zones | Appearance |
| --- | --- |
| ![Four Racing zones](images/home-zones.png) | ![Radio appearance preferences](images/appearance.png) |

Home always opens its own editor, even while another screen is visible.
Restore ApexTX design asks for confirmation and affects only that screen.

## Context and personalization

| Quick-access configuration | Help index |
| --- | --- |
| ![Ordered quick-access slots](images/quick-editor.png) | ![Help by category](images/help.png) |

| Templates in Spanish | Shared curve in Spanish |
| --- | --- |
| ![Template manager and empty state](images/templates-es.png) | ![Affected inputs and mixes](images/shared-curve-es.png) |

## Vehicle setup

| Steering | Throttle and brake |
| --- | --- |
| ![Steering travel setup](images/steering.png) | ![Throttle and brake travel setup](images/throttle.png) |

Travel, direction, centre, speed, curves, brake behaviour, and engine controls
are grouped around the two primary axes of a surface transmitter.

## Current car, startup checks and receiver/RF

| Car details | Startup checks with inline explanations (Spanish) |
| --- | --- |
| ![Name and labels without duplicated settings](images/car-details.png) | ![Startup checks explained under each control](images/safety-es.png) |

| Vehicle presets (Spanish) | Race setup (Spanish) |
| --- | --- |
| ![Optional electric and nitro defaults explained before applying](images/car-presets-es.png) | ![Lap control, spoken times and finish target](images/race-setup-es.png) |

| Receiver | Receiver header help |
| --- | --- |
| ![Receiver with surface-radio icon and header help](images/receiver.png) | ![Field-by-field receiver explanation](images/receiver-help.png) |

General is now Car details; Starting point is now Vehicle presets. Receiver,
timers, trims and startup checks are not repeated in Car details. A preset is optional
and requires confirmation. The ? control is in the header; closing help returns
to the same editor, and decorative icons do not act as hidden Back buttons.

## Throttle and brake curve

| Portrait | Landscape |
| --- | --- |
| ![Throttle and brake curve in portrait orientation](images/throttle.png) | ![Throttle and brake curve in landscape orientation](images/throttle-landscape.png) |

The same response curve and live input marker adapt to both orientations. Gas
and brake keep independent travel and direction controls around a shared
neutral point.

## Racing and telemetry

| Timers and laps | Live telemetry |
| --- | --- |
| ![Timers, lap controls and race history](images/race-timing.png) | ![Live receiver-voltage telemetry graph](images/telemetry.png) |

Lap timing includes the current run, best and last laps, delta, undo, pit
tracking, and persistent history. Telemetry pages show live values and graphs
for sensors reported by the receiver.

## Navigation and assignments

| Menu | Physical controls |
| --- | --- |
| ![Car-oriented Menu](images/settings.png) | ![Physical control assignments](images/assignments.png) |

The Menu hierarchy is organised for car use. Physical buttons,
switches, and four-way trims can also be assigned by choosing an action and
pressing the desired control.

## Menu and quick access

| Menu: portrait | Quick access: portrait |
| --- | --- |
| ![Menu modal in portrait orientation](images/settings.png) | ![Quick access in portrait orientation](images/quick-access.png) |

| Menu: landscape | Quick access: landscape |
| --- | --- |
| ![Menu modal in landscape orientation](images/settings-modal-landscape.png) | ![Quick access in landscape orientation](images/quick-access-landscape.png) |

The two main modal headers use the compact ApexTX wordmark. Section titles and
touch targets remain consistent in both layouts, and the grids reflow without
clipping their labels.
Navigation grids omit the ? button; contextual help remains in editors, and
the complete index is available in System > Help.
Quick access opens the same editor without hiding its permanent Menu entry.
Steering and Throttle/brake open their editor directly, without a grid of tabs.
The default race-oriented set uses Pit; existing Receiver shortcuts are retained.

## Stable menu review

| Display & interface | Race order and reset separator |
| --- | --- |
| ![Wider submenu options](images/display-menu-es.png) | ![Session tasks before configuration and resets](images/race-menu-es.png) |

| Navigation buttons, saved per car | Usage statistics, mixed radio/session data |
| --- | --- |
| ![Navigation-only shared button editor with scope](images/navigation-es.png) | ![Explained usage counters and car timers](images/usage-statistics.png) |

| Manual backup | Separate scoped resets |
| --- | --- |
| ![Manual USB file-copy instructions](images/manual-backup-es.png) | ![Radio, this car or both, with confirmation](images/reset-settings-es.png) |

Scope stays visible below the header. Wide submenu tiles avoid splitting long
names; compact root/quick-access grids retain their existing arrangement.

## Regenerating the images

The gallery must stay tied to a tested build. Generate the source frames with:

```sh
mkdir -p build/nb4-gallery
NB4_SCREENSHOT_DIR="$PWD/build/nb4-gallery" \
  build/nb4-device/native/gtests-radio \
  --gtest_filter='Nb4Ux.RacingHomeIsARealFourZoneLayoutWithIndependentScreenEditors:Nb4Ux.EveryAvailableRouteActuallyOpensSomethingAndComesBack:Nb4Ux.HeaderHelpReturnsToItsOwnerAndDoesNotCreateDuplicateEditors:Nb4Ux.RouteScopesAndNavigationOnlyEditorInBothLanguagesAndOrientations:Nb4Ux.MenuTilesHaveLegibleFirstFrameFocusAndFitLongNames:Nb4Ux.ContextualCurveEditorListsSharedInputsAndMixes:Nb4Ux.AssignmentsAreReadableAndSaveOnlyWhenRequested:Nb4RacingUi.AllDestinationsAndEditorsInBothLanguagesAndOrientations'
```

The tests write lossless PPM framebuffer captures. Convert only the selected
English and Spanish `ApexTX Dark` frames to the PNG files in `docs/nb4/images`
and inspect them before committing.
