/* SPDX-License-Identifier: GPL-2.0-only */
#include "edgetx.h"
#if defined(RADIO_NB4)
#include "nb4_audio_identity.h"
#include "nb4_maintenance_protocol.h"
namespace {
Nb4AudioIdentity identity;
struct Receipt { char magic[8],language[2]; uint16_t version; char release[32]; uint8_t hash[32]; uint32_t crc; };
static_assert(sizeof(Receipt)==80);
}
const Nb4AudioIdentity& nb4AudioIdentity() { return identity; }
void nb4AudioIdentityLoad() {
  identity={};
  FILINFO stat;
  identity.pending=f_stat("/.APEXTX-AUDIO-INSTALL.json",&stat)==FR_OK ||
      f_stat("/APEXTX-AUDIO-RESTORE.json",&stat)==FR_OK;
  if (identity.pending) return;
  FIL file; if (f_open(&file,"/APEXTX-AUDIO.ID",FA_READ)!=FR_OK) return;
  Receipt record{}; UINT count=0;
  bool ok=f_size(&file)==sizeof(record) && f_read(&file,&record,sizeof(record),&count)==FR_OK &&
      count==sizeof(record) && !memcmp(record.magic,"APXAUD1",8) && record.version==1 &&
      record.crc==nb4::maintenanceCrc(reinterpret_cast<const uint8_t*>(&record),sizeof(record)-4) &&
      (!memcmp(record.language,"en",2)||!memcmp(record.language,"es",2)||!memcmp(record.language,"de",2)||!memcmp(record.language,"cn",2));
  if (f_close(&file)!=FR_OK) ok=false;
  bool terminated=false;
  for (char c : record.release) {
    if (!c) { terminated=true; break; }
    if (!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='.'||c=='-'||c=='+'||c=='_')) ok=false;
  }
  if (!ok || !terminated || !record.release[0]) return;
  char hello[48]; snprintf(hello,sizeof(hello),"/SOUNDS/%.2s/SYSTEM/hello.wav",record.language);
  if (f_stat(hello,&stat)!=FR_OK || !stat.fsize) return;
  identity.installed=true; memcpy(identity.language,record.language,2); memcpy(identity.release,record.release,32);
  if (memcmp(g_eeGeneral.ttsLanguage,record.language,2)) {
    memcpy(g_eeGeneral.ttsLanguage,record.language,2); storageDirty(EE_GENERAL);
  }
}
#endif
