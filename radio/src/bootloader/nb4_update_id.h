// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Which hardware an ApexTX build is for, in the three places that have to
// agree on it: the marker the firmware carries in its own image, the update
// manifest target the bootloader accepts, and the name the bootloader gives
// itself over USB in update mode.
//
// The bootloader is built from the same tree and with the same options as the
// firmware it installs, so it names the hardware it was built for and refuses
// any other. The host updater reads that name before it sends anything.
//
// Plain C, because the USB descriptors are C.

#define NB4_UPDATE_TARGET_NB4 1
#define NB4_UPDATE_TARGET_NB4PLUS 2

#if defined(RADIO_NB4PLUS_HW)
#define NB4_UPDATE_TARGET NB4_UPDATE_TARGET_NB4PLUS
#define NB4_UPDATE_PRODUCT "ApexTX NB4+ Update"
#define NB4_HARDWARE_ID "nb4plus"
#else
#define NB4_UPDATE_TARGET NB4_UPDATE_TARGET_NB4
#define NB4_UPDATE_PRODUCT "ApexTX NB4 Update"
#define NB4_HARDWARE_ID "nb4"
#endif

// The DfuSe memory descriptor. Staging is 15 x 128 KiB of SDRAM.
#define NB4_UPDATE_INTERFACE "@" NB4_UPDATE_PRODUCT " /0xC0200000/015*128Kg"
