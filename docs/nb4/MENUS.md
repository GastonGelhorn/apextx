# ApexTX menus and customizable Home

## Navigation

Menu uses one car-oriented route catalogue. Steering and Throttle/brake
open their tabbed editors directly; Advanced setup is inside Current car. Each legacy editor
opens on its own, without unrelated tabs. Back returns to its parent; category
grids retain their scroll and focus. Editors also have a visible Back button.

There is no standalone curve-library destination. Select a custom curve in
an input or mix, then use Create/Edit, or open it from the axis response page.
Shared curves show the inputs/mixes that will change before editing. Curve IDs
still belong to the current model, not to a global vehicle-independent library.

System > Help lists categories and options with short explanations and a link
to the setting. Opening a setting from that index dismisses the help trail;
Back returns to the menu, not an obsolete help sheet. Editors share a ? control
in the header; Menu, Quick access and category grids deliberately omit it.
System > Help remains available as the complete index. Contextual help returns to its owner,
without an extra link that would open a duplicate editor. Receiver field help
uses this same header, not a separate button in the form. Decorative header
icons are passive; only the explicit Back control navigates back.
Settings/help dialogs also ignore taps on the backdrop: a Home icon visible
behind a short dialog is not an implicit Back target.
Unavailable settings explain why; unsupported Bluetooth is not offered.

## Current car, startup checks and presets

Current car > Car details replaces the former General page: only the name and labels
remain. Receiver, timers, trims and safety keep their own destinations. Less
frequent feature-visibility and input-filter/centre-beep preferences live in
Current car > Advanced setup.

Startup checks explains each startup check directly below its control: notes/checklist,
interactive checklist, throttle warning, optional custom throttle position,
switch positions and available analogue-control positions. Wrapped labels grow
their rows instead of clipping into the descriptions, including in Spanish.

Vehicle presets replaces Starting point. These are optional electric/nitro
driving defaults for initial setup, not templates or a required setup step.
The page describes both presets and confirms before replacing brake, ABS,
steering-speed and engine settings. Channels, curves, trims, race configuration,
calibration and screens are preserved. Changing only the vehicle type is in
Throttle/brake > Engine and does not apply a preset. Lap input, spoken lap
announcements and target lap count are in Race > Race setup.

## Display & interface

- Brightness: radio-wide backlight preferences.
- Appearance: built-in palette, accent, orientation, and external themes.
  External themes do not repeat the built-in palettes.
- Screen layouts: Home, each additional screen, and Add screen. The selected list entry,
  not the currently visible main view, determines which screen is edited.
- Top bar: model-specific widgets. ApexTX Racing keeps its own fixed status
  indicators; other layouts decide whether the configurable top bar is shown.

Home uses the selectable **ApexTX Racing** layout with four editable zones:
steering, throttle/brake, timer, and statistics. Replace/remove their widgets
through Configure widgets, or choose another layout. Racing keeps navigation,
RF status and transmitter/receiver battery indicators outside the editable zones.
The widget selector uses localized names; stored IDs remain language-independent.
While editing zones, navigation dialogs are hidden so they cannot cover the
widgets. Back restores the previous screen and menu stack. The top-bar editor
temporarily shows the configurable bar even when Racing normally hides it.

Restore ApexTX design asks for confirmation and replaces only the selected
screen's layout and widgets. It does not reset servo settings, curves, racing
configuration, other screens, the top bar, calibration, or radio appearance.

- Keys and navigation: navigation-only view of the shared physical-button editor.
  Bindings are per car; assigning one replaces that button's previous function.
- Quick access setup: global shortcuts, not model-specific bindings.
- Lights: global off/fixed/breathing/battery-state LED appearance. Charging
  temporarily overrides the chosen colour; this is not a telemetry-alert editor.

Pages show a persistent scope strip: Whole radio, Car: <name>, Car collection,
or Radio and car data for mixed pages. Layout/widget sub-editors inherit the
car scope, while appearance/brightness/LED/shortcut pages declare radio scope.

## Quick access

Display & interface > Quick access setup manages eight global slots. Choose,
move up/down, remove, or restore the eight defaults. Empty slots stay empty after
restart. Duplicate destinations cannot be assigned twice. Entries that directly
perform destructive/reboot actions are excluded. Normal navigation pages can
still contain their own confirmed actions.

