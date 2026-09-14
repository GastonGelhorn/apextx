/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "diskio_spi_flash.h"
#include "spi_flash.h"

#include "hal.h"

#if defined(RADIO_NB4) && !defined(BOOT)
#include "nb4_health.h"
#endif

#if !defined(DISABLE_FLASH_FTL)
#define USE_FLASH_FTL
#endif

#if defined(SPI_FLASH_RESERVED_BASE)
  #define SPI_FLASH_DATA_BASE  ((uint32_t)(SPI_FLASH_RESERVED_BASE))
#else
  #define SPI_FLASH_DATA_BASE  ((uint32_t)0)
#endif

#if defined(USE_FLASH_FTL)
#include "drivers/frftl.h"

static FrFTL _frftl;
static bool frftlInitDone = false;

static bool flashRead(uint32_t addr, uint8_t* buf, uint32_t len)
{
  return flashSpiRead(SPI_FLASH_DATA_BASE + addr, buf, len) == len;
}

static bool flashWrite(uint32_t addr, const uint8_t *buf, uint32_t len)
{
  uint32_t pageSize = flashSpiGetPageSize();
  if(len % pageSize != 0)
    return false;

  while(len > 0) {
    if (flashSpiWrite(SPI_FLASH_DATA_BASE + addr, buf, pageSize) != pageSize) {
      return false;
    }
    len -= pageSize;
    buf += pageSize;
    addr += pageSize;
  }

  if(len != 0) {
    return false;
  }

  return true;
}

static bool flashErase(uint32_t addr)
{
  return flashSpiErase(SPI_FLASH_DATA_BASE + addr) == 0;
}

static bool flashBlockErase(uint32_t addr)
{
  return flashSpiBlockErase(SPI_FLASH_DATA_BASE + addr) == 0;
}

static bool isFlashErased(uint32_t addr)
{
  return flashSpiIsErased(SPI_FLASH_DATA_BASE + addr);
}

static const FrFTLOps _frftl_cb = {
  .flashRead = flashRead,
  .flashProgram = flashWrite,
  .flashErase = flashErase,
  .flashBlockErase = flashBlockErase,
  .isFlashErased = isFlashErased,
};
#endif  // USE_FLASH_FTL

static DSTATUS spi_flash_initialize(BYTE lun)
{
  if (!flashSpiInit()) {
    return STA_NOINIT;
  }

#if defined(USE_FLASH_FTL)
  if (frftlInitDone) {
    // Do flush when trying to reinit spi flash,
    // because the frftl only need to be initialized once
    if (!ftlSync(&_frftl)) {
      return STA_NOINIT;
    }
  } else {
#if defined(SPI_FLASH_USABLE_SIZE)
    const uint32_t flashSize = SPI_FLASH_USABLE_SIZE;
    if ((uint32_t)flashSpiGetSize() <
        SPI_FLASH_DATA_BASE + flashSize) {

      return STA_NOINIT;
    }
    if (!ftlInitWithSize(&_frftl, &_frftl_cb, flashSize)) {
      return STA_NOINIT;
    }
#else
    int flashSize = flashSpiGetSize();
    int flashSizeMB = flashSize  / 1024 / 1024;

    if (!ftlInit(&_frftl, &_frftl_cb, flashSizeMB)) {
      return STA_NOINIT;
    }
#endif
    frftlInitDone = true;
  }
#endif

  return 0;
}

static DSTATUS spi_flash_status (BYTE lun)
{
  return 0;
}

static DRESULT spi_flash_read(BYTE lun, BYTE * buff, DWORD sector, UINT count)
{
#if defined(USE_FLASH_FTL)
  while(frftlInitDone && count) {

    if(!ftlRead(&_frftl, sector, (uint8_t*)buff)) {
      return RES_ERROR;
    }

    buff += 512;
    sector++;
    count --;
  }
#else
  flashSpiRead(SPI_FLASH_DATA_BASE + (uint32_t)sector * 512, buff, count * 512);
#endif

  return RES_OK;
}

static DRESULT spi_flash_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
#if defined(USE_FLASH_FTL)
  if (frftlInitDone && !ftlWrite(&_frftl, sector, count, (uint8_t*)buff)) {
    return RES_ERROR;
  }
#else
  // write in page size
  uint32_t address = SPI_FLASH_DATA_BASE + (uint32_t)sector * 512;
  count <<= 1;
  while(count) {
    if (!flashSpiWrite(address, buff, 256)) {
      return RES_ERROR;
    }
    buff    += 256;
    address += 256;
    count -= 1;
  }
#endif

  return RES_OK;
}

static DRESULT spi_flash_ioctl(BYTE lun, BYTE ctrl, void *buff)
{
  DRESULT res = RES_OK;

  switch (ctrl) {

  case GET_SECTOR_COUNT: /* Get number of sectors on the disk (DWORD) */
#if defined(USE_FLASH_FTL)
    *(DWORD*)buff = _frftl.usableSectorCount;
#else
    *(DWORD*)buff = flashSpiGetSize() / 512;
#endif
    break;

  case GET_SECTOR_SIZE:  /* Get R/W sector size (WORD) */
    *(WORD*)buff = 512;
    break;

  case GET_BLOCK_SIZE :   /* Get erase block size in unit of sector (DWORD) */
    *(DWORD*)buff = 4096 / 512;
    break;

  case CTRL_SYNC:
#if defined(USE_FLASH_FTL)
    if (frftlInitDone && !ftlSync(&_frftl)) {
      res = RES_ERROR;
    }
#else
    flashSpiSync();
#endif
    break;

  case CTRL_TRIM:
#if defined(USE_FLASH_FTL)
    if (frftlInitDone && !ftlTrim(&_frftl, *(DWORD*)buff, 1 + *((DWORD*)buff + 1) - *(DWORD*)buff)) {
      res = RES_ERROR;
    }
#endif
    break;
  }

  return res;
}

bool spiFlashDiskEraseAll()
{
#if defined(USE_FLASH_FTL)
  if (frftlInitDone) {
    ftlDeInit(&_frftl);
    frftlInitDone = false;
  }
#endif
#if defined(SPI_FLASH_RESERVED_BASE)

  const uint32_t blockSize = 32768;
  const uint32_t dataBytes = SPI_FLASH_USABLE_SIZE;
  for (uint32_t off = 0; off < dataBytes; off += blockSize) {
    if (flashSpiBlockErase(SPI_FLASH_DATA_BASE + off) != 0) return false;
#if defined(RADIO_NB4) && !defined(BOOT)
    nb4HealthBeat(NB4_TASK_UI);
#endif
  }
  for (uint32_t off = 0; off < dataBytes; off += 4096) {
    if (!flashSpiIsErased(SPI_FLASH_DATA_BASE + off)) return false;
#if defined(RADIO_NB4) && !defined(BOOT)
    nb4HealthBeat(NB4_TASK_UI);
#endif
  }
#else
  flashSpiEraseAll();
#endif
  return true;
}

const diskio_driver_t spi_flash_diskio_driver = {
  .initialize = spi_flash_initialize,
  .status = spi_flash_status,
  .read = spi_flash_read,
  .write = spi_flash_write,
  .ioctl = spi_flash_ioctl,
};
