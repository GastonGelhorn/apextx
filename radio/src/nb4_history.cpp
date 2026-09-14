/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_history.h"
#include "nb4_health.h"
#include "storage/sdcard_yaml.h"
#include "ff.h"
#include "hal/usb_driver.h"
#include "os/task.h"
#include "os/sleep.h"
#include "tasks.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char* directory = "/LOGS/NB4";
enum SlotState : uint8_t { Empty, Reserved, Pending, Failed };
struct Slot { std::atomic<uint8_t> state{Empty}; Nb4RaceRecord record{}; };
Slot slots[2];
std::atomic<Nb4HistoryStatus> status{Nb4HistoryStatus::Idle};
std::atomic<bool> retry{false}, logRequested{false};
// One UI request, one completed immutable reply. Producers never wait.
std::atomic<uint8_t> queryState{0}; // 0 idle, 1 requested, 2 complete, 3 being read
uint32_t nextToken = 0, queryToken = 0, queryBefore = 0;
uint32_t lastAssignedId = 0; // storage-worker-owned; never read a Reserved record
bool usbOwnsStorage() { return usbStarted() && getSelectedUsbMode() == USB_MASS_STORAGE_MODE; }
bool queryDetail = false;
Nb4HistoryView response;
FIL file __DMA;
DIR dir;
FILINFO info;
char line[192], readBuffer[256];
unsigned readPos = 0, readLength = 0;
Nb4RaceRecord scratch;
task_handle_t storageTask;
TASK_DEFINE_STACK(storageStack, 1024);
bool started = false;
std::atomic<bool> paused{false}, busy{false};

