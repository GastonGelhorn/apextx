/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#include "nb4_controls.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_racing.h"
#include "nb4_model_compat.h"
#include "hal/switch_driver.h"
#include <atomic>
SwitchHwPos boardSwitchGetPosition(uint8_t index);

namespace {
static_assert(sizeof(g_model.nb4Bindings) == NB4_CONTROL_COUNT, "Binding storage and inputs must agree");
static_assert(NB4_CONTROL_ACTION_COUNT < NB4_CONTROL_LONG, "Action IDs overlap the hold flag");
std::atomic<uint8_t> bindings[NB4_CONTROL_COUNT];
std::atomic<unsigned> revision{1};
std::atomic<uint16_t> pressedMask{0}, commands{0};
std::atomic<bool> learning{false};
std::atomic<uint8_t> learned{0xff};
std::atomic<unsigned> learnRevision{0};
std::atomic<uint16_t> heldForLearn{0}, physicalMask{0};
std::atomic<uint16_t> learnInitial{0};
std::atomic<uint8_t> switchLast[2], switchFrozen[2], trimsFrozen{0};
struct Input {
  uint8_t binding = 0xff;
  uint8_t samples = 0;
  uint8_t duration = 0;
  bool blocked = true;
  bool fired = false;
};
Input inputs[NB4_CONTROL_COUNT]; // used only by the timer task

uint8_t sanitize(uint8_t binding)
{
  uint8_t action = binding & ~NB4_CONTROL_LONG;
  if (action >= NB4_CONTROL_ACTION_COUNT) return NB4_CONTROL_DEFAULT;
  if (action == NB4_CONTROL_RESET) return action | NB4_CONTROL_LONG;
  return action | (nb4ControlCanHold(action) ? binding & NB4_CONTROL_LONG : 0);
}
}

bool nb4ControlIsNavigation(uint8_t action)
{
  return action >= NB4_CONTROL_PREVIOUS && action <= NB4_CONTROL_QUICK;
}
bool nb4ControlCanHold(uint8_t action)
{
  return action >= NB4_CONTROL_RUN_PAUSE && action <= NB4_CONTROL_UNDO;
}
uint8_t nb4ControlBinding(unsigned index)
{
  return index < NB4_CONTROL_COUNT ? sanitize(g_model.nb4Bindings[index]) : 0;
}
bool nb4ControlsHasAction(uint8_t action)
{
  for (unsigned i = 0; i < NB4_CONTROL_COUNT; ++i)
    if ((nb4ControlBinding(i) & ~NB4_CONTROL_LONG) == action) return true;
  return false;
}
void nb4ControlSetBinding(unsigned index, uint8_t binding)
{
  if (index >= NB4_CONTROL_COUNT || nb4ModelBlocked()) return;
  binding = sanitize(binding);
  g_model.nb4Bindings[index] = binding;
  bindings[index].store(binding);
  storageDirty(EE_MODEL);
}
void nb4ControlsReload()
{
  nb4ControlsEndLearn();
  for (unsigned i = 0; i < NB4_CONTROL_COUNT; ++i)
    bindings[i].store(nb4ModelBlocked() ? 0 : nb4ControlBinding(i));
  commands.store(0);
  revision.fetch_add(1);
}
uint16_t nb4ControlsPressed() { return pressedMask.load(); }

void nb4ControlsBeginLearn()
{
  for (unsigned i = 0; i < 2; ++i) switchFrozen[i].store(switchLast[i].load());
  trimsFrozen.store(physicalMask.load() >> 4);
  learnInitial.store(physicalMask.load());
  learned.store(0xff);
  commands.store(0);
  learnRevision.fetch_add(1);
  learning.store(true);
  // The key that opened this dialog must not release into it as Select/Back.
  for (unsigned i = 0; i < MAX_KEYS; ++i) killEvents(i);
  pushEvent(0);
}
void nb4ControlsEndLearn() { learning.store(false); }
bool nb4ControlsLearning() { return learning.load(); }
uint8_t nb4ControlsLearned() { return learned.load(); }
uint8_t nb4ControlsReadSwitches()
{
  return (boardSwitchGetPosition(0) == SWITCH_HW_DOWN ? 1 : 0) |
         (boardSwitchGetPosition(1) == SWITCH_HW_DOWN ? 2 : 0);
}
uint8_t nb4ControlsSwitchSource(uint8_t index, uint8_t physical)
{
  if (index >= 2) return physical;
  if (learning.load() || (heldForLearn.load() & (1u << (index + 2))))
    return switchFrozen[index].load();
  switchLast[index].store(physical);
  return physical;
}
uint32_t nb4ControlsTrimSource(uint32_t physical)
{
  const uint32_t frozen = learning.load() ? 0xff : heldForLearn.load() >> 4;
  return (physical & ~frozen) | (trimsFrozen.load() & frozen);
}

