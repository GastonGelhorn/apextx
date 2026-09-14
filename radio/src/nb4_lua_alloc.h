/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#if defined(RADIO_NB4_FAMILY)
constexpr size_t NB4_LUA_HEAP_LIMIT = 1024 * 1024;
void* nb4LuaAlloc(void*, void* pointer, size_t oldSize, size_t newSize);
size_t nb4LuaHeapUsed();
#endif