void filename(char* out, uint32_t id, bool temporary = false) {
  snprintf(out, 64, "%s/race-%010lu.%s", directory, (unsigned long)id, temporary ? "tmp" : "yml");
}
uint32_t fileId(const char* name) {
  if (strlen(name) != 19 || strncmp(name, "race-", 5) || strcmp(name + 15, ".yml")) return 0;
  uint64_t n = 0;
  for (int i = 5; i < 15; ++i) {
    if (name[i] < '0' || name[i] > '9') return 0;
    n = n * 10 + name[i] - '0';
  }
  return n <= UINT32_MAX ? n : 0;
}
FRESULT scanIds(uint32_t before, uint32_t* ids, unsigned capacity, bool* more = nullptr) {
  memset(ids, 0, capacity * sizeof(uint32_t));
  if (more) *more = false;
  FRESULT result = f_opendir(&dir, directory);
  if (result == FR_NO_PATH) return FR_OK;
  if (result != FR_OK) return result;
  for (;;) {
    result = f_readdir(&dir, &info);
    if (result != FR_OK || !info.fname[0]) break;
    uint32_t id = (info.fattrib & AM_DIR) ? 0 : fileId(info.fname);
    if (!id || (before && id >= before)) continue;
    for (unsigned i = 0; i < capacity; ++i) if (id > ids[i]) {
      for (unsigned j = capacity - 1; j > i; --j) ids[j] = ids[j - 1];
      ids[i] = id; break;
    }
    if (more && ids[capacity - 1] && id < ids[capacity - 1]) *more = true;
  }
  f_closedir(&dir);
  return result;
}
FRESULT writeText(const char* text) {
  UINT written = 0;
  auto result = f_write(&file, text, strlen(text), &written);
  return result == FR_OK && written != strlen(text) ? FR_DISK_ERR : result;
}
uint32_t checksum(const Nb4RaceRecord& r) {
  // Format-level checksum is independent of ABI padding/endian representation.
  uint32_t h = 2166136261u;
  auto number = [&h](uint32_t n) { for (unsigned b = 0; b < 4; ++b) { h = (h ^ uint8_t(n)) * 16777619u; n >>= 8; } };
  number(1); number(r.id); number(r.date); number(r.duration); number(r.best);
  number(r.last); number(r.average); number(r.laps);
  for (unsigned i = 0; i < 33; ++i) h = (h ^ uint8_t(r.model[i])) * 16777619u;
  for (unsigned i = 0; i < r.laps; ++i) number(r.times[i]);
  return h;
}
bool readLine() {
  unsigned len = 0; UINT read = 0; char c;
  for (;;) {
    if (readPos >= readLength) {
      if (f_read(&file, readBuffer, sizeof(readBuffer), &read) != FR_OK || !read) return false;
      readPos = 0; readLength = read;
    }
    c = readBuffer[readPos++];
    if (c == '\n') { line[len] = 0; return true; }
    if (len + 1 >= sizeof(line)) return false;
    line[len++] = c;
  }
}
bool readNumber(const char* key, uint32_t& n) {
  if (!readLine() || strncmp(line, key, strlen(key))) return false;
  char* end = nullptr;
  const char* value = line + strlen(key);
  if (*value < '0' || *value > '9') return false;
  auto v = strtoull(value, &end, 10);
  if (*end || v > UINT32_MAX) return false;
  n = v; return true;
}
FRESULT readRecord(uint32_t id, Nb4RaceRecord& r) {
  memset(&r, 0, sizeof(r));
  char path[64]; filename(path, id);
  auto res = f_open(&file, path, FA_READ);
  if (res != FR_OK) return res;
  readPos = readLength = 0;
  uint32_t version = 0, count = 0, crc = 0;
  bool ok = readNumber("version: ", version) && version == 1 &&
    readNumber("id: ", r.id) && r.id == id && readNumber("date: ", r.date) &&
    readLine() && !strncmp(line, "model: '", 8);
  if (ok) {
    unsigned pos = 8, out = 0;
    while (line[pos] && out < sizeof(r.model) - 1) {
      if (line[pos] == '\'') {
        if (line[pos + 1] != '\'') break;
        ++pos;
      }
      r.model[out++] = line[pos++];
    }
    ok = line[pos] == '\'' && !line[pos + 1];
  }
  ok = ok && readNumber("duration: ", r.duration) && readNumber("laps: ", count) && count <= NB4_MAX_LAPS &&
    readNumber("best: ", r.best) && readNumber("last: ", r.last) && readNumber("average: ", r.average) &&
    readLine() && !strcmp(line, "times:");
  r.laps = count <= NB4_MAX_LAPS ? count : 0;
  for (unsigned i = 0; ok && i < r.laps; ++i) ok = readNumber("  - ", r.times[i]);
  ok = ok && readNumber("checksum: ", crc) && crc == checksum(r) && readPos == readLength && f_eof(&file);
  f_close(&file);
  return ok ? FR_OK : FR_INT_ERR;
}
FRESULT saveRecord(Nb4RaceRecord& r) {
  if (!sdHasSpaceFor(4096)) return FR_DENIED;
  auto result = f_mkdir("/LOGS");
  if (result != FR_OK && result != FR_EXIST) return result;
  result = f_mkdir(directory);
  if (result != FR_OK && result != FR_EXIST) return result;
  if (!r.id) {
    uint32_t highest;
    result = scanIds(0, &highest, 1);
    if (result != FR_OK || highest == UINT32_MAX) return result == FR_OK ? FR_DENIED : result;
    highest = max(highest, lastAssignedId);
    if (highest == UINT32_MAX) return FR_DENIED;
    r.id = highest + 1; lastAssignedId = r.id;
  }
  char final[64], temporary[64];
  filename(final, r.id); filename(temporary, r.id, true);
  // A rename may have succeeded before an I/O error was returned. A retry must
  // recognize that exact result and never create a second race or overwrite one.
  result = f_stat(final, &info);
  if (result == FR_OK) {
    result = readRecord(r.id, scratch);
    return result == FR_OK && checksum(scratch) == checksum(r) ? FR_OK : FR_EXIST;
  }
  if (result != FR_NO_FILE) return result;
  result = f_open(&file, temporary, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK) return result;
  snprintf(line, sizeof(line), "version: 1\nid: %lu\ndate: %lu\nmodel: '", (unsigned long)r.id, (unsigned long)r.date);
  result = writeText(line);
  for (unsigned i = 0; result == FR_OK && r.model[i] && i < sizeof(r.model); ++i) {
    char c[3] = {r.model[i], 0, 0};
    if (c[0] == '\'') c[1] = '\'';
    if (uint8_t(c[0]) < 32) c[0] = ' ';
    result = writeText(c);
  }
  if (result == FR_OK) {
    snprintf(line, sizeof(line), "'\nduration: %lu\nlaps: %u\nbest: %lu\nlast: %lu\naverage: %lu\ntimes:\n",
      (unsigned long)r.duration, r.laps, (unsigned long)r.best, (unsigned long)r.last, (unsigned long)r.average);
    result = writeText(line);
  }
  for (unsigned i = 0; result == FR_OK && i < r.laps; ++i) {
    snprintf(line, sizeof(line), "  - %lu\n", (unsigned long)r.times[i]); result = writeText(line);
  }
  if (result == FR_OK) {
    snprintf(line, sizeof(line), "checksum: %lu\n", (unsigned long)checksum(r)); result = writeText(line);
  }
  if (result == FR_OK) result = f_sync(&file);
  auto closed = f_close(&file);
  if (result == FR_OK) result = closed;
  if (result == FR_OK) result = f_rename(temporary, final);
  return result;
}
void processQuery() {
  if (queryState.load(std::memory_order_acquire) != 1) return;
  memset(&response, 0, sizeof(response));
  response.token = queryToken; response.before = queryBefore; response.detail = queryDetail;
  if (!sdMounted() || usbOwnsStorage()) response.error = FR_NOT_READY;
  else if (queryDetail) {
    response.error = readRecord(queryBefore, response.records[0]);
    response.count = response.error ? 0 : 1;
  } else {
    uint32_t ids[NB4_HISTORY_PAGE_SIZE + 1];
    response.error = scanIds(queryBefore, ids, NB4_HISTORY_PAGE_SIZE + 1);
    response.more = ids[NB4_HISTORY_PAGE_SIZE] != 0;
    for (unsigned i = 0; !response.error && i < NB4_HISTORY_PAGE_SIZE && ids[i]; ++i) {
      auto& record = response.records[response.count++];
      // A damaged record remains visible, identified by id, rather than skipped.
      if (readRecord(ids[i], record) != FR_OK) {
        memset(&record, 0, sizeof(record)); record.id = ids[i];
      }
    }
  }
  queryState.store(2, std::memory_order_release);
}
void storageWorker() { while (task_running()) { nb4StorageProcess(); sleep_ms(25); } }
}

