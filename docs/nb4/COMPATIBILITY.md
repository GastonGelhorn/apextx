# Noble family compatibility

This document separates source compatibility, successful compilation, and
hardware validation. A firmware image is safe to install only on the exact
target named by its release.

## Current status

| Transmitter | Repository target | Status | What remains |
| --- | --- | --- | --- |
| Noble NB4 | `PCB=PL18`, `PCBREV=NB4` | Supported and tested on hardware | Continue release validation on the original NB4 |
| Noble NB4+ | `PCB=PL18`, `PCBREV=NB4P` | Compiles; experimental port candidate | Factory backups and complete testing on a real NB4+ |
| Noble NB4 Pro | None | Not supported | New board target, hardware map, drivers, and device validation |
| Noble NB4 Pro+ | None | Not supported | New board target, hardware map, drivers, charge-controller handling, and device validation |

`NB4P` is the EdgeTX target name for the Noble NB4+. Upstream EdgeTX lists the
radio in its target catalogue and nightly build matrix, and a full release ARM
build also completes in this repository. That indicates much of the common
PL18 and car interface code can be reused, but it says nothing about whether
this fork's generated image is safe to flash.

Never install an NB4 image on an NB4+, Pro, or Pro+, or install one model's
official updater on another model.

The compile-only NB4+ check used on 2026-09-13 was:

```sh
cmake -S . -B build/nb4plus-check \
  -DPCB=PL18 -DPCBREV=NB4P -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
  -DTRANSLATIONS=EN -DDISABLE_COMPANION=ON
cmake --build build/nb4plus-check --target firmware-size -j8
```

This command is maintained as a build check. Its output is not an installable
release from this project until NB4+ hardware qualification is complete.

## Why the NB4+ is the closest candidate

FlySky lists the NB4 and NB4+ with the same 3.5-inch 320 x 480 logical
resolution, AFHDS3 protocol, 4096 channel resolution, and 520 g weight. The
NB4+ changes the panel to IPS, uses USB-C and a 4.35 V battery system, and has
its own product identity.

The source already accounts for several board differences. The NB4+ target
uses a different key driver, haptic output, battery scaling, and a USART3
internal-RF route. The original NB4 uses the USART6 route this firmware ships.
Those differences are enough to make cross-flashing unsafe.

A first NB4+ hardware qualification must verify:

1. Factory MCU, OTP, option-byte, calibration, and external-flash backups.
2. DFU identity, flash layout, bootloader entry, power hold, and shutdown.
3. Display controller, orientation, touch controller, and backlight.
4. Every key, trim, switch, analogue input, LED, haptic output, and battery
   reading.
5. USB serial, storage, joystick, charging and cable-disconnect behaviour.
6. AFHDS3 startup, bind persistence, failsafe, receiver voltage, link quality,
   and telemetry.

## Why the Pro and Pro+ need new ports

The Noble Pro and Pro+ use the same display resolution and AFHDS3 family, so
the car interface, model logic, telemetry parser, and racing features should be
portable. They are different physical transmitters with additional controls
and different power systems. FlySky lists wireless charging for the Pro and a
separate charge-control firmware for the Pro+.

The official update packages use a related ARM updater structure, which is
consistent with a shared product family. It does not reveal or prove GPIO,
ADC, display, touch, RF, flash, power, or charging compatibility. Supporting
either Pro model therefore requires a separate target and access to the real
device for read-only discovery and staged validation.

## Original NB4 versus official Pro+ functions

The accurate project claim is that ApexTX brings many Pro-style workflows
and several original racing features to the first-generation NB4. It does not
turn the NB4 into a Pro+ and does not add hardware that the transmitter lacks.

| Capability | ApexTX on NB4 | Official Pro+ reference |
| --- | --- | --- |
| Steering and throttle travel, reverse, trim, curves, mixes, and failsafe | Available | Available |
| AFHDS3 bind, receiver telemetry, link quality, and receiver voltage | Available for qualified receivers and reported sensors | Available |
| Physical-control assignment | Available, including assign-by-press, navigation, trims, timer, and lap actions | Available, including copying assignments between models |
| Race timer, lap marking, best/last/delta, undo, pit estimate, and persistent history | Available | The official firmware provides timers and race-oriented controls; this workflow is project-specific |
| Custom dark interface, portrait/landscape layouts, English and Spanish UI/audio | Available | Different official interface and language packages |
| Temperature, RPM, pack voltage, and other telemetry | Displayed when a compatible receiver or sensor reports the value | Available with supported FlySky sensors and adapters |
| Channels | 8 | 2 ultra-fast, 4, 6, 8, 10, 12, or 18 |
| Receiver topology | One receiver | One, dual, or multiple receiver modes |
| Anti-interference modulation selection | Not implemented | Available |
| i-BUS2-IN/S.BUS-IN receiver redundancy | Not implemented | Available |
| Start mode and channel lock | Not implemented | Available |
| Lamp and FlySky telemetry-adapter integration | Not implemented as dedicated features | Available |
| Gear-ratio and motor-pole calculations; official sensor data recording | Not implemented as dedicated features | Available |
| Channel voice broadcast | General EdgeTX audio and special functions are available; the Pro+ workflow is not reproduced | Available |
| Model capacity | 10 | 18 in firmware 1.0.23 |
| Pro/Pro+ controls, enclosure, USB-C, charging hardware, and radio-specific accessories | Hardware dependent and unchanged | Native Pro+ hardware |

The AFHDS3 transport can encode up to 18 channels, but the maintained Noble
build currently sets `MAX_OUTPUT_CHANNELS` to 8 for the whole NB4 family. More
channels need model-format, interface, RF-mode, receiver, failsafe, timing, and
hardware tests; changing the constant alone would not implement the Pro+
channel modes.

## Official references

- [EdgeTX target catalogue](https://github.com/EdgeTX/edgetx/blob/main/fw.json)
- [EdgeTX compilation target documentation](https://github.com/EdgeTX/edgetx/blob/main/docs/building/compilation-options.md)
- [EdgeTX NB4+ hardware notes](https://github.com/EdgeTX/edgetx/wiki/Flysky-NB4--Hardware-Mod-for-Complete-EdgeTX-Support)
- [Noble NB4 specifications](https://www.flysky-cn.com/nb4-canshu)
- [Noble NB4+ specifications](https://www.flysky-cn.com/nb4plus-specifications-2-1)
- [Noble NB4+ downloads and firmware](https://www.flysky-cn.com/noble-nb4plus-downloads)
- [Noble NB4+ firmware release notes](https://www.flysky-cn.com/s/Release_Notes_For_Noble_NB4_Transmitter_Firmware-20250909.pdf)
- [Noble Pro specifications](https://www.flysky-cn.com/paladin-evspecifications-2)
- [Noble Pro downloads and firmware](https://www.flysky-cn.com/noble-pro-download-1-1)
- [Noble Pro firmware release notes](https://www.flysky-cn.com/s/Release_Notes_For_Noble_NB4_Pro_Transmitter_Firmware-20250512.pdf)
- [Noble Pro+ specifications](https://www.flysky-cn.com/noble-nb4-pro-specifications-copy)
- [Noble Pro+ downloads, transmitter firmware, and charge-control firmware](https://www.flysky-cn.com/noble-nb4-pro-downloads)
- [Noble Pro+ firmware release notes](https://www.flysky-cn.com/s/Release_Notes_for_Noble_NB4_Pro_transmitter_firmware-20250918.pdf)

No FlySky firmware, resources or other proprietary material is redistributed
by this repository. The links above are FlySky's own public product pages,
cited for the published specifications.
