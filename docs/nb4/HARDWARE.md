# Original Noble NB4 hardware

The target uses an STM32F429 with 2 MiB internal flash, external SDRAM, a
320 x 480 display, and an 8 MiB GD25Q64 SPI NOR device.

| Function | Hardware route |
|---|---|
| Steering / throttle ADC | PA2 channel 2 / PA3 channel 3 |
| Auxiliary ADC | PC2 channel 12 / PA7 channel 7 |
| Battery ADC | PC5 channel 15 |
| Touch controller | FT6236, PB7/PB8, interrupt PB9, reset PB12 |
| SPI NOR | SPI6, PG13/PG14/PG12, chip select PG6 |
| Power button / hold | PI11 active low / PI14 |
| AFHDS3 UART | USART6, PC6 TX, PC7 RX, AF8, 1.5 Mbaud 8N1 |
| AFHDS3 power | enabled PD11=1, PI8=1; disabled PD11=0, PI8=1 |
| Haptic motor | PA8, TIM1 channel 1 |
| Addressable LED | PH12, TIM5 channel 3, four WS2812 pixels |

The AFHDS3 framing is addressless SLIP with a complement checksum. The selected
route and electrical sequence are recorded in `rf/qualification.json`.

## SPI NOR layout

The custom firmware uses the complete GD25Q64 for its filesystem. Archive the
original unit-specific factory calibration, stock configuration, internal MCU
flash, option bytes, and OTP snapshot in a private location before reclaiming
the original NOR region. Device backups must not be committed to this
repository.

| Address range | Purpose |
|---|---|
| `0x000000-0x7FFFFF` | EdgeTX FrFTL and FAT filesystem, 8 MiB physical |

FrFTL reserves pages for translation tables and wear management, leaving
7.875 MiB of logical storage. The filesystem uses 512-byte clusters so the
English and Spanish system voice packs can coexist with radio settings. The
factory and retired custom regions are no longer read by the firmware and have
been reclaimed. Changing the filesystem layout requires a filesystem backup,
format, and restore.

The board has no dedicated VBUS input. While the USB device stack is active,
host SOF frames provide the authoritative cable signal. After frames stop, the
firmware synchronizes mass storage, keeps the battery power latch asserted, and
uses a controlled reset to return ownership of the NOR to the application.