void nb4ControlsFilter(uint32_t& keys, uint32_t& trims, uint8_t switches)
{
  static unsigned lastRevision = 0;
  const unsigned currentRevision = revision.load();
  if (lastRevision != currentRevision) {
    for (auto& input : inputs) input = Input{};
    lastRevision = currentRevision;
  }
  const uint16_t physical = ((keys >> KEY_EXIT) & 1) |
      (((keys >> KEY_ENTER) & 1) << 1) | ((switches & 3) << 2) |
      ((trims & 0xff) << 4);
  physicalMask.store(physical);
  const bool capturing = learning.load();
  static unsigned lastLearnRevision = 0;
  static uint16_t learnArmed = 0;
  const unsigned captureRevision = learnRevision.load();
  if (lastLearnRevision != captureRevision) {
    learnArmed = ~learnInitial.load() & 0xfff;
    lastLearnRevision = captureRevision;
  }
  uint16_t suppressed = heldForLearn.load();
  if (capturing) suppressed |= physical;
  uint32_t mappedKeys = keys & ~((1u << KEY_EXIT) | (1u << KEY_ENTER));
  uint32_t mappedTrims = trims & ~0xffu;
  uint16_t stable = 0;
  for (unsigned i = 0; i < NB4_CONTROL_COUNT; ++i) {
    auto& input = inputs[i];
    const bool down = physical & (1u << i);
    const uint8_t binding = bindings[i].load();
    const uint8_t action = binding & ~NB4_CONTROL_LONG;
    if (input.binding != binding) {
      input = Input{};
      input.binding = binding;
    }
    input.samples = (input.samples << 1) | down;
    if (!input.samples) {
      input.blocked = false; input.fired = false; input.duration = 0;
      suppressed &= ~(1u << i);
      learnArmed |= 1u << i;
    }
    const bool settled = (input.samples & 0x0f) == 0x0f;
    if (settled) stable |= 1u << i;
    if (capturing || (suppressed & (1u << i))) continue;
    // The default path retains the native debounce/repeat and startup keys.
    if (action == NB4_CONTROL_DEFAULT) {
      if (down && i < 2) mappedKeys |= 1u << (i ? KEY_ENTER : KEY_EXIT);
      if (down && i >= 4) mappedTrims |= 1u << (i - 4);
      continue;
    }
    // Reassignment/model changes never act on a control already held down.
    if (input.blocked || action == NB4_CONTROL_OFF) continue;
    if (down) {
      static constexpr uint8_t navKeys[] = {
        KEY_UP, KEY_DOWN, KEY_ENTER, KEY_EXIT, KEY_MENU, KEY_BIND
      };
      if (nb4ControlIsNavigation(action))
        mappedKeys |= 1u << navKeys[action - NB4_CONTROL_PREVIOUS];
      else if (action >= NB4_CONTROL_ST_DOWN && action <= NB4_CONTROL_TH_UP)
        mappedTrims |= 1u << (action - NB4_CONTROL_ST_DOWN);
    }
    if (!settled || !nb4ControlCanHold(action)) continue;
    if (input.duration < 100) ++input.duration;
    if (!input.fired && (!(binding & NB4_CONTROL_LONG) || input.duration >= 60)) {
      input.fired = true;
      commands.fetch_or(1u << (action - NB4_CONTROL_RUN_PAUSE));
    }
  }
  pressedMask.store(stable);
  heldForLearn.store(suppressed);
  if (capturing && learned.load() >= 0xfe) {
    const uint16_t candidates = stable & learnArmed;
    if (candidates) {
      if (candidates & (candidates - 1)) {
        learned.store(0xfe);
        learnArmed &= ~candidates;
      } else {
        unsigned index = 0;
        while (!(candidates & (1u << index))) ++index;
        learned.store(index);
      }
    }
  }
  keys = mappedKeys;
  trims = mappedTrims;
}

void nb4ControlsProcessCommands()
{
  const uint16_t pending = commands.exchange(0);
  if (!pending || learning.load() || nb4ModelBlocked()) return;
  // Destructive commands have priority when two controls are pressed together.
  if (pending & (1u << (NB4_CONTROL_RESET - NB4_CONTROL_RUN_PAUSE))) {
    timerReset(0);
    return;
  }
  if (pending & (1u << (NB4_CONTROL_FINISH - NB4_CONTROL_RUN_PAUSE))) nb4RaceFinish();
  else if (pending & 1) nb4RaceTogglePause();
  if (pending & (1u << (NB4_CONTROL_LAP - NB4_CONTROL_RUN_PAUSE))) nb4RacingMarkLap();
  if (pending & (1u << (NB4_CONTROL_UNDO - NB4_CONTROL_RUN_PAUSE))) nb4RacingUndoLap();
}
#endif
