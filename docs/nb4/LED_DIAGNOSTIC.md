# NB4 / NB4+ LED diagnostic D1

This is an opt-in bench diagnostic for issue #19 / PR #20, **not a confirmed
LED fix**. Normal firmware has it disabled. No radio/model preference is saved
by the test. Run with the car/receiver switched off.

## Download and install

The PR's **ApexTX CI** run publishes four distinct artifacts:

- `apextx-nb4-standard-<commit>`: normal original-NB4 firmware.
- `apextx-nb4plus-standard-<commit>`: normal NB4+ firmware.
- `apextx-nb4-led-diagnostic-<commit>`: original-NB4 diagnostic.
- `apextx-nb4plus-led-diagnostic-<commit>`: **NB4+ diagnostic**.

Use the artifact for your radio. Extract it, check `BUILD.json` for the commit,
model, `led-diagnostic` variant and SHA-256, and select its **firmware.bin** in
ApexTX Flasher's normal **Update** mode. The file is 2,097,152 bytes. This is not
an Install/Recovery operation and does not replace the radio's bootloader.
GitHub may require sign-in to download Actions artifacts.

For a local build, add `-DNB4_LED_DIAGNOSTIC=ON` to the usual configuration;
NB4+ additionally needs `-DNB4_PLUS_HW=ON` with `-DPCBREV=NB4`. The full image is
`<directory passed to cmake -B>/arm-none-eabi/firmware.bin`. Run
`python3 tools/apextx-updater.py <image> --check` and check that it says NB4+ for
an NB4+ image. Neither `bootloader.bin` nor `firmware.elf` is an update image.

## Test

1. Unplug USB and leave the base pack off for the first run. Cold-start the radio.
2. Open **Menu > Display & interface > Lights > LED diagnostic D1** (the Lights
   tile may also be in Quick access). The diagnostic UI is intentionally English.
3. Leave **Quiet** off for the first run. Press **Run 50%**. Observe the
   physical LEDs for all three six-second phases:
   **A: LTDC ON**, **B: LTDC OFF**, **C: LTDC ON**. During B the screen may go
   blank or freeze; it returns automatically. The backlight setting and panel
   power stay unchanged. Closing the dialog cancels the test and restores LTDC.
4. The fixed pattern is index 0 red, 1 green, 2 blue, 3 white. On the tested
   right-to-left strip, physical left-to-right is **white, blue, green, red**.
   Every phase resends it at 20 Hz. No phantom pixel is added. Charging and the
   selected LED mode do not override the pattern while the test is running.
5. Photograph the result counters after the screen returns. Repeat with
   **Run 100%**. If useful, repeat with the base pack on and label that result.
6. Switch **Quiet** on and run again at the same level. While a quiet run
   lasts, the internal RF module is de-initialised and unpowered, the audio
   amplifier is muted, the haptic motor is off and ADC sampling is paused
   (sticks, keys and the battery reading freeze; the touch screen keeps
   working). All of it resumes by itself when the run ends or the dialog is
   closed; the module rebinds without a restart. The report's first line says
   whether the run was quiet.
7. Close the dialog to return to the normal settings. After the test, the
   ordinary LED policy resumes. Install the matching `standard` artifact to
   remove the diagnostic completely.

The quiet run separates the LED waveform from the rest of the board. If the
last two LEDs are clean only in the quiet run, something the normal firmware
powers (first suspect: the RF module and its supply bursts; then audio and
haptic) disturbs them, and the next step is switching those back on one at a
time. If they fail in the quiet run too, the cause is in the LED line or its
supply and not in those peripherals.

All phases redraw the same small coloured panel at 10 Hz. Only LTDC scanout
(its continuous framebuffer reads and line interrupts) is disabled in B.
The CPU/GUI rendering, SDRAM writes, LED timer, RF and backlight remain active.
This isolates scanout; it does **not** isolate every display-related load or
electrical effect. Simulator results cannot validate the LED waveform.

## Report

Include `BUILD.json`'s commit/model/hash, 50% or 100%, quiet on or off,
battery/base/USB state, and for each phase A/B/C:

- Actual left-to-right colours, which LEDs flicker, and whether they recover.
- TX (transfers started), TC (completed), TE (transfer errors), DME (direct-mode
  errors), FE (FIFO errors), TO (transfer still busy after 20 ms).

The result also shows the DMA buffer address and the last raw error flags/NDTR.
Counts can differ by one at phase boundaries because a transfer may be in
flight. Zero DMA errors **does not prove correct pulse timing or signal levels**:
a transfer can complete without an error while a PWM update arrives too late.
If the colours fail only with scanout ON, that is evidence to investigate bus
load or a display-related electrical interaction; it is not proof of either.
If they fail in all phases, this test has not implicated scanout.
