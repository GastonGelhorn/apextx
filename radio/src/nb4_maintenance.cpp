/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4)
#include "nb4_maintenance.h"
#include "nb4_maintenance_protocol.h"
#include "nb4_rf_update.h"
#include "nb4_racing.h"
#include "nb4_history.h"
#include "nb4_audio_identity.h"
#include "bootloader/nb4_update_id.h"
#include "bootloader/nb4_update.h"
#include "pulses/afhds3.h"
#include "fifo.h"
#include "fw_version.h"
#include "hal/usb_driver.h"
#include "hal/abnormal_reboot.h"
#include <atomic>

namespace {
std::atomic<bool> enabled{false}, active{false};
Fifo<uint8_t, 2048> incoming;
nb4::MaintenanceDecoder decoder;
uint8_t outgoing[nb4::maintenanceFrameSize];
size_t outgoingSize = 0;
tmr10ms_t lastByte = 0;
// Service ticks left before a requested restart into update mode; the reply
// is flushed first so the host sees the acknowledgement.
uint8_t rebootTicks = 0;
#if defined(SIMU)
bool rebootRequested = false;
#endif

void receive(uint8_t* data, uint32_t length)
{
  if (!active) return;
  for (uint32_t i = 0; i < length; ++i) incoming.push(data[i]);
}
const char* boolean(bool value) { return value ? "true" : "false"; }
void unitSerial(char (&serial)[13]) {
#if !defined(SIMU)
  const auto uid = reinterpret_cast<const uint32_t*>(UID_BASE);
  snprintf(serial, sizeof(serial), "%08lX%04lX", static_cast<unsigned long>(uid[0]+uid[2]),
           static_cast<unsigned long>(uid[1]>>16));
#else
  strcpy(serial,"000000000001");
#endif
}
void storageSession(const uint8_t* data, size_t size, char* response, size_t capacity) {
  uint8_t any=0; for (size_t i=0;i<size;++i) any |= data[i];
  if (size != 16 || !any || nb4RfUpdateStorageOwned() || !sdMounted() ||
      nb4RacePhase() == Nb4RacePhase::Running || !nb4StorageQuiesce()) {
    snprintf(response,capacity,"{\"error\":\"storage_session_unavailable\"}"); return;
  }
  char serial[13], nonce[33]; unitSerial(serial);
  for (unsigned i=0;i<16;++i) snprintf(nonce+i*2,3,"%02x",data[i]);
  char marker[192];
  const int length=snprintf(marker,sizeof(marker),"{\"format\":\"apextx-storage-session-1\",\"serial\":\"%s\",\"target\":\"%s\",\"nonce\":\"%s\"}",serial,NB4_HARDWARE_ID,nonce);
  FIL file; UINT count=0;
  bool ok=f_open(&file,"/APEXTX-USB.json",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK;
  if (ok) {
    ok=f_write(&file,marker,length,&count)==FR_OK && count==unsigned(length) && f_sync(&file)==FR_OK;
    if (f_close(&file)!=FR_OK) ok=false;
  }
  nb4StorageResume();
  snprintf(response,capacity,ok?"{\"storage_session\":true,\"nonce\":\"%s\"}":"{\"error\":\"storage_session_failed\"}",nonce);
}
}

bool nb4MaintenanceEnable()
{
  if (active) return true;
  if (usbStarted()) {
    if (getSelectedUsbMode() != USB_SERIAL_MODE) return false;
    // Explicitly hand over an ordinary serial session. A physical reconnect
    // must not depend on the automatic cable probe's temporary backoff.
#if !defined(SIMU)
    usbStop();
    serialStop(SP_VCP);
#endif
  }
  enabled = true;
  setSelectedUsbMode(USB_SERIAL_MODE);
#if !defined(SIMU)
  nb4UsbRetryConnection();
  nb4MaintenanceStartUsb();
  usbStart();
#endif
  return true;
}
bool nb4MaintenanceEnabled() { return enabled; }
void nb4RebootIntoUpdateMode()
{
  watchdogSuspend(2000);
  pulsesStop();
  pwrOn();
  abnormalRebootRequestDfu();
#if defined(SIMU)
  rebootRequested = true;
#else
  NVIC_SystemReset();
#endif
}
extern "C" bool nb4MaintenanceUsbSelected() { return enabled; }
void nb4MaintenanceStartUsb()
{
  incoming.clear(); decoder.reset(); outgoingSize = 0;
  active = enabled.load();
#if !defined(SIMU)
  usbSerialSetReceiveDataCb(nullptr, receive);
#endif
}
void nb4MaintenanceStopUsb()
{
  nb4RfUpdateUsbDisconnected();
  active = enabled = false; rebootTicks = 0;
#if !defined(SIMU)
  usbSerialSetReceiveDataCb(nullptr, nullptr);
#endif
  incoming.clear(); decoder.reset(); outgoingSize = 0;
}
size_t nb4MaintenanceInventory(char* output, size_t capacity)
{
  char serial[13]; unitSerial(serial);
  uint32_t appSize=0, appCrc=0; bool appValid=false;
#if !defined(SIMU)
  const auto& manifest=*reinterpret_cast<const nb4update::Manifest*>(nb4update::ManifestAddress);
  appValid=!memcmp(manifest.magic,"APXNB4U1",8) && manifest.target==NB4_UPDATE_TARGET &&
      manifest.address==nb4update::AppAddress && manifest.version==1 && manifest.size>=8 &&
      manifest.size<=nb4update::MaxApplication && manifest.headerCrc==nb4::maintenanceCrc(reinterpret_cast<const uint8_t*>(&manifest),28);
  if (appValid) { appSize=manifest.size; appCrc=manifest.crc; }
#endif
  const auto epoch = afhds3::getVersionEpoch(0);
  const auto module = afhds3::getModuleVersionInfo(0);
  const auto receiver = afhds3::getReceiverVersionInfo(0);
  uint32_t car = 2166136261u;
  for (auto byte : g_model.nb4RfSettings) car = (car ^ byte) * 16777619u;
  for (auto byte : g_eeGeneral.currModelFilename) car = (car ^ uint8_t(byte)) * 16777619u;
  const bool connected = afhds3::isConnected(0) && getModuleMode(0) != MODULE_MODE_BIND;
  const auto& audio=nb4AudioIdentity();
  const char* voice=g_eeGeneral.ttsLanguage;
  if (voice[0]<'a'||voice[0]>'z'||voice[1]<'a'||voice[1]>'z') voice="";
  const int count = snprintf(output, capacity,
      "{\"format\":\"apextx-inventory-1\",\"serial\":\"%s\",\"target\":\"%s\","
      "\"application\":{\"valid\":%s,\"bytes\":%lu,\"crc32\":%lu},"
      "\"epoch\":%lu,\"car_key\":%lu,\"connected\":%s,\"two_way\":%s,"
      "\"module\":{\"valid\":%s,\"unavailable\":%s,\"reply_bytes\":%u,"
      "\"product\":%lu,\"hardware\":%lu,\"firmware\":%lu,\"rf_library\":%lu,"
      "\"firmware_known\":%s,\"write_supported\":%s},"
      "\"receiver\":{\"valid\":%s,\"unavailable\":%s,\"product\":%lu,"
      "\"identity_words\":[%u,%u,%u,%u,%u],\"firmware\":%u,\"write_supported\":%s},"
      "\"bootloader\":{\"state\":\"query_in_update_mode\"},"
      "\"audio\":{\"state\":\"%s\",\"language\":\"%.2s\",\"release\":\"%s\"},\"rf_write_supported\":%s}",
      serial, NB4_HARDWARE_ID,
      boolean(appValid),static_cast<unsigned long>(appSize),static_cast<unsigned long>(appCrc),
      static_cast<unsigned long>(epoch), static_cast<unsigned long>(car),
      boolean(connected), boolean(g_model.moduleData[0].afhds3.telemetry),
      boolean(module.valid), boolean(module.unavailable), module.replyLen,
      static_cast<unsigned long>(module.productNumber),
      static_cast<unsigned long>(module.hardwareVersion),
      static_cast<unsigned long>(module.firmwareVersion),
      static_cast<unsigned long>(module.rfVersion), boolean(module.firmwareKnown),
      boolean(nb4RfUpdateWriteSupported(false)),
      boolean(receiver.valid), boolean(receiver.unavailable),
      static_cast<unsigned long>(receiver.productNumber),
      receiver.identityWords[0], receiver.identityWords[1], receiver.identityWords[2],
      receiver.identityWords[3], receiver.identityWords[4], receiver.firmwareVersion,
      boolean(nb4RfUpdateWriteSupported(true)),
      audio.pending?"pending":audio.installed?"installed":"unknown",audio.installed?audio.language:voice,audio.release,
      boolean(nb4RfUpdateWriteSupported(false) || nb4RfUpdateWriteSupported(true)));
  if (count < 0 || size_t(count) >= capacity || epoch != afhds3::getVersionEpoch(0)) return 0;
  return count;
}

void nb4MaintenanceService()
{
  if (!active) return;
#if !defined(SIMU)
  if (outgoingSize) {
    if (usbSerialFreeSpace() < outgoingSize) return;
    for (size_t i = 0; i < outgoingSize; ++i) usbSerialPutc(nullptr, outgoing[i]);
    outgoingSize = 0;
  }
#endif
  if (rebootTicks && !outgoingSize && !--rebootTicks) { nb4RebootIntoUpdateMode(); return; }
  const auto now = get_tmr10ms();
  if (tmr10ms_t(now - lastByte) >= 100) decoder.reset();
  uint8_t byte;
  // Work is bounded even if the PC continuously sends invalid data.
  for (unsigned i = 0; i < 512 && incoming.pop(byte); ++i) {
    lastByte = now;
    if (!decoder.push(byte)) continue;
    char response[nb4::maintenanceMaxPayload];
    size_t length = 0;
    if (nb4RfUpdateCommand(decoder.command(), decoder.payload(), decoder.length(), response, sizeof(response))) {
      length = strlen(response);
    } else if (decoder.command() == uint8_t(nb4::MaintenanceCommand::StorageSession)) {
      storageSession(decoder.payload(),decoder.length(),response,sizeof(response));
    } else if (decoder.length()) {
      strcpy(response, "{\"error\":\"unexpected_payload\"}");
    } else if (decoder.command() == uint8_t(nb4::MaintenanceCommand::Capabilities)) {
      const bool write = nb4RfUpdateWriteSupported(false) || nb4RfUpdateWriteSupported(true);
      snprintf(response, sizeof(response), "{\"protocol\":1,\"inventory\":true,\"rf_write\":%s,"
                       "\"rf_build\":\"%s\","
                       "\"rf_recover\":%s,"
                       "\"raw_backup\":false,\"file_transfer\":true,\"storage_session\":true,\"application_identity\":true,"
                       "\"assets\":true,\"reboot_update\":true,"
                       "\"reason\":\"%s\"}",
                       boolean(write), nb4RfUpdateBuildKind(), boolean(nb4RfUpdateRecoverySupported()),
                       write ? "" : "hardware_qualification_required");
    } else if (decoder.command() == uint8_t(nb4::MaintenanceCommand::Inventory)) {
      length = nb4MaintenanceInventory(response, sizeof(response));
      if (!length) strcpy(response, "{\"error\":\"inventory_changed\"}");
    } else if (decoder.command() == uint8_t(nb4::MaintenanceCommand::RebootUpdate)) {
      // Same guard as the Update page: never leave an RF operation behind.
      if ((nb4RfUpdateStorageOwned() && !nb4RfUpdateNeedsRecovery()) || nb4RacePhase() == Nb4RacePhase::Running)
        strcpy(response, "{\"error\":\"maintenance_busy\"}");
      else { rebootTicks = 30; strcpy(response, "{\"reboot_update\":true}"); }
    } else {
      // Never accept upload/start as an acknowledged no-op. An old or
      // unqualified device cannot accidentally authorize a newer host writer.
      strcpy(response, "{\"error\":\"unsupported_operation\"}");
    }
    if (!length) length = strlen(response);
    outgoingSize = nb4::maintenanceEncode(outgoing, sizeof(outgoing),
        decoder.command() | 0x80, decoder.request(), response, length);
    break; // One response at a time; no overwriting the pending transmit buffer.
  }
}
#if defined(SIMU)
size_t nb4MaintenanceTestExchange(uint8_t command, const void* payload, size_t length, char* output, size_t capacity)
{
  uint8_t frame[nb4::maintenanceFrameSize];
  const auto size = nb4::maintenanceEncode(frame, sizeof(frame), command, 1, payload, length);
  active = true; outgoingSize = 0;
  for (size_t i = 0; i < size; ++i) incoming.push(frame[i]);
  nb4MaintenanceService();
  const size_t count = outgoingSize ? outgoing[6] | (size_t(outgoing[7]) << 8) : 0;
  if (count >= capacity) return 0;
  memcpy(output, outgoing + nb4::maintenanceHeaderSize, count); output[count] = 0;
  outgoingSize = 0;
  return count;
}
bool nb4MaintenanceTestRebootRequested()
{
  for (unsigned i = 0; i < 64 && rebootTicks; ++i) nb4MaintenanceService();
  const bool result = rebootRequested; rebootRequested = false; return result;
}
#endif
#endif
