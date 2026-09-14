// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>
#include "hal/flash_driver.h"

namespace nb4update {
constexpr uint32_t AppAddress = 0x08020000;
constexpr uint32_t ManifestAddress = 0x081fffe0;
constexpr uint32_t MaxApplication = ManifestAddress - AppAddress;
constexpr uint32_t StageAddress = 0xc0200000; // USB virtual address, never dereferenced
constexpr uint32_t ControlAddress = 0xc03f0000;
constexpr uint32_t HeaderSize = 32;
constexpr uint32_t StageCapacity = HeaderSize + MaxApplication;

struct Manifest {
  char magic[8];
  uint32_t target, address, size, crc, version, headerCrc;
};
static_assert(sizeof(Manifest) == HeaderSize, "Update protocol layout");

enum class State : uint32_t { Receiving, Queued, Installing, Done, Error, Restart };
struct Status {
  char magic[8];
  uint32_t state, progress, size, crc, error, received;
};
static_assert(sizeof(Status) == 32, "Update status layout");

uint32_t crc32(const void* data, uint32_t size);
bool validManifest(const Manifest& manifest);
bool validApplication(const Manifest& manifest, const uint8_t* application);

// USB callbacks only stage bytes or queue an operation. Flash writes run in
// the main loop. The transport has no access to raw flash, OTP or option bytes.
class Updater {
 public:
  explicit Updater(uint8_t* staging) : staging(staging) {}
  bool write(uint32_t address, const uint8_t* data, uint32_t size);
  bool read(uint32_t address, uint8_t* data, uint32_t size) const;
  bool erase(uint32_t address) const;
  void install(const etx_flash_driver_t& flash, const uint8_t* application,
               void (*service)());
  State state() const { return current; }
  uint32_t percent() const { return progress; }
 private:
  uint8_t* staging;
  volatile State current = State::Receiving;
  volatile uint32_t received = 0, progress = 0, error = 0;
  Manifest manifest{};
  void fail(uint32_t code) { error = code; current = State::Error; }
};
}

bool nb4BootApplicationValid();
void bootloaderNB4Update();
void bootloaderDrawNB4Update(const char* status, unsigned progress,
                            bool mayExit);
