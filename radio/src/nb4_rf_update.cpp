/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4)
#include "nb4_rf_update.h"
#include "nb4_rf_update_core.h"
#include "nb4_rf_images.h"
#include "nb4_sha256.h"
#include "nb4_history.h"
#include "nb4_racing.h"
#include "pulses/afhds3.h"
#include "pulses/afhds3_transport.h"
#include "nb4_maintenance_protocol.h"
#include "nb4_rf_notice.h"
#include "hal/usb_driver.h"
#include "bootloader/nb4_update_id.h"
#include <atomic>
#include <new>

namespace {
using namespace nb4;
#if defined(NB4_RF_UPDATE_BENCH)
constexpr const char rfBenchMarker[] = "APXRFTEST1";
#endif
constexpr const char* stagePath = "/FIRMWARE/RF/transfer.tmp";
constexpr const char* journalPaths[] = {"/FIRMWARE/RF/operation.dat", "/FIRMWARE/RF/operation.alt"};
uint32_t journalSequence = 0;
RfUpdateCore engine;
FIL imageFile{};
bool open = false, uploading = false, validated = false, initializing = true;
bool validating = false, startRequested = false, storagePaused = false, reinstall = false;
bool recovering = false, journalValid = false;
bool assetUploading = false;
unsigned assetIndex = 0;
uint32_t assetUploaded = 0;
int installable = -1;
uint32_t installableEpoch = UINT32_MAX;
bool installableDirty = true;
bool journalDamaged = false, entryProbeStarted = false, entryProbeFinished = false;
uint32_t entryProbeEpoch = 0;
#if defined(NB4_RF_UPDATE_BENCH)
std::atomic<uint32_t> rxWireBytes{0}, rxFrames{0}, lastRxMeta{0}, lastRfRxMeta{0}, lastRfValue{0}, txRequests{0}, lastTxOpcode{0};
std::atomic<uint32_t> faultFlags{0}, txResponses{0};
std::atomic<uint32_t> checkpointMs{0};
std::atomic<uint32_t> bootReplyWords[4]{};
std::atomic<uint32_t> lastRxValue{0};
#endif
bool benchArmed = false;
unsigned imageIndex = 0;
uint32_t operation = 0, uploaded = 0, validationOffset = 0;
uint32_t verifyBaseline = 0;
RfUpdateIdentity identity{};
Sha256 imageHash;
uint8_t backupHash[32]{};
uint8_t pinnedPeer[16]{};
uint32_t pinnedPeerLength = 0;
std::atomic<bool> linkOwned{false}, drivingLocked{false}, startupReady{
#if defined(SIMU)
true
#else
false
#endif
};
struct Packet {
  uint8_t type, opcode, sequence, size;
  uint8_t data[169];
};
Packet outgoing{};
std::atomic<bool> transmit{false}, overflow{false}, requestPending{false}, abortTransport{false}, disconnected{false};
std::atomic<bool> releaseEntryRequest{false};
std::atomic<bool> awaitingModuleEntryAck{false};
struct ReceivedPacket {
  uint8_t type, opcode, sequence, size;
  uint8_t data[14];
  uint32_t generation;
};
Fifo<ReceivedPacket, 32> received;
ReceivedPacket lastReceived{}; // Only the RX producer writes this cache.
std::atomic<uint32_t> receivedGeneration{0}, consumedGeneration{0};
struct Journal {
  uint8_t magic[8];
  uint32_t sequence;
  uint32_t operation, image, phase;
  RfUpdateIdentity identity;
  uint8_t backup[32];
  uint32_t peerLength;
  uint8_t peer[16];
  uint32_t uid[3], target;
  uint32_t crc;
};
static_assert(sizeof(Journal) == 128, "RF journal layout changed");
void unitId(uint32_t uid[3]) {
#if defined(SIMU)
  uid[0] = 1; uid[1] = uid[2] = 0;
#else
  memcpy(uid, reinterpret_cast<const void*>(UID_BASE), 12);
#endif
}
uint32_t get32(const uint8_t* p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void assetPathFor(unsigned index, char* result, size_t size) {
  snprintf(result, size, "/FIRMWARE/RF/%08lX.bin", (unsigned long)rfImages[index].product);
}
void assetPath(char* result, size_t size) { assetPathFor(imageIndex, result, size); }
bool headroom(uint32_t bytes) {
  DWORD freeClusters; FATFS* filesystem = nullptr;
  if (f_getfree("", &freeClusters, &filesystem) != FR_OK) return false;
#if defined(SIMU)
  return true;  // The simulator reports free clusters without a volume.
#else
  return uint64_t(freeClusters) * filesystem->csize * 512 >= bytes;
#endif
}
bool assetPresent(unsigned index) {
  char path[48]; assetPathFor(index, path, sizeof(path)); FILINFO info;
  return f_stat(path, &info) == FR_OK && info.fsize == rfImages[index].size;
}
bool assetVerified(unsigned index) {
  char path[48]; assetPathFor(index, path, sizeof(path));
  FIL file; if (f_open(&file, path, FA_READ) != FR_OK) return false;
  bool ok = f_size(&file) == rfImages[index].size;
  Sha256 hash; uint8_t block[512]; UINT count = 0;
  for (uint32_t offset = 0; ok && offset < rfImages[index].size; offset += count) {
    const auto wanted = std::min<uint32_t>(sizeof(block), rfImages[index].size - offset);
    ok = f_read(&file, block, wanted, &count) == FR_OK && count == wanted;
    if (ok) hash.update(block, count);
  }
  f_close(&file);
  uint8_t digest[32]; hash.finish(digest);
  return ok && !memcmp(digest, rfImages[index].sha256, 32);
}
RfUpdateIdentity snapshot(bool receiver) {
  const auto module = afhds3::getModuleVersionInfo(0);
  const auto rx = afhds3::getReceiverVersionInfo(0);
  uint32_t car = 2166136261u;
  for (auto byte : g_model.nb4RfSettings) car = (car ^ byte) * 16777619u;
  for (auto byte : g_eeGeneral.currModelFilename) car = (car ^ uint8_t(byte)) * 16777619u;
  const bool liveReceiver = afhds3::isConnected(0) && getModuleMode(0) != MODULE_MODE_BIND &&
      g_model.moduleData[0].afhds3.telemetry;
  return {afhds3::getVersionEpoch(0), car,
          receiver ? (rx.valid && liveReceiver ? rx.productNumber : 0) : (module.valid ? module.productNumber : 0),
          receiver ? uint32_t(rx.identityWords[0]) : module.hardwareVersion,
          receiver ? rx.firmwareVersion : (module.firmwareKnown ? module.firmwareVersion : 0),
          receiver ? uint32_t(rx.identityWords[2]) : 0,
          receiver ? uint32_t(rx.identityWords[1]) : 0,
          receiver ? uint32_t(rx.identityWords[4]) : module.rfVersion};
}
bool journal(RfUpdatePhase phase) {
  if (journalSequence == UINT32_MAX) return false;
  Journal data{};
  memcpy(data.magic, "APXRFW3", 8);
  data.sequence = journalSequence + 1;
  data.operation = operation; data.image = imageIndex; data.phase = uint32_t(phase); data.identity = identity;
  memcpy(data.backup, backupHash, 32);
  data.peerLength = pinnedPeerLength; memcpy(data.peer, pinnedPeer, sizeof(pinnedPeer));
  data.target = NB4_UPDATE_TARGET; unitId(data.uid);
  data.crc = maintenanceCrc(reinterpret_cast<const uint8_t*>(&data), sizeof(data) - 4);
  // Alternate slots so a torn write never truncates the last verified phase.
  const auto path = journalPaths[data.sequence & 1];
  FIL file; UINT count = 0;
  if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
  bool ok = f_write(&file, &data, sizeof(data), &count) == FR_OK && count == sizeof(data) && f_sync(&file) == FR_OK;
  if (f_close(&file) != FR_OK) ok = false;
  Journal check{};
  if (!ok || f_open(&file, path, FA_READ) != FR_OK) return false;
  ok = f_read(&file, &check, sizeof(check), &count) == FR_OK && count == sizeof(check) && !memcmp(&data, &check, sizeof(data));
  if (f_close(&file) != FR_OK) ok = false;
  if (ok) journalSequence = data.sequence;
  return ok;
}
bool readJournal(Journal& selected, bool& present, bool& damaged) {
  bool valid = false;
  uint32_t uid[3]; unitId(uid);
  for (const auto path : journalPaths) {
    FIL file; const auto result = f_open(&file, path, FA_READ);
    if (result == FR_NO_FILE || result == FR_NO_PATH) continue;
    present = true;
    if (result != FR_OK) { damaged = true; continue; }
    Journal data{}; UINT count;
    bool ok = f_size(&file) == sizeof(data) && f_read(&file, &data, sizeof(data), &count) == FR_OK &&
        count == sizeof(data) && !memcmp(data.magic, "APXRFW3", 8) && data.sequence &&
        data.operation && data.phase >= uint32_t(RfUpdatePhase::Ready) &&
        data.phase <= uint32_t(RfUpdatePhase::RecoveryRequired) &&
        data.crc == maintenanceCrc(reinterpret_cast<const uint8_t*>(&data), sizeof(data) - 4) &&
        data.image < sizeof(rfImages) / sizeof(rfImages[0]) &&
        !memcmp(data.uid, uid, sizeof(uid)) && data.target == NB4_UPDATE_TARGET;
    if (f_close(&file) != FR_OK) ok = false;
    if (ok) {
      const auto& image = rfImages[data.image];
      ok = data.identity.product == image.reportedProduct &&
          data.identity.version <= image.version &&
          (!data.peerLength || data.peerLength == (image.receiver ? 14u : 4u));
    }
    if (!ok || (valid && data.sequence == selected.sequence &&
                memcmp(&data, &selected, sizeof(data)))) damaged = true;
    if (ok && (!valid || data.sequence > selected.sequence)) { selected = data; valid = true; }
  }
  return valid;
}
bool checkpoint(void*, const uint8_t* peer, uint8_t length) {
  if (length > sizeof(pinnedPeer) || (pinnedPeerLength &&
      (length != pinnedPeerLength || memcmp(pinnedPeer, peer, length)))) return false;
  memcpy(pinnedPeer, peer, length); pinnedPeerLength = length;
#if defined(NB4_RF_UPDATE_BENCH)
  const auto began = get_tmr10ms();
#endif
  journalValid = journal(RfUpdatePhase::Transfer);
#if defined(NB4_RF_UPDATE_BENCH)
  checkpointMs = uint32_t(tmr10ms_t(get_tmr10ms() - began)) * 10;
#endif
  if (journalValid) releaseEntryRequest = true;
  return journalValid;
}
bool readImage(void*, uint32_t offset, uint8_t* output, size_t size) {
  UINT count;
  return open && f_lseek(&imageFile, offset) == FR_OK &&
         f_read(&imageFile, output, size, &count) == FR_OK && count == size;
}
bool send(void*, bool response, uint8_t opcode, uint8_t sequence,
          const uint8_t* data, uint8_t count) {
  if (transmit.load(std::memory_order_acquire) || count > sizeof(outgoing.data)) return false;
  outgoing = {uint8_t(response ? 0x10 : 3), opcode, sequence, count, {}};
  if (count) memcpy(outgoing.data, data, count);
  if (response && opcode == 0x22) releaseEntryRequest = true;
  transmit.store(true, std::memory_order_release);
  return true;
}
const RfUpdateIo io{nullptr, readImage, send, checkpoint};
void closeImage() {
  if (open) f_close(&imageFile);
  open = uploading = validating = assetUploading = false;
  if (storagePaused) { nb4StorageResume(); storagePaused = false; }
}
void error(char* output, size_t capacity, const char* reason) {
  snprintf(output, capacity, "{\"error\":\"%s\"}", reason);
}
void assetStatus(char* output, size_t capacity, unsigned index, bool verified) {
  snprintf(output, capacity, "{\"asset\":%u,\"verified\":%s,\"uploaded\":%lu,\"bytes\":%lu}",
           index, verified ? "true" : "false",
           (unsigned long)(verified ? rfImages[index].size : assetUploaded), (unsigned long)rfImages[index].size);
}
void status(char* output, size_t capacity) {
  snprintf(output, capacity,
      "{\"operation\":%lu,\"phase\":%u,\"error_code\":%u,\"uploaded\":%lu,"
      "\"validated\":%s,\"transferred\":%lu,\"cancellable\":%s,\"driving_locked\":%s}",
      (unsigned long)operation, unsigned(engine.phase()), unsigned(engine.error()),
      (unsigned long)uploaded, validated ? "true" : "false", (unsigned long)engine.transferred(),
      engine.cancellable() || uploading || (validated && !startRequested) ? "true" : "false", drivingLocked ? "true" : "false");
#if defined(NB4_RF_UPDATE_BENCH)
  const auto size = strlen(output);
  if (size && size < capacity)
    snprintf(output + size - 1, capacity - size + 1,
        ",\"entry_probe\":%s,\"boot_peer_bytes\":%lu,\"rx_wire_bytes\":%lu,\"rx_frames\":%lu,"
        "\"last_rx_meta\":%lu,\"last_rf_rx_meta\":%lu,\"last_rf_value\":%lu,\"tx_requests\":%lu,\"last_tx_opcode\":%lu,"
        "\"fault_flags\":%lu,\"tx_responses\":%lu,\"checkpoint_ms\":%lu,\"boot_reply\":[%lu,%lu,%lu,%lu],"
        "\"last_rx_value\":%lu,\"protocol\":%lu}",
        entryProbeStarted && !entryProbeFinished ? "true" : "false", (unsigned long)pinnedPeerLength,
        (unsigned long)rxWireBytes.load(), (unsigned long)rxFrames.load(), (unsigned long)lastRxMeta.load(),
        (unsigned long)lastRfRxMeta.load(), (unsigned long)lastRfValue.load(), (unsigned long)txRequests.load(),
        (unsigned long)lastTxOpcode.load(), (unsigned long)faultFlags.load(), (unsigned long)txResponses.load(),
        (unsigned long)checkpointMs.load(), (unsigned long)bootReplyWords[0].load(),
        (unsigned long)bootReplyWords[1].load(), (unsigned long)bootReplyWords[2].load(),
        (unsigned long)bootReplyWords[3].load(), (unsigned long)lastRxValue.load(),
        (unsigned long)afhds3::getMaintenanceDiagnostics(0));
#endif
}
const RfUpdateHardwareRule* hardwareRule(unsigned index, const RfUpdateIdentity& current) {
  for (const auto& rule : rfUpdateHardware) {
    if (rule.target == NB4_UPDATE_TARGET && rule.image == index &&
        rule.hardware == current.hardware && rule.rfHardware == current.rfHardware &&
        rule.bootloader == current.bootloader && rule.rfLibrary == current.rfLibrary &&
        current.product == rfImages[index].reportedProduct) return &rule;
  }
  return nullptr;
}
// Images the radio installs by itself: a production rule exists for this target.
bool installableImage(unsigned index) {
  for (const auto& rule : rfUpdateHardware)
    if (rule.target == NB4_UPDATE_TARGET && rule.image == index && rule.transferVerified && rule.recoveryVerified)
      return true;
  return false;
}
bool qualified(unsigned index, const RfUpdateIdentity& current) {
  const auto rule = hardwareRule(index, current);
  if (!rule) return false;
  if (rule->transferVerified && rule->recoveryVerified) return true;
#if defined(NB4_RF_UPDATE_BENCH)
  uint32_t uid[3]; unitId(uid); char serial[13];
  snprintf(serial, sizeof(serial), "%08lX%04lX", (unsigned long)(uid[0] + uid[2]),
           (unsigned long)(uid[1] >> 16));
  return benchArmed && !strcmp(serial, NB4_RF_UPDATE_BENCH_SERIAL);
#endif
  return false;
}
}

void nb4RfUpdateArmBench() { benchArmed = true; }
const char* nb4RfUpdateBuildKind() {
#if defined(NB4_RF_UPDATE_BENCH)
  return rfBenchMarker;
#else
  return "production";
#endif
}
bool nb4RfUpdateWriteSupported(bool receiver) {
  const auto current = snapshot(receiver);
  for (unsigned i = 0; i < sizeof(rfImages) / sizeof(rfImages[0]); ++i)
    if (rfImages[i].receiver == receiver && qualified(i, current)) return true;
  return false;
}
int nb4RfUpdateCandidate(bool receiver) {
  const auto current=snapshot(receiver);
  if (!current.product || !current.version) return -1;
  for (unsigned i=0;i<sizeof(rfImages)/sizeof(rfImages[0]);++i)
    if (rfImages[i].receiver==receiver && current.version<rfImages[i].version && hardwareRule(i,current))
      return int(i);
  return -1;
}

bool nb4RfUpdateStorageOwned() { return open || drivingLocked.load(); }
bool nb4RfUpdateRecoverySupported() {
  return journalValid && engine.phase() == RfUpdatePhase::RecoveryRequired && qualified(imageIndex, identity);
}
bool nb4RfUpdatePrepared() { return validated && !startRequested; }
bool nb4RfUpdateNeedsRecovery() { return engine.phase() == RfUpdatePhase::RecoveryRequired; }
int nb4RfUpdatePercent() {
  if (engine.phase() == RfUpdatePhase::Complete) return 100;
  if (!startRequested || !engine.size()) return -1;
  return int(engine.transferred() * 99 / engine.size());
}
bool nb4RfUpdateLocalStart(bool recovery) {
  if (!recovery) {
    const auto current = snapshot(rfImages[imageIndex].receiver);
    auto previous = identity; previous.epoch = current.epoch;
    if (!(previous == current)) return false;
    identity = current;
  }
  uint8_t payload[5];
  for (unsigned i = 0; i < 4; ++i) payload[i] = uint8_t(operation >> (i * 8));
  payload[4] = 1;
  char result[512];
  nb4RfUpdateCommand(recovery ? 21 : 18, payload, recovery ? 4 : 5, result, sizeof(result));
  return strstr(result, "\"error\"") == nullptr;
}
int nb4RfUpdateInstallable() { return installable; }
bool nb4RfUpdateLocalInstall() {
  const int index = installable;
  if (index < 0 || nb4RfUpdateStorageOwned() || !startupReady || uploading || validating || startRequested ||
      nb4RacePhase() == Nb4RacePhase::Running || usbPluggedInStorageMode() || !sdMounted() || g_vbat100mV < 38)
    return false;
  const bool receiver = rfImages[index].receiver;
  const auto candidate = snapshot(receiver);
  if (nb4RfUpdateCandidate(receiver) != index || !qualified(index, candidate)) return false;
  uint32_t local = (candidate.epoch + 1) * 2654435761u ^ (uint32_t(get_tmr10ms()) << 8);
  if (!local) local = 1;
  if (!engine.prepare(rfImages[index], candidate, local, false)) return false;
  imageIndex = index; operation = local; identity = candidate;
  entryProbeStarted = entryProbeFinished = false;
  uploading = validated = startRequested = recovering = false; uploaded = rfImages[index].size;
  pinnedPeerLength = 0; memset(pinnedPeer, 0, sizeof(pinnedPeer));
  // A local installation has no desktop backup; the journal records the image itself.
  memcpy(backupHash, rfImages[index].sha256, 32);
  journalValid = validated = journal(RfUpdatePhase::Ready);
  installableDirty = true;
  return validated && nb4RfUpdateLocalStart(false);
}
bool nb4RfUpdateDrivingLocked() { return drivingLocked.load(); }
bool nb4RfUpdateFrame(afhds3::Transport& transport) {
  if (abortTransport.exchange(false)) {
    awaitingModuleEntryAck = false;
    transport.clear(); transport.skipFrame(); return true;
  }
  if (releaseEntryRequest.exchange(false)) {
    awaitingModuleEntryAck = false;
    transport.cancelPendingRequest(); requestPending = false;
  }
  if (!startupReady.load()) { transport.skipFrame(); return true; }
  if (transmit.load(std::memory_order_acquire)) {
    bool error = false;
    if (outgoing.type == 0x10) {
      if (!transport.queueResponse(afhds3::COMMAND(outgoing.opcode), outgoing.sequence,
                                   outgoing.data, outgoing.size)) { transport.skipFrame(); return true; }
#if defined(NB4_RF_UPDATE_BENCH)
      ++txResponses;
#endif
      transport.skipFrame();
    } else {
      if (transport.waiting() && transport.handleRetransmissions(error)) return true;
      if (error) {
        overflow.store(true);
#if defined(NB4_RF_UPDATE_BENCH)
        faultFlags.fetch_or(4);
#endif
      }
      transport.putFrame(afhds3::COMMAND(outgoing.opcode), afhds3::REQUEST_SET_EXPECT_ACK,
                          outgoing.data, outgoing.size);
      awaitingModuleEntryAck = outgoing.opcode == 0x23;
#if defined(NB4_RF_UPDATE_BENCH)
      ++txRequests; lastTxOpcode = outgoing.opcode;
#endif
      requestPending = true;
    }
    transmit.store(false, std::memory_order_release);
    return true;
  }
  if (linkOwned.load()) {
    bool error = false;
    if (transport.waiting() && transport.handleRetransmissions(error)) return true;
    if (error) {
      overflow.store(true);
#if defined(NB4_RF_UPDATE_BENCH)
      faultFlags.fetch_or(4);
#endif
    }
    transport.skipFrame(); return true;
  }
  return false;
}
void nb4RfUpdateReceive(uint8_t type, uint8_t opcode, uint8_t sequence,
                        const uint8_t* data, size_t length) {
#if defined(NB4_RF_UPDATE_BENCH)
  if (type == 3 && opcode == 0x33 && length == 14) {
    for (unsigned i = 0; i < 3; ++i) bootReplyWords[i] = get32(data + i * 4);
    bootReplyWords[3] = data[12] | uint32_t(data[13]) << 8;
  }
  if (linkOwned.load() || drivingLocked.load()) {
    ++rxFrames;
    const uint32_t meta = type | uint32_t(opcode) << 8 | uint32_t(sequence) << 16 | uint32_t(length & 255) << 24;
    lastRxMeta = meta;
    uint32_t rxValue = 0;
    for (unsigned i=0;i<std::min<size_t>(length,4);++i) rxValue |= uint32_t(data[i]) << (i*8);
    lastRxValue = rxValue;
    if (opcode >= 0x22 && opcode <= 0x33) {
      lastRfRxMeta = meta; lastRfValue = rxValue;
    }
  }
#endif
  if (!linkOwned.load()) return;
  if (opcode < 0x22 || opcode > 0x33) return;
  // The bootloader may already be running and never acknowledge the entry
  // command. Stop its retries before a durable checkpoint can block the UI.
  if (((type == 2 && ((opcode == 0x22 && !length) || (opcode == 0x24 && length == 4))) ||
       (type == 1 && opcode == 0x25 && !length)) &&
      awaitingModuleEntryAck.exchange(false)) releaseEntryRequest = true;
  if (type == 0x20 || type == 0x10) requestPending = false;
  if (type == 0x20) return;
  if (length > sizeof(ReceivedPacket::data)) {
    overflow.store(true);
#if defined(NB4_RF_UPDATE_BENCH)
    faultFlags.fetch_or(1);
#endif
    return;
  }
  const auto previous = receivedGeneration.load(std::memory_order_acquire);
  // Coalesce only a retry still waiting in the queue. Once consumed, another
  // retry must be accepted in case its response was delayed or lost.
  if (previous != consumedGeneration.load(std::memory_order_acquire) &&
      type == lastReceived.type && opcode == lastReceived.opcode &&
      sequence == lastReceived.sequence && length == lastReceived.size &&
      (!length || !memcmp(data, lastReceived.data, length))) return;
  if (received.isFull()) {
    overflow.store(true);
#if defined(NB4_RF_UPDATE_BENCH)
    faultFlags.fetch_or(2);
#endif
    return;
  }
  ReceivedPacket packet{type, opcode, sequence, uint8_t(length), {}, previous + 1};
  if (length) memcpy(packet.data, data, length);
  lastReceived = packet;
  received.push(packet);
  receivedGeneration.store(packet.generation, std::memory_order_release);
}
#if defined(NB4_RF_UPDATE_BENCH)
void nb4RfUpdateRxByte() { if (linkOwned.load() || drivingLocked.load()) ++rxWireBytes; }
#endif
bool nb4RfUpdateCommand(uint8_t command, const uint8_t* payload, size_t length,
                         char* output, size_t capacity) {
  if (command < 16 || command > 23) return false;
  if (command == 19) {
    if (length != 4 || (get32(payload) && get32(payload) != operation)) error(output, capacity, "operation_mismatch");
    else status(output, capacity);
    return true;
  }
  if (command == 16) {
    // index(1), operation(4), epoch(4), car(4), verified-backup SHA-256(32).
    if (length != 45 || payload[0] >= sizeof(rfImages) / sizeof(rfImages[0]) || !get32(payload + 1)) {
      error(output, capacity, "invalid_prepare"); return true;
    }
    if (nb4RfUpdateStorageOwned() || !startupReady || nb4RacePhase() == Nb4RacePhase::Running ||
        usbPluggedInStorageMode() || !sdMounted()) { error(output, capacity, "maintenance_busy"); return true; }
    const unsigned candidateIndex = payload[0];
    const auto candidate = snapshot(rfImages[candidateIndex].receiver);
    uint8_t any = 0; for (unsigned i = 13; i < 45; ++i) any |= payload[i];
    if (!any || !candidate.product || !candidate.version || candidate.product != rfImages[candidateIndex].reportedProduct ||
        candidate.epoch != get32(payload + 5) || candidate.car != get32(payload + 9)) {
      error(output, capacity, "inventory_or_backup_required"); return true;
    }
    if (!hardwareRule(candidateIndex, candidate)) {
      error(output, capacity, "hardware_identity_not_supported"); return true;
    }
    if (candidate.version > rfImages[candidateIndex].version) {
      error(output, capacity, "image_version_not_allowed"); return true;
    }
    if (!nb4StorageQuiesce()) { error(output, capacity, "storage_busy"); return true; }
    storagePaused = true;
    if (!headroom(rfImages[candidateIndex].size + 262144u)) {
      closeImage(); error(output, capacity, "storage_headroom_required"); return true;
    }
    FRESULT result = f_mkdir("/FIRMWARE");
    if (result == FR_OK || result == FR_EXIST) result = f_mkdir("/FIRMWARE/RF");
    if ((result != FR_OK && result != FR_EXIST) || f_open(&imageFile, stagePath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
      closeImage(); error(output, capacity, "storage_failed"); return true;
    }
    open = true;
    if (!engine.prepare(rfImages[candidateIndex], candidate, get32(payload + 1), true)) {
      closeImage(); error(output, capacity, "image_version_not_allowed"); return true;
    }
    imageIndex = candidateIndex; operation = get32(payload + 1); identity = candidate;
    entryProbeStarted = entryProbeFinished = false;
    uploading = true; validated = startRequested = recovering = false; uploaded = 0;
    pinnedPeerLength = 0; memset(pinnedPeer, 0, sizeof(pinnedPeer));
    memcpy(backupHash, payload + 13, 32); new (&imageHash) Sha256();
    status(output, capacity); return true;
  }
  if (command == 22) {
    // index(1). A verified asset is reported as present; otherwise a stage
    // file opens for AssetData. Images without a production rule are refused.
    if (length != 1 || payload[0] >= sizeof(rfImages) / sizeof(rfImages[0])) {
      error(output, capacity, "invalid_asset"); return true;
    }
    const unsigned index = payload[0];
    if (!installableImage(index)) { error(output, capacity, "image_not_installable"); return true; }
    if (nb4RfUpdateStorageOwned() || !startupReady || uploading || validating || startRequested ||
        nb4RacePhase() == Nb4RacePhase::Running || usbPluggedInStorageMode() || !sdMounted()) {
      error(output, capacity, "maintenance_busy"); return true;
    }
    if (!nb4StorageQuiesce()) { error(output, capacity, "storage_busy"); return true; }
    storagePaused = true;
    if (assetVerified(index)) { closeImage(); assetStatus(output, capacity, index, true); return true; }
    if (!headroom(rfImages[index].size + 262144u)) {
      closeImage(); error(output, capacity, "storage_headroom_required"); return true;
    }
    FRESULT result = f_mkdir("/FIRMWARE");
    if (result == FR_OK || result == FR_EXIST) result = f_mkdir("/FIRMWARE/RF");
    if ((result != FR_OK && result != FR_EXIST) || f_open(&imageFile, stagePath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
      closeImage(); error(output, capacity, "storage_failed"); return true;
    }
    open = assetUploading = true; assetIndex = index; assetUploaded = 0; new (&imageHash) Sha256();
    assetStatus(output, capacity, index, false); return true;
  }
  if (command == 23) {
    // index(1), offset(4), data. The asset becomes visible only once it hashes.
    if (!assetUploading || length <= 5 || payload[0] != assetIndex || get32(payload + 1) != assetUploaded ||
        length - 5 > rfImages[assetIndex].size - assetUploaded) {
      error(output, capacity, "upload_range"); return true;
    }
    UINT count;
    if (f_write(&imageFile, payload + 5, length - 5, &count) != FR_OK || count != length - 5) {
      closeImage(); error(output, capacity, "storage_full"); return true;
    }
    imageHash.update(payload + 5, count); assetUploaded += count;
    bool verified = false;
    if (assetUploaded == rfImages[assetIndex].size) {
      uint8_t digest[32]; imageHash.finish(digest);
      bool ok = !memcmp(digest, rfImages[assetIndex].sha256, 32) && f_sync(&imageFile) == FR_OK;
      if (f_close(&imageFile) != FR_OK) ok = false;
      open = assetUploading = false;
      char path[48]; assetPathFor(assetIndex, path, sizeof(path));
      if (ok) {
        const auto result = f_unlink(path);
        ok = (result == FR_OK || result == FR_NO_FILE) && f_rename(stagePath, path) == FR_OK;
      }
      closeImage(); installableDirty = true; nb4RfNoticeInvalidate();
      if (!ok) { f_unlink(stagePath); error(output, capacity, "asset_verification_failed"); return true; }
      verified = true;
    }
    assetStatus(output, capacity, assetIndex, verified); return true;
  }
  if (length < 4 || get32(payload) != operation || !operation) {
    error(output, capacity, "operation_mismatch"); return true;
  }
  if (command == 17) {
    if (!uploading || length <= 8 || get32(payload + 4) != uploaded ||
        uploaded > rfImages[imageIndex].size || length - 8 > rfImages[imageIndex].size - uploaded) {
      error(output, capacity, "upload_range"); return true;
    }
    UINT count;
    if (f_write(&imageFile, payload + 8, length - 8, &count) != FR_OK || count != length - 8) {
      closeImage(); error(output, capacity, "storage_full"); return true;
    }
    imageHash.update(payload + 8, count); uploaded += count;
    if (uploaded == rfImages[imageIndex].size) {
      uint8_t digest[32]; imageHash.finish(digest);
      bool ok = !memcmp(digest, rfImages[imageIndex].sha256, 32) && f_sync(&imageFile) == FR_OK;
      if (f_close(&imageFile) != FR_OK) ok = false;
      open = uploading = false;
      char path[48]; assetPath(path, sizeof(path));
      if (ok) {
        // Replacing a previously verified asset never removes a personal file.
        // Existing paths are reserved exclusively for this catalog's product.
        auto result = f_unlink(path);
        ok = (result == FR_OK || result == FR_NO_FILE) && f_rename(stagePath, path) == FR_OK;
      }
      if (ok) { journalValid = journal(RfUpdatePhase::Ready); ok = journalValid; }
      validated = ok;
      if (ok) nb4RfNoticeInvalidate();
      closeImage();
      if (!ok) { error(output, capacity, "image_verification_failed"); return true; }
    }
  } else if (command == 18) {
    if (startRequested || validating || uploading || linkOwned.load()) {
      error(output, capacity, "operation_in_progress"); return true;
    }
    if (length != 5 || payload[4] > 1 || !validated || !qualified(imageIndex, snapshot(rfImages[imageIndex].receiver))) {
      error(output, capacity, "hardware_qualification_required"); return true;
    }
    if (!(snapshot(rfImages[imageIndex].receiver) == identity) || g_vbat100mV < 38 ||
        nb4RacePhase() == Nb4RacePhase::Running || !nb4StorageQuiesce()) {
      error(output, capacity, "identity_power_or_storage_changed"); return true;
    }
    storagePaused = true;
    reinstall = payload[4] == 1; recovering = false;
    char path[48]; assetPath(path, sizeof(path));
    open = f_open(&imageFile, path, FA_READ) == FR_OK;
    if (!open || f_size(&imageFile) != rfImages[imageIndex].size) {
      closeImage(); error(output, capacity, "asset_missing"); return true;
    }
    open = validating = startRequested = true; validationOffset = 0;
    new (&imageHash) Sha256();
  } else if (command == 21) {
    if (startRequested || validating || open) {
      error(output, capacity, "operation_in_progress"); return true;
    }
    if (length != 4 || engine.phase() != RfUpdatePhase::RecoveryRequired || !journalValid ||
        !qualified(imageIndex, identity) || snapshot(rfImages[imageIndex].receiver).car != identity.car ||
        g_vbat100mV < 38 || !nb4StorageQuiesce()) {
      error(output, capacity, "recovery_identity_or_qualification_required"); return true;
    }
    storagePaused = true;
    char path[48]; assetPath(path, sizeof(path));
    open = f_open(&imageFile, path, FA_READ) == FR_OK;
    if (!open || f_size(&imageFile) != rfImages[imageIndex].size) {
      closeImage(); error(output, capacity, "recovery_asset_missing"); return true;
    }
    recovering = validating = startRequested = true;
    entryProbeStarted = entryProbeFinished = false;
    validationOffset = 0; new (&imageHash) Sha256();
  } else if (command == 20) {
    const bool safe = !recovering && (uploading || validating || engine.cancellable() ||
        (validated && !startRequested && !engine.ownsLink() &&
         engine.phase() != RfUpdatePhase::VerifyVersion));
    if (length != 4 || !safe) {
      error(output, capacity, "cannot_cancel_after_entry"); return true;
    }
    if (engine.cancellable()) engine.cancel();
    closeImage(); validated = startRequested = false;
    if (journalValid) journalValid = journal(RfUpdatePhase::Cancelled);
  }
  status(output, capacity); return true;
}

void nb4RfUpdateService() {
  if (disconnected.exchange(false) && (uploading || validating || assetUploading)) {
    startRequested = validated = false; closeImage();
  }
  if (initializing && sdMounted() && !usbPluggedInStorageMode()) {
    Journal data{}; bool present = false, damaged = false;
    journalValid = readJournal(data, present, damaged);
    journalDamaged = damaged;
    if (journalValid) {
      journalSequence = data.sequence;
      operation = data.operation; imageIndex = data.image; identity = data.identity;
      memcpy(backupHash, data.backup, 32);
      pinnedPeerLength = data.peerLength; memcpy(pinnedPeer, data.peer, sizeof(pinnedPeer));
      if (!damaged && data.phase == uint32_t(RfUpdatePhase::Ready)) {
        validated = true;
        uploaded = rfImages[imageIndex].size;
        if (!engine.prepare(rfImages[imageIndex], identity, operation, true)) {
          validated = false; drivingLocked = linkOwned = true;
          engine.requireRecovery(RfUpdateError::Storage);
        }
      } else if (damaged || (data.phase != uint32_t(RfUpdatePhase::Complete) &&
                            data.phase != uint32_t(RfUpdatePhase::Cancelled))) {
        drivingLocked = linkOwned = true;
        engine.prepare(rfImages[imageIndex], identity, operation, true);
        engine.requireRecovery(RfUpdateError::Storage);
      } else if (!engine.restoreResult(rfImages[imageIndex], identity, operation, RfUpdatePhase(data.phase))) {
        drivingLocked = linkOwned = true;
        engine.requireRecovery(RfUpdateError::Storage);
      } else if (data.phase == uint32_t(RfUpdatePhase::Complete)) {
        uploaded = rfImages[imageIndex].size;
      }
    } else if (present) {
      drivingLocked = linkOwned = true;
      engine.requireRecovery(RfUpdateError::Storage);
    }
    startupReady = true; initializing = false;
  }
  // Allow the normal handshake to identify an application that returned after
  // an interrupted operation. Driving stays locked. Abandonment still requires
  // no boot checkpoint, no image bytes and a fresh original identity; a peer
  // checkpoint never becomes permission to unlock on a cached version.
  if (journalValid && !journalDamaged &&
      engine.phase() == RfUpdatePhase::RecoveryRequired && !startRequested) {
    if (!entryProbeStarted) {
      entryProbeStarted = true; entryProbeFinished = false;
      entryProbeEpoch = afhds3::getVersionEpoch(0);
      transmit = requestPending = overflow = false; abortTransport = true;
      linkOwned = false;
    } else if (!entryProbeFinished && engine.cancelUntouchedApplication(snapshot(rfImages[imageIndex].receiver), entryProbeEpoch)) {
      entryProbeFinished = true;
      if (journal(RfUpdatePhase::Cancelled)) {
        validated = false; drivingLocked = linkOwned = false;
      } else engine.requireRecovery(RfUpdateError::Storage);
    } else if (!entryProbeFinished && engine.completeInstalledImage(snapshot(rfImages[imageIndex].receiver), entryProbeEpoch)) {
      // The component came back running the intended image: close the
      // operation on that evidence instead of writing the image again.
      entryProbeFinished = true;
      if (journal(RfUpdatePhase::Complete)) {
        validated = false; uploaded = rfImages[imageIndex].size;
        drivingLocked = linkOwned = false;
      } else engine.requireRecovery(RfUpdateError::Storage);
    }
  }
  if (startupReady && !open && !startRequested && sdMounted() &&
      (installableDirty || installableEpoch != afhds3::getVersionEpoch(0))) {
    installableDirty = false; installableEpoch = afhds3::getVersionEpoch(0); installable = -1;
    if (engine.phase() != RfUpdatePhase::RecoveryRequired)
      for (bool receiver : {true, false}) {
        const int index = nb4RfUpdateCandidate(receiver);
        if (index >= 0 && qualified(index, snapshot(receiver)) && assetPresent(index)) { installable = index; break; }
      }
  }
  const auto now = uint32_t(get_tmr10ms()) * 10;
  if (validating) {
    uint8_t block[512];
    const auto count = std::min<uint32_t>(sizeof(block), rfImages[imageIndex].size - validationOffset);
    if (!readImage(nullptr, validationOffset, block, count)) {
      if (!recovering) engine.fail(RfUpdateError::Storage);
      startRequested = validated = false; closeImage(); return;
    }
    imageHash.update(block, count); validationOffset += count;
    if (validationOffset == rfImages[imageIndex].size) {
      uint8_t digest[32]; imageHash.finish(digest); validating = false;
      if (memcmp(digest, rfImages[imageIndex].sha256, 32) ||
          (!recovering && !(snapshot(rfImages[imageIndex].receiver) == identity)) ||
          (recovering && snapshot(rfImages[imageIndex].receiver).car != identity.car) ||
          (!recovering && !engine.prepare(rfImages[imageIndex], identity, operation, reinstall)) ||
          !journal(RfUpdatePhase::Entering)) {
        if (!recovering) engine.fail(RfUpdateError::Storage);
        startRequested = validated = false; closeImage(); return;
      }
      linkOwned = false;
      received.clear(); consumedGeneration = receivedGeneration.load();
      transmit = overflow = requestPending = false;
      abortTransport = true;
#if defined(NB4_RF_UPDATE_BENCH)
      rxWireBytes = rxFrames = lastRxMeta = lastRfRxMeta = lastRfValue = txRequests = lastTxOpcode = 0;
      faultFlags = txResponses = 0;
      checkpointMs = lastRxValue = 0;
#endif
      verifyBaseline = afhds3::getVersionEpoch(0);
      drivingLocked = linkOwned = true;
      const bool started = recovering ?
          engine.recover(rfImages[imageIndex], identity, operation, pinnedPeer, pinnedPeerLength, now) :
          engine.start(identity, now);
      if (!started) engine.requireRecovery(RfUpdateError::Protocol);
    }
  }
  if (overflow.exchange(false)) engine.fail(RfUpdateError::Protocol);
  ReceivedPacket packet;
  for (unsigned i = 0; i < 4 && received.pop(packet); ++i) {
    consumedGeneration.store(packet.generation, std::memory_order_release);
    engine.receive(packet.type, packet.opcode, packet.sequence, packet.data, packet.size, now, io);
  }
  engine.service(now, io);
  if (startRequested && engine.phase() == RfUpdatePhase::VerifyVersion &&
      !transmit.load() && !requestPending.load()) {
    // A fresh normal handshake must report the installed version. The RF
    // driver invalidates its caches when it releases exclusive ownership.
    linkOwned = false;
    auto current = snapshot(engine.receiver());
    if (current.car != identity.car) engine.fail(RfUpdateError::IdentityChanged);
    else engine.verifyVersion(current.product, current.version,
          current.epoch != verifyBaseline && current.product);
  }
  if (startRequested && !validating && (engine.phase() == RfUpdatePhase::Complete ||
      engine.phase() == RfUpdatePhase::Cancelled || engine.phase() == RfUpdatePhase::RecoveryRequired)) {
    const bool ok = journal(engine.phase());
    if (!ok) {
      engine.requireRecovery(RfUpdateError::Storage);
    }
    if (engine.phase() != RfUpdatePhase::RecoveryRequired && ok) drivingLocked = linkOwned = false;
    else {
      drivingLocked = linkOwned = true;
      transmit = requestPending = false;
      abortTransport = true;
    }
    startRequested = validated = false; closeImage(); installableDirty = true;
  }
}
void nb4RfUpdateUsbDisconnected() { disconnected = true; }
#if defined(SIMU)
bool nb4RfUpdateTestEntryProbeActive() { return entryProbeStarted && !entryProbeFinished; }
void nb4RfUpdateTestRequireRecovery(bool allowInventory) {
  initializing = false; journalValid = false;
  engine.requireRecovery(RfUpdateError::Timeout); drivingLocked = true;
  linkOwned = !allowInventory;
}
void nb4RfUpdateTestReset() {
  closeImage();
  engine = RfUpdateCore{};
  operation = uploaded = journalSequence = pinnedPeerLength = 0;
  imageIndex = validationOffset = verifyBaseline = 0;
  identity = {};
  memset(backupHash, 0, sizeof(backupHash)); memset(pinnedPeer, 0, sizeof(pinnedPeer));
  validated = startRequested = recovering = journalValid = benchArmed = false;
  journalDamaged = entryProbeStarted = entryProbeFinished = false;
  assetUploading = installableDirty = false; assetIndex = assetUploaded = 0;
  installable = -1; installableEpoch = UINT32_MAX; installableDirty = true;
  linkOwned = drivingLocked = false; startupReady = true;
  transmit = overflow = requestPending = abortTransport = disconnected = releaseEntryRequest = false;
  awaitingModuleEntryAck = false;
  initializing = true; received.clear();
  receivedGeneration = consumedGeneration = 0; lastReceived = {};
#if defined(NB4_RF_UPDATE_BENCH)
  for (auto& word : bootReplyWords) word = 0;
#endif
}
void nb4RfUpdateTestStartModule() {
  initializing = false; startupReady = true; imageIndex = 0; operation = 17;
  identity = {1, 1, rfImages[0].reportedProduct, 0x10003, rfImages[0].version, 0, 0, 0x30000};
  engine.prepare(rfImages[0], identity, operation, true);
  const auto now = uint32_t(get_tmr10ms()) * 10;
  engine.start(identity, now); drivingLocked = linkOwned = true;
  engine.service(now, io);
}
void nb4RfUpdateTestBeginValidation(bool recovery) {
  recovering = recovery; startRequested = validating = true;
  char path[48]; assetPath(path, sizeof(path));
  open = f_open(&imageFile, path, FA_READ) == FR_OK;
  validationOffset = 0; new (&imageHash) Sha256();
}
#endif
#endif
