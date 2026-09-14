// SPDX-License-Identifier: GPL-2.0-only
#include "nb4_update.h"
#include <string.h>

namespace nb4update {
uint32_t crc32(const void* data, uint32_t size)
{
  static const uint32_t table[] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
  };
  auto bytes = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xffffffff;
  while (size--) {
    crc ^= *bytes++;
    crc = (crc >> 4) ^ table[crc & 15];
    crc = (crc >> 4) ^ table[crc & 15];
  }
  return ~crc;
}

bool validManifest(const Manifest& m)
{
  return !memcmp(m.magic, "APXNB4U1", 8) && m.target == 1 && m.version == 1 &&
         m.address == AppAddress && m.size >= 8 && m.size <= MaxApplication &&
         !(m.size & 3) && crc32(&m, 28) == m.headerCrc;
}

bool validApplication(const Manifest& m, const uint8_t* app)
{
  if (!validManifest(m)) return false;
  uint32_t vectors[2];
  memcpy(vectors, app, sizeof(vectors));
  bool stack = (vectors[0] > 0x10000000 && vectors[0] <= 0x1000fff0) ||
               (vectors[0] > 0x20000000 && vectors[0] <= 0x20030000);
  return stack && !(vectors[0] & 7) && (vectors[1] & 1) &&
         vectors[1] >= AppAddress && vectors[1] < AppAddress + m.size &&
         crc32(app, m.size) == m.crc;
}

bool Updater::erase(uint32_t address) const
{
  // DfuSe erase is a no-op on staging RAM. Mass erase and flash addresses fail.
  return current != State::Queued && current != State::Installing &&
         current != State::Restart && address >= StageAddress &&
         address < StageAddress + StageCapacity;
}

bool Updater::write(uint32_t address, const uint8_t* data, uint32_t size)
{
  if (address == ControlAddress && size == 8) {
    if (!memcmp(data, "APXSTART", 8) && current == State::Receiving &&
        validManifest(manifest) && received == HeaderSize + manifest.size) {
      current = State::Queued;
      return true;
    }
    if (!memcmp(data, "APXRESET", 8) && current == State::Done) {
      current = State::Restart;
      return true;
    }
    return false;
  }
  if (current == State::Queued || current == State::Installing ||
      current == State::Restart || address < StageAddress || !size ||
      size > StageCapacity || address - StageAddress > StageCapacity - size)
    return false;

  if (address == StageAddress) {
    if (size < HeaderSize) return false;
    Manifest candidate;
    memcpy(&candidate, data, HeaderSize);
    if (!validManifest(candidate)) return false;
    manifest = candidate;
    received = progress = error = 0;
    current = State::Receiving;
  }
  if (current != State::Receiving || address - StageAddress != received ||
      !validManifest(manifest) || size > HeaderSize + manifest.size - received)
    return false;
  memcpy(staging + received, data, size);
  received += size;
  progress = received * 100 / (HeaderSize + manifest.size);
  return true;
}

bool Updater::read(uint32_t address, uint8_t* data, uint32_t size) const
{
  if (address == ControlAddress && size == sizeof(Status)) {
    Status status = {{'A','P','X','S','T','A','T','1'}, uint32_t(current),
                     progress, manifest.size, manifest.crc, error, received};
    memcpy(data, &status, size);
    return true;
  }
  if (address < StageAddress || size > received ||
      address - StageAddress > received - size) return false;
  memcpy(data, staging + address - StageAddress, size);
  return true;
}

void Updater::install(const etx_flash_driver_t& flash, const uint8_t* application,
                      void (*service)())
{
  if (current != State::Queued) return;
  current = State::Installing;
  progress = 0;
  service();
  const uint8_t* payload = staging + HeaderSize;
  // Recheck on the radio, independently of the host readback.
  if (!validApplication(manifest, payload)) { fail(1); return; }

  // Erase only the application and its manifest, leaving the bootloader intact.
  // Boot validates the complete CRC, including after a partially erased sector.
  for (uint32_t addr = AppAddress; addr <= ManifestAddress;) {
    service();
    if (flash.erase_sector(addr)) { fail(2); return; }
    uint32_t step = flash.get_sector_size(flash.get_sector(addr));
    if (!step || step > ManifestAddress + HeaderSize - addr) { fail(2); return; }
    addr += step;
    progress = (addr - AppAddress) * 25 / (MaxApplication + HeaderSize);
  }

  // Leave the vectors erased until all other bytes have been read back.
  for (uint32_t offset = 8; offset < manifest.size;) {
    uint32_t count = manifest.size - offset;
    if (count > 1024) count = 1024;
    service();
    if (flash.program(AppAddress + offset,
                      const_cast<uint8_t*>(payload + offset), count) ||
        memcmp(application + offset, payload + offset, count)) {
      fail(3); return;
    }
    offset += count;
    progress = 25 + offset * 70 / manifest.size;
  }
  if (flash.program(ManifestAddress, &manifest, HeaderSize) ||
      memcmp(application + MaxApplication, &manifest, HeaderSize) ||
      flash.program(AppAddress + 4, const_cast<uint8_t*>(payload + 4), 4) ||
      flash.program(AppAddress, const_cast<uint8_t*>(payload), 4) ||
      !validApplication(manifest, application)) { fail(4); return; }
  progress = 100;
  current = State::Done;
}
}
