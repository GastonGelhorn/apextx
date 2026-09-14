# Interface gallery

These images are rendered by the native LVGL test target from the same source
used to build the radio firmware. The model, controls, lap times, receiver
voltage, and telemetry values are deterministic simulated data. No layout is a
separate design mockup.

## Racing home

| Portrait | Landscape |
| --- | --- |
| ![Racing home in portrait orientation](images/home-portrait.png) | ![Racing home in landscape orientation](images/home-landscape.png) |

The home view keeps steering, throttle and brake, trims, RF status, both
batteries, and race timing visible together. It can rotate at runtime without
changing the model.

The portrait capture is the radio's full-height racing view. The simulated
receiver is connected at 83% link quality and reports 6.0 V.

## Vehicle setup

| Steering | Throttle and brake |
| --- | --- |
| ![Steering travel setup](images/steering.png) | ![Throttle and brake travel setup](images/throttle.png) |

Travel, direction, centre, speed, curves, brake behaviour, and engine controls
are grouped around the two primary axes of a surface transmitter.

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

| Settings | Physical controls |
| --- | --- |
| ![Car-oriented settings menu](images/settings.png) | ![Physical control assignments](images/assignments.png) |

The settings hierarchy is organised for car and boat use. Physical buttons,
switches, and four-way trims can also be assigned by choosing an action and
pressing the desired control.

## Settings and quick access

| Settings: portrait | Quick access: portrait |
| --- | --- |
| ![Settings modal in portrait orientation](images/settings.png) | ![Quick access in portrait orientation](images/quick-access.png) |

| Settings: landscape | Quick access: landscape |
| --- | --- |
| ![Settings modal in landscape orientation](images/settings-modal-landscape.png) | ![Quick access in landscape orientation](images/quick-access-landscape.png) |

The two main modal headers use the compact ApexTX wordmark. Section titles and
touch targets remain consistent in both layouts, and the grids reflow without
clipping their labels.

## Regenerating the images

The gallery must stay tied to a tested build. Generate the source frames with:

```sh
mkdir -p build/nb4-gallery
NB4_SCREENSHOT_DIR="$PWD/build/nb4-gallery" \
  build/nb4-device/native/gtests-radio \
  --gtest_filter='Nb4Ux.RacingHomeRendersAllPalettesInBothOrientationsWithoutLeaking:Nb4Ux.AssignmentsAreReadableAndSaveOnlyWhenRequested:Nb4RacingUi.AllDestinationsAndEditorsInBothLanguagesAndOrientations'
```

The tests write lossless PPM framebuffer captures. Convert only the selected
English `ApexTX Dark` frames to the PNG files in `docs/nb4/images` and inspect
them before committing.
