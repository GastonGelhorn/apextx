/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#if defined(RADIO_NB4_FAMILY)
// Validation precedes deserialization, so incompatible files remain untouched.
const char* nb4ValidateModelFile(const char* path);
const char* nb4ValidateModelText(const char* text, size_t length);
bool nb4ModelBlocked();
const char* nb4ModelCompatibilityIssue();
void nb4AcceptNewCarModel(); // only after creating defaults with a fresh filename
#else
inline bool nb4ModelBlocked() { return false; }
#endif
