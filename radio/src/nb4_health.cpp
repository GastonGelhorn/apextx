/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "nb4_health.h"

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)

#include "edgetx.h"
#include "hal/usb_driver.h"
#include "os/time.h"
#include "tasks.h"
#include "tasks/mixer_task.h"
#include "nb4_fault_screen.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
constexpr uint32_t MAGIC = 0x4e423448;
constexpr uint32_t RECORD_VERSION = 3;

#if defined(SIMU)
Nb4HealthRecord retained;
#else
// The linker reserves this address for both firmware and bootloader images.
__attribute__((section(".nb4_health"), used, aligned(4))) Nb4HealthRecord retained;
#endif

Nb4HealthRecord previous;
std::atomic<uint32_t> progress[NB4_TASK_COUNT];
std::atomic<uint32_t> lastBeat[NB4_TASK_COUNT];
std::atomic<bool> recovery{false};
std::atomic_flag recording = ATOMIC_FLAG_INIT;
uint32_t lastSupervision;
bool reportedUiStall;
bool reportedTimerStall;

uint32_t checksum(const Nb4HealthRecord& record)
{
  const auto* words = reinterpret_cast<const uint32_t*>(&record);
  uint32_t sum = 0x81e53b9a;
  for (unsigned i = 1; i < sizeof(record) / sizeof(uint32_t) - 1; ++i)
    sum = (sum << 5 | sum >> 27) ^ words[i];
  return sum;
}

bool valid(const Nb4HealthRecord& record)
{
  return record.magic == MAGIC && record.version == RECORD_VERSION &&
         record.checksum == checksum(record);
}

[[noreturn]] void allocationFailed()
{
  nb4HealthFault(NB4_FAULT_CPP_ALLOC, 0);
#if defined(FREE_RTOS) && !defined(SIMU)
  if (mixerTaskStarted() &&
      xTaskGetCurrentTaskHandle() == menusTaskId._rtos_handle) {
    // Say so on the panel before the interface stops answering for good. The
    // screen is painted here, on the task that is about to be suspended,
    // rather than left to the mixer: the time is worth nothing now.
    nb4FaultScreenShow(NB4_FAULT_CPP_ALLOC);
    for (;;) vTaskSuspend(nullptr);
  }
#endif
  nb4Fatal(NB4_FAULT_CPP_ALLOC, 0);
}
}  // namespace

void nb4HealthInit()
{
  previous = valid(retained) ? retained : Nb4HealthRecord{};
  recovery.store(previous.fault != NB4_FAULT_NONE);
  lastSupervision = 0;
  reportedUiStall = false;
  reportedTimerStall = false;
  for (unsigned i = 0; i < NB4_TASK_COUNT; ++i) {
    progress[i].store(0);
    lastBeat[i].store(time_get_ms());
  }
  std::set_new_handler(allocationFailed);
}

void nb4HealthBeat(Nb4Task task)
{
  if ((unsigned)task >= NB4_TASK_COUNT) return;
  progress[task].fetch_add(1, std::memory_order_relaxed);
  lastBeat[task].store(time_get_ms(), std::memory_order_relaxed);
}

bool nb4HealthPrevious(Nb4HealthRecord* out)
{
  *out = previous;
  return valid(previous);
}

bool nb4HealthRecovery() { return recovery.load(); }
void nb4HealthRequestRecovery() { recovery.store(true); }
void nb4HealthClearPrevious() { retained.magic = 0; previous = {}; }

