/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#if defined(RADIO_NB4_FAMILY)
void nb4OpenBluetooth();
const char* nb4CreateCarFromTemplate(const char* path);
const char* nb4SavePersonalTemplate(const char* name, bool overwrite = false);
bool nb4TemplateNameValid(const char* name);
#endif
