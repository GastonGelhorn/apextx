/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "nb4_lua_alloc.h"
#if defined(RADIO_NB4_FAMILY)
#include <cstdlib>
namespace { size_t used = 0; }
size_t nb4LuaHeapUsed() { return used; }
// Lua's three states run on the UI task. Share a hard budget so a script's
// allocation fails inside Lua's protected call, before exhausting the UI heap.
void* nb4LuaAlloc(void*, void* pointer, size_t oldSize, size_t newSize)
{
  if (!pointer) oldSize = 0; // Lua supplies a type tag for a new allocation
  if (!newSize) { free(pointer); used -= oldSize; return nullptr; }
  if (newSize > NB4_LUA_HEAP_LIMIT - (used - oldSize)) return nullptr;
  void* next = realloc(pointer, newSize);
  if (next) used = used - oldSize + newSize;
  return next;
}
#endif
