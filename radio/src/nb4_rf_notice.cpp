/* SPDX-License-Identifier: GPL-2.0-only */
#include "edgetx.h"
#if defined(RADIO_NB4)
#include "nb4_rf_notice.h"
#include "nb4_rf_update.h"
#include "nb4_rf_images.h"
#include "nb4_maintenance_protocol.h"
#include "nb4_sha256.h"
#include "nb4_racing.h"
#include "pulses/afhds3.h"
#include "hal/usb_driver.h"
#include <new>
namespace {
struct Deferred { uint8_t magic[8],key[32]; uint32_t crc; };
constexpr const char* path="/FIRMWARE/RF/deferred.dat";
constexpr unsigned maxRecords=64;
FIL asset;
bool open=false;
int checking=-1,ready=-1;
uint32_t epoch=UINT32_MAX,attempted=0,offset=0;
uint8_t key[32]{},shown[32]{};
bool shownValid=false;
nb4::Sha256 noticeHash;
void close() { if (open) f_close(&asset); open=false; checking=-1; }
bool deferred(const uint8_t value[32]) {
  FIL file; if (f_open(&file,path,FA_READ)!=FR_OK) return false;
  Deferred record{}; UINT count=0; bool found=false;
  for (unsigned i=0;i<maxRecords;++i) {
    if (f_read(&file,&record,sizeof(record),&count)!=FR_OK || count!=sizeof(record)) break;
    if (!memcmp(record.magic,"APXDEFR1",8) && !memcmp(record.key,value,32) &&
        record.crc==nb4::maintenanceCrc(reinterpret_cast<const uint8_t*>(&record),sizeof(record)-4)) found=true;
  }
  f_close(&file); return found;
}
bool remember(const uint8_t value[32]) {
  if (deferred(value)) return true;
  FRESULT result=f_mkdir("/FIRMWARE");
  if (result==FR_OK || result==FR_EXIST) result=f_mkdir("/FIRMWARE/RF");
  if (result!=FR_OK && result!=FR_EXIST) return false;
  FIL file; if (f_open(&file,path,FA_OPEN_ALWAYS|FA_WRITE|FA_READ)!=FR_OK) return false;
  Deferred record{}; memcpy(record.magic,"APXDEFR1",8); memcpy(record.key,value,32);
  record.crc=nb4::maintenanceCrc(reinterpret_cast<const uint8_t*>(&record),sizeof(record)-4);
  const auto size=f_size(&file);
  const auto at=size/sizeof(record)>=maxRecords ? 0u : size-size%sizeof(record);
  UINT count=0;
  bool ok=f_lseek(&file,at)==FR_OK && f_write(&file,&record,sizeof(record),&count)==FR_OK &&
      count==sizeof(record) && f_sync(&file)==FR_OK;
  if (f_close(&file)!=FR_OK) ok=false;
  return ok && deferred(value);
}
void makeKey(int index,uint8_t value[32]) {
  nb4::Sha256 digest;
  // The learned identity/hopping prefix is unchanged by trims, failsafe,
  // car renaming and UI settings. Rebinding intentionally creates a new key.
  if (nb4::rfImages[index].receiver) digest.update(g_model.nb4RfSettings+2,52);
#if !defined(SIMU)
  else digest.update(reinterpret_cast<const uint8_t*>(UID_BASE),12);
#endif
  digest.update(nb4::rfImages[index].sha256,32); digest.finish(value);
}
}
void nb4RfNoticeInvalidate() { epoch=UINT32_MAX; }
bool nb4RfNoticeStorageOwned() { return open; }
void nb4RfNoticeService() {
  const auto currentEpoch=afhds3::getVersionEpoch(0);
  if (epoch!=currentEpoch) { close(); ready=-1; attempted=0; epoch=currentEpoch; }
  if (!sdMounted() || nb4RfUpdateStorageOwned() || getSelectedUsbMode()==USB_MASS_STORAGE_MODE ||
      nb4RacePhase()==Nb4RacePhase::Running) {
    if (checking>=0) attempted&=~(1u<<checking);
    close(); return;
  }
  if (checking>=0) {
    if (nb4RfUpdateCandidate(nb4::rfImages[checking].receiver)!=checking) { close(); return; }
    uint8_t block[512]; UINT count=0;
    const auto wanted=std::min<uint32_t>(sizeof(block),nb4::rfImages[checking].size-offset);
    if (f_read(&asset,block,wanted,&count)!=FR_OK || count!=wanted) { close(); return; }
    noticeHash.update(block,count); offset+=count;
    if (offset==nb4::rfImages[checking].size) {
      uint8_t digest[32]; noticeHash.finish(digest);
      if (!memcmp(digest,nb4::rfImages[checking].sha256,32)) ready=checking;
      close();
    }
    return;
  }
  if (ready>=0) return;
  for (bool receiver : {true,false}) {
    if (receiver && (g_model.nb4RfSettings[0]!=1 || g_model.nb4RfSettings[1]!=1)) continue;
    const int index=nb4RfUpdateCandidate(receiver);
    if (index<0 || attempted&(1u<<index)) continue;
    attempted|=1u<<index; makeKey(index,key);
    if (deferred(key)) continue;
    char name[48]; snprintf(name,sizeof(name),"/FIRMWARE/RF/%08lX.bin",(unsigned long)nb4::rfImages[index].product);
    if (f_open(&asset,name,FA_READ)!=FR_OK) continue;
    open=true;
    if (f_size(&asset)!=nb4::rfImages[index].size) { close(); continue; }
    checking=index; offset=0; new (&noticeHash) nb4::Sha256(); return;
  }
}
int nb4RfNoticeTake() {
  if (ready<0 || nb4RacePhase()==Nb4RacePhase::Running ||
      nb4RfUpdateCandidate(nb4::rfImages[ready].receiver)!=ready) return -1;
  const int result=ready; ready=-1; memcpy(shown,key,32); shownValid=true; return result;
}
bool nb4RfNoticePostpone() { return shownValid && remember(shown); }
#if defined(SIMU)
bool nb4RfNoticeTestDeferred(const uint8_t key[32],bool write) { return write ? remember(key) : deferred(key); }
#endif
#endif
