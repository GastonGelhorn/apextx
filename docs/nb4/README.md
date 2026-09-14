# Noble NB4 firmware

This tree contains the maintained EdgeTX port for the original FlySky Noble NB4.
The target is `PCB=PL18`, `PCBREV=NB4` and uses the recovered AFHDS3 hardware
profile for USART6.

The interface supports English and Spanish. Source code, comments, tests, build
tools, and engineering documentation are written in English; Spanish text is
kept only as localized content shown by the radio.

The project version is read from `APEXTX_VERSION`. Development builds identify
themselves as ApexTX development images and retain the EdgeTX 2.12.4 base
version in their firmware metadata.

The NB4 additions are grouped around these areas:

- `radio/src/nb4_*`: vehicle state, axis processing, racing, pit, health, LEDs,
  and palettes.
- `radio/src/gui/colorlcd`: the racing home, car setup pages, assignments, and
  responsive controls.
- `radio/src/pulses/afhds3*`: the NB4 AFHDS3 transport and receiver state.
- `radio/src/targets/pl18`: the original NB4 hardware definition and drivers.
- `tools/nb4-*`: build validation, packaging, fonts, and DFU flashing.

See [GALLERY.md](GALLERY.md) for simulator-rendered interface captures and
[COMPATIBILITY.md](COMPATIBILITY.md) for the verified status of each Noble
model. Use [FLASH.md](FLASH.md) for the first installation from FlySky firmware
and [UPDATE.md](UPDATE.md) for subsequent ApexTX application updates.
[BUILD.md](BUILD.md), [HARDWARE.md](HARDWARE.md),
and [VALIDATION.md](VALIDATION.md) contain the maintained engineering
workflows.