bool nb4HealthSupervisor()
{
  const auto now = time_get_ms();
  if (usbStarted() && getSelectedUsbMode() == USB_MASS_STORAGE_MODE) {
    // Mass-storage callbacks run in the USB interrupt and can legitimately
    // pause the scheduler while NOR blocks are erased. Keep task timestamps
    // current so returning from the callback cannot latch a false stall.
    lastBeat[NB4_TASK_UI].store(now, std::memory_order_relaxed);
    lastBeat[NB4_TASK_TIMER].store(now, std::memory_order_relaxed);
    lastSupervision = now;
    return true;
  }
  // A bounded slice of any pending fault screen, every cycle. Painting the
  // whole panel here would delay a channel frame; a slice will not.
  nb4FaultScreenStep();

  if (now - lastSupervision < 100) return !reportedTimerStall;
  lastSupervision = now;

  if (!reportedUiStall &&
      progress[NB4_TASK_UI].load(std::memory_order_relaxed) &&
      now - lastBeat[NB4_TASK_UI].load(std::memory_order_relaxed) > 5000) {
    reportedUiStall = true;
    nb4HealthFault(NB4_FAULT_UI_STALL,
                   now - lastBeat[NB4_TASK_UI].load());
    // The interface cannot report this itself, so the mixer does it: the panel
    // is scanned straight out of SDRAM, and this task is still running.
    nb4FaultScreenRequest(NB4_FAULT_UI_STALL);
  }

  if (!reportedTimerStall &&
      progress[NB4_TASK_TIMER].load(std::memory_order_relaxed) &&
      now - lastBeat[NB4_TASK_TIMER].load(std::memory_order_relaxed) > 500) {
    reportedTimerStall = true;
    nb4HealthFault(NB4_FAULT_TIMER_STALL, NB4_TASK_TIMER);
  }
  return !reportedTimerStall;
}

void nb4HealthAssertUi()
{
#if defined(FREE_RTOS) && !defined(SIMU)
  if (menusTaskId._rtos_handle &&
      xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
      xTaskGetCurrentTaskHandle() != menusTaskId._rtos_handle)
    nb4Fatal(NB4_FAULT_UI_ASSERT, 0x54485244);
#endif
}

void nb4HealthFault(uint32_t fault, uint32_t detail)
{
  if (recording.test_and_set(std::memory_order_acquire)) return;
  if (fault == NB4_FAULT_UI_STALL && valid(retained) &&
      retained.fault != NB4_FAULT_NONE) {
    recording.clear(std::memory_order_release);
    return;
  }

  retained = {};
  retained.version = RECORD_VERSION;
  retained.fault = fault;
  retained.detail = detail;
  for (unsigned i = 0; i < NB4_TASK_COUNT; ++i)
    retained.progress[i] = progress[i].load(std::memory_order_relaxed);
  retained.checksum = checksum(retained);
  std::atomic_thread_fence(std::memory_order_release);
  retained.magic = MAGIC;
  recording.clear(std::memory_order_release);
}

void nb4UiAssert(const char* file, unsigned line)
{
  uint32_t detail = line;
  for (; *file; ++file) detail = detail * 33 ^ (uint8_t)*file;
  nb4HealthFault(NB4_FAULT_UI_ASSERT, detail);
#if defined(FREE_RTOS) && !defined(SIMU)
  if (mixerTaskStarted() &&
      xTaskGetCurrentTaskHandle() == menusTaskId._rtos_handle) {
    nb4FaultScreenShow(NB4_FAULT_UI_ASSERT);
    for (;;) vTaskSuspend(nullptr);
  }
#endif
  nb4Fatal(NB4_FAULT_UI_ASSERT, detail);
}

void nb4Fatal(uint32_t fault, uint32_t detail)
{
  nb4HealthFault(fault, detail);
#if defined(SIMU)
  std::abort();
#else
  __disable_irq();
  NVIC_SystemReset();
  for (;;) {}
#endif
}

#if defined(FREE_RTOS) && !defined(SIMU)
extern "C" void vApplicationStackOverflowHook(TaskHandle_t task, char*)
{
  nb4Fatal(NB4_FAULT_STACK,
           task == menusTaskId._rtos_handle ? NB4_TASK_UI :
           task == mixerTaskId._rtos_handle ? NB4_TASK_MIXER : NB4_TASK_TIMER);
}
#endif

#endif
