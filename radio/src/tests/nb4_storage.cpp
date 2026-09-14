/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "gtests.h"

#if defined(RADIO_NB4_FAMILY)
#include "sdcard.h"

#if defined(RADIO_NB4)
#include "drivers/frftl.h"

#include <algorithm>
#include <array>
#include <vector>

namespace {

constexpr uint32_t kExpandedNorSize = SPI_FLASH_USABLE_SIZE;
constexpr uint32_t kFtlPageSize = 4096;
constexpr uint32_t kFtlBlockSize = 32768;
std::vector<uint8_t> ftlFlash;

bool ftlRangeValid(uint32_t address, uint32_t length)
{
  return address <= ftlFlash.size() &&
         length <= ftlFlash.size() - address;
}

bool ftlFlashRead(uint32_t address, uint8_t* buffer, uint32_t length)
{
  if (!ftlRangeValid(address, length)) return false;
  std::copy_n(ftlFlash.data() + address, length, buffer);
  return true;
}

bool ftlFlashProgram(uint32_t address, const uint8_t* buffer, uint32_t length)
{
  if (!ftlRangeValid(address, length)) return false;
  for (uint32_t i = 0; i < length; ++i)
    ftlFlash[address + i] &= buffer[i];
  return true;
}

bool ftlFlashErase(uint32_t address)
{
  if (!ftlRangeValid(address, kFtlPageSize)) return false;
  std::fill_n(ftlFlash.data() + address, kFtlPageSize, 0xff);
  return true;
}

bool ftlFlashBlockErase(uint32_t address)
{
  if (!ftlRangeValid(address, kFtlBlockSize)) return false;
  std::fill_n(ftlFlash.data() + address, kFtlBlockSize, 0xff);
  return true;
}

bool ftlFlashIsErased(uint32_t address)
{
  return ftlRangeValid(address, kFtlPageSize) &&
         std::all_of(ftlFlash.begin() + address,
                     ftlFlash.begin() + address + kFtlPageSize,
                     [](uint8_t value) { return value == 0xff; });
}

const FrFTLOps kFtlOps = {
    ftlFlashRead,
    ftlFlashProgram,
    ftlFlashErase,
    ftlFlashBlockErase,
    ftlFlashIsErased,
};

}  // namespace

TEST(Nb4Storage, ExpandedNorRegionPersistsItsFirstAndLastSectors)
{
  ftlFlash.assign(kExpandedNorSize, 0xff);
  FrFTL ftl = {};
  ASSERT_TRUE(ftlInitWithSize(&ftl, &kFtlOps, kExpandedNorSize));
  ASSERT_EQ(SPI_FLASH_RESERVED_BASE, 0x00000000u);
  ASSERT_EQ(STORAGE_FAT_CLUSTER_SIZE, 512u);
  ASSERT_EQ(ftl.physicalPageCount, 2048u);
  ASSERT_EQ(ftl.usableSectorCount, 16128u);

  std::array<uint8_t, 512> first = {};
  std::array<uint8_t, 512> last = {};
  std::fill(first.begin(), first.end(), 0x35);
  std::fill(last.begin(), last.end(), 0xca);
  ASSERT_TRUE(ftlWrite(&ftl, 0, 1, first.data()));
  ASSERT_TRUE(ftlWrite(&ftl, ftl.usableSectorCount - 1, 1, last.data()));
  ASSERT_TRUE(ftlSync(&ftl));
  ftlDeInit(&ftl);

  ASSERT_TRUE(ftlInitWithSize(&ftl, &kFtlOps, kExpandedNorSize));
  std::array<uint8_t, 512> actual = {};
  ASSERT_TRUE(ftlRead(&ftl, 0, actual.data()));
  EXPECT_EQ(actual, first);
  ASSERT_TRUE(ftlRead(&ftl, ftl.usableSectorCount - 1, actual.data()));
  EXPECT_EQ(actual, last);
  EXPECT_FALSE(ftlRead(&ftl, ftl.usableSectorCount, actual.data()));
  ftlDeInit(&ftl);
}

TEST(Nb4Storage, FreeSpaceReserveUsesTheConfiguredFatAllocationUnit)
{
  EXPECT_EQ(sdFreeKBFromSectors(1024), 512u);
  EXPECT_EQ(sdFreeKBFromSectors(776), 388u); // Remaining after English voices.
  EXPECT_EQ(SD_ALLOCATION_UNIT_BYTES, 512u);
  EXPECT_TRUE(sdSpaceAvailable(388, 0));
  EXPECT_TRUE(sdSpaceAvailable(388, 16384));
  EXPECT_FALSE(sdSpaceAvailable(388, 480 * 320 * 3 + 54));
  EXPECT_FALSE(sdSpaceAvailable(255, 0));
  EXPECT_TRUE(sdSpaceAvailable(256, 0));
  EXPECT_FALSE(sdSpaceAvailable(256, 1));
  EXPECT_TRUE(sdSpaceAvailable(257, 1));
  EXPECT_FALSE(sdSpaceAvailable(1024, UINT32_MAX));
}
#endif

static const FRESULT kAllResults[] = {
    FR_OK, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM, FR_MKFS_ABORTED, FR_TIMEOUT,
    FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER,
};

TEST(Nb4Storage, OnlyAMissingFilesystemCanEverJustifyCreatingOne)
{
  unsigned accepted = 0;
  for (FRESULT r : kAllResults) {
    const bool ok = nb4MountFailureIsMissingFilesystem(r);
    if (r == FR_NO_FILESYSTEM) {
      EXPECT_TRUE(ok);
      accepted += 1;
    } else {
      EXPECT_FALSE(ok);
    }
  }
  EXPECT_EQ(accepted, 1u);

  EXPECT_FALSE(nb4MountFailureIsMissingFilesystem(FR_DISK_ERR));
  EXPECT_FALSE(nb4MountFailureIsMissingFilesystem(FR_NOT_READY));
  EXPECT_FALSE(nb4MountFailureIsMissingFilesystem(FR_INT_ERR));
}

TEST(Nb4Storage, MountingAtBootNeverRequestsAWriteWhateverGoesWrong)
{

  for (FRESULT r : kAllResults) {
    if (r == FR_OK) continue;
    const uint32_t before = nb4FilesystemCreationRequests();
    simuFatfsSetNextMountResult(r);
    sdMount();
    EXPECT_EQ(nb4FilesystemCreationRequests(), before);

    EXPECT_EQ(nb4StorageMountResult(), r);
  }
}

TEST(Nb4Storage, CreatingTheFilesystemIsAlwaysRecordedAsARequest)
{
  const uint32_t before = nb4FilesystemCreationRequests();
  nb4RequestFilesystemCreation();
  EXPECT_EQ(nb4FilesystemCreationRequests(), before + 1);
}

#endif  // RADIO_NB4_FAMILY