bool nb4HistoryCanReserve() { for (auto& slot : slots) if (slot.state.load() == Empty) return true; return false; }
int nb4HistoryReserve() {
  for (unsigned i = 0; i < 2; ++i) {
    uint8_t empty = Empty;
    if (slots[i].state.compare_exchange_strong(empty, Reserved)) return i;
  }
  status.store(Nb4HistoryStatus::Full); return -1;
}
void nb4HistoryRelease(int i) { if (i >= 0 && i < 2 && slots[i].state.load() == Reserved) slots[i].state.store(Empty); }
void nb4HistoryPublish(int i, const Nb4RaceRecord& record) {
  if (i < 0 || i >= 2) return;
  slots[i].record = record;
  for (auto& c : slots[i].record.model) if (c && uint8_t(c) < 32) c = ' ';
  slots[i].state.store(Pending, std::memory_order_release);
  status.store(Nb4HistoryStatus::Pending);
}
unsigned nb4HistoryPending() { unsigned n = 0; for (auto& s : slots) if (s.state.load() >= Pending) ++n; return n; }
Nb4HistoryStatus nb4HistoryStatus() { return status.load(); }
void nb4HistoryRetry() { retry.store(true); }
uint32_t nb4HistoryRequest(uint32_t before, bool detail) {
  uint8_t state = queryState.load();
  if (state == 1 || state == 3) return 0;
  queryToken = ++nextToken; queryBefore = before; queryDetail = detail;
  queryState.store(1, std::memory_order_release); return queryToken;
}
bool nb4HistoryPoll(uint32_t token, Nb4HistoryView& view) {
  uint8_t complete = 2;
  if (!queryState.compare_exchange_strong(complete, 3)) return false;
  bool matching = response.token == token;
  if (matching) view = response;
  queryState.store(0); return matching;
}
bool nb4StorageQuiesce() {
  // A missing or damaged filesystem cannot drain queued settings. Let USB MSC
  // take ownership so the host can format or repair the volume.
  if (started && sdMounted() && nb4SettingsPending()) return false;
  paused.store(true);
  return !busy.load();
}
bool nb4StorageStarted() { return started; }
void nb4StorageResume() { paused.store(false); }
void nb4StorageProcess() {
  busy.store(true);
  if (paused.load()) { busy.store(false); return; }
  if (sdMounted() && !usbOwnsStorage()) nb4WritePendingSettings();
  const bool again = retry.exchange(false);
  for (auto& slot : slots) {
    auto state = slot.state.load(std::memory_order_acquire);
    if (state != Pending && !(again && state == Failed)) continue;
    if (!sdMounted() || usbOwnsStorage()) {
      status.store(Nb4HistoryStatus::Unavailable); slot.state.store(Failed); continue;
    }
    status.store(Nb4HistoryStatus::Saving);
    auto result = saveRecord(slot.record);
    status.store(result == FR_OK ? Nb4HistoryStatus::Saved : Nb4HistoryStatus::Failed);
    slot.state.store(result == FR_OK ? Empty : Failed, std::memory_order_release);
  }
  if (nb4HistoryPending() && status.load() == Nb4HistoryStatus::Saved) status.store(Nb4HistoryStatus::Failed);
  processQuery();
  nb4HealthBeat(NB4_TASK_STORAGE);
  if (logRequested.exchange(false) && sdMounted() && !usbOwnsStorage()) logsWrite();
  busy.store(false);
}
void nb4StorageRequestLog() { logRequested.store(true); }
void nb4StorageStart() {
  if (started) return;
  started = true;
  task_create(&storageTask, storageWorker, "nb4-storage", storageStack, 1024, MENUS_TASK_PRIO);
}
unsigned nb4StorageStackSize() { return started ? task_get_stack_size(&storageTask) : 0; }
unsigned nb4StorageStackUsage() { return started ? task_get_stack_usage(&storageTask) : 0; }
#endif