Every destination keeps its permanent Menu location. Quick access only points
to that same route/editor/state. Adding or removing a shortcut never hides a
category or rebuilds an open Menu grid, so focus and scroll remain stable.
Back returns to the actual entry point (Quick access or the Menu category).
Steering and Throttle/brake each open one tabbed editor, never a redundant grid
of individual tabs. Unsupported hardware functions are distinct from disabled
features: unsupported Bluetooth is absent, while disabled telemetry/logic/etc.
remain discoverable with an explanation.

The radio YAML stores `nb4QuickAccessVersion: 2` and indexed
`nb4QuickAccess/<slot>/val` unsigned 32-bit IDs (FNV-1a of canonical ASCII
paths). Route IDs remain stable. Presentation category metadata moves USB to
System, navigation/shortcut setup to Display & interface, and Lights to that
same category without changing their legacy paths or saved IDs. Missing/invalid IDs and duplicates are normalized; version 0 installs the
defaults. The old cards and native shortcut fields are retained.
Version 1 axis-tab shortcuts migrate to their canonical axis editor. If that
creates a duplicate, the later slot becomes empty without reordering the others.
Restoring defaults installs eight distinct destinations: Steering, Throttle/brake,
Race history, Trims, Timers/laps, Telemetry view, Pit and Input/output monitor.
Pit replaces Receiver only on explicit reset or first setup; existing saved
Receiver shortcuts are preserved. The storage format remains version 2:
changing shortcut navigation semantics does not require rewriting the schema.

## Templates

My cars > Templates browses YAML templates and their optional same-name .txt
description. Create car validates and stages the template before changing the
active model. Copy, parse or promotion failures leave the previous car active,
including its dynamic screen/widget data.

Save current car as template validates the name and writes to the personal
template folder. An existing name requires explicit overwrite confirmation.
An overwritten template is retained as .previous. Only personal templates offer
Delete, with confirmation; built-in templates are not deletable from this page.
Storage errors are shown, not silently treated as success.

## Controls and racing

Controls separates Trims, Button functions, Channel assignment, Switch response
and Input/output monitor. Channel assignment selects the steering/throttle
receiver outputs and their reversal; Advanced > Outputs adds channel limits,
centre and other servo settings using the same output data. Switch response is
the radio-wide switch delay (and rotary navigation direction where supported).
Navigation-only bindings live in Display & interface. The full button-function
editor remains available and shares the same per-car bindings.

Race order is Timers/laps, Pit, Race history, Statistics, Race setup, Timers,
then a visual separator and Session resets. Timer editors return to their list;
session resets ask for confirmation. Statistics explains its mixed radio/session
usage counters, three car timers and throttle trace. Its counter reset now asks
for confirmation and does not delete race history. Pit uses Timer 2.

## Race results

A finished run offers View result only after its own save receipt has a record
ID. Pending/saving/failure messages are shown before that; failure offers retry.
The result uses the same detail page as History, including zero-lap sessions.
The session token and saved record ID prevent a previous run from being opened
as the result of a failed or pending save.

## Manual backup and reset

System contains USB directly, without a singleton Connection category.
Manual backup provides copy/restore instructions and Open files (the same file
browser as System > Storage); it does not create an automated backup.
Reset settings is a separate page for radio preferences, this car or both, each
with a scoped confirmation. Other saved cars and calibration are preserved.
Vehicle presets, templates and screen-design restoration retain their distinct
purposes and confirmations. Enabling Variables now overrides the current car,
not the radio-wide default for other cars.

## Compatibility and migration

Model `nb4ScreenVersion: 1` enables the editable Home. Before replacing the
formerly hidden screen 0, the unmodified model file is backed up as
`<model>.yml.pre-apextx-home` through a temporary file and rename. Failed backup
or promotion defers migration and retains the legacy Home. A conflicting backup
also defers migration; rename that backup outside the radio before retrying.
Additional screens and model settings are untouched. Later loads do not reapply
the default widgets.
A newer unsupported Home version fails the compatibility check.

Unknown saved layout IDs keep a navigable fallback without overwriting stored
data. Reset remains an explicit action. Recovery/model-incompatibility guards
still protect layouts and the top bar, while radio appearance/brightness/lights,
help, update and recovery remain accessible.

## Verification

Run the NB4 native suite and firmware build described in [VALIDATION.md](VALIDATION.md).
The tests include route identity/availability, EN/ES rendering, both orientations,
physical Back through the simulated key driver, per-screen editors, widget
restoration, YAML round trips, migration backup failures, template write failures
and session-specific result receipts.

Simulator rendering is not a substitute for the real-radio checklist. Confirm
touch accuracy, physical focus/scroll behavior, storage removal and restart/car
switch persistence on hardware before release. No radio is flashed by the tests.
