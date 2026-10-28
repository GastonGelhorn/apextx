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
#endif
