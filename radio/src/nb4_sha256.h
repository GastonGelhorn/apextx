/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace nb4 {
class Sha256 {
 public:
  Sha256();
  void update(const uint8_t*, size_t);
  void finish(uint8_t digest[32]);
 private:
  void block(const uint8_t*);
  uint32_t state[8];
  uint8_t pending[64]{};
  uint64_t bytes = 0;
  unsigned used = 0;
};
}
