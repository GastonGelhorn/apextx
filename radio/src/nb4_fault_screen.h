/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdint.h>

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)

// Puts a red fault screen on the panel without going through LVGL, without
// allocating and without taking a lock, so it can be drawn from the mixer task
// when the interface is the thing that has failed.
//
// The panel is scanned continuously by the display controller straight out of
// SDRAM, so anything that can write to that memory can put a picture on the
// screen. That is the whole reason this works when nothing else in the
// interface does.
//
// The message tells the driver to bring the car in and restart the radio, and
// says plainly that steering and throttle keep working, because they do: an
// interface fault leaves the mixer and the pulses untouched.
//
// `fault` is the recorded Nb4HealthRecord fault, shown so a report can name it.
//
// Show paints the whole screen and presents it in one call. Use it from the
// interface task on a path that is about to stop it for good: the task's time
// is worth nothing by then, and the mixer preempts it anyway.
void nb4FaultScreenShow(uint32_t fault);

// Request and Step are for the mixer task, which detected the stall but must
// not spend five milliseconds filling a framebuffer: that would delay a
// channel frame. Request only records the intent; each Step paints a bounded
// slice and presents the screen once it is complete. Step returns true while
// there is more to do, and does nothing once the screen has been presented.
void nb4FaultScreenRequest(uint32_t fault);
bool nb4FaultScreenStep();

#endif
