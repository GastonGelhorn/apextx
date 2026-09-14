/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
/* Keep upstream's fixed TLSF pool and monitor APIs. Intercept allocation
 * failures even in LVGL constructors which do not check their return value. */
#define lv_mem_alloc nb4_lv_mem_alloc_raw
#define lv_mem_realloc nb4_lv_mem_realloc_raw
#include "../../thirdparty/lvgl/src/misc/lv_mem.c"
#undef lv_mem_alloc
#undef lv_mem_realloc
#include "nb4_health.h"

void* lv_mem_alloc(size_t size)
{
  void* memory = nb4_lv_mem_alloc_raw(size);
  if (!memory && size) nb4UiAssert(__FILE__, __LINE__);
  return memory;
}

void* lv_mem_realloc(void* memory, size_t size)
{
  void* resized = nb4_lv_mem_realloc_raw(memory, size);
  if (!resized && size) nb4UiAssert(__FILE__, __LINE__);
  return resized;
}
