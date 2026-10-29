/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#if defined(RADIO_NB4)
// Explicit radio action; ordinary serial/debug sessions are never intercepted.
bool nb4MaintenanceEnable();
bool nb4MaintenanceEnabled();
void nb4MaintenanceStartUsb();
void nb4MaintenanceStopUsb();
void nb4MaintenanceService();
size_t nb4MaintenanceInventory(char* output, size_t capacity);
// Restart into the bootloader's update (DFU) mode. Stops pulses first.
void nb4RebootIntoUpdateMode();
#if defined(SIMU)
size_t nb4MaintenanceTestExchange(uint8_t command, const void* payload, size_t length, char* output, size_t capacity);
bool nb4MaintenanceTestRebootRequested();
#endif
#endif
