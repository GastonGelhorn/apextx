/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "edgetx.h"
#include "nb4_racing.h"
#include "hal/adc_driver.h"

#if defined(RADIO_NB4_FAMILY)

#include "nb4_pit.h"
#include "nb4_history.h"
#include <atomic>
#include "nb4_controls.h"
#include "tasks/mixer_task.h"
#if defined(HAPTIC)
#include "haptic.h"
#endif

static Nb4RacingState _nb4RacingState;
static Nb4LapState _nb4LapState;
// Mixer-owned lap state is published through aligned atomics. UI/Lua never
// wait for the mixer, and the 99-element copy only happens when laps change.
static std::atomic<uint8_t> _nb4LapPending{0};
static std::atomic<uint8_t> lapCount{0}, bestLapIndex{0};
static std::atomic<uint32_t> lapElapsed{0}, lastLap{0}, bestLap{0}, lapTotal{0}, lapAverage{0};
static std::atomic<int32_t> lapDelta{0};
static std::atomic<uint32_t> lapTimes[NB4_MAX_LAPS];
static void publishLaps() {
  const auto& st = _nb4LapState;
  uint64_t total = 0; uint32_t previousBest = 0; uint8_t bestIndex = 0;
  for (uint8_t i = 0; i < st.laps; ++i) {
    lapTimes[i].store(st.times[i], std::memory_order_relaxed);
    total += st.times[i];
    if (!bestIndex && st.times[i] == st.best) bestIndex = i + 1;
    if (i + 1 < st.laps && (!previousBest || st.times[i] < previousBest)) previousBest = st.times[i];
  }
  lapTotal.store(min<uint64_t>(UINT32_MAX, total));
  lapAverage.store(st.laps ? min<uint64_t>(UINT32_MAX, total / st.laps) : 0);
  lapDelta.store(st.laps < 2 ? 0 : limit<int64_t>(INT32_MIN, int64_t(st.last) - previousBest, INT32_MAX));
  bestLapIndex.store(bestIndex); lastLap.store(st.last); bestLap.store(st.best);
  lapElapsed.store(st.elapsed); lapCount.store(st.laps);
}
static std::atomic<uint8_t> _nb4LapTouch{0};
static std::atomic<uint8_t> raceCommand{0};
static std::atomic<Nb4RacePhase> racePhase{Nb4RacePhase::Ready};
static std::atomic<uint32_t> raceElapsed{0};
static bool manualTimer = false;
static std::atomic<bool> racePaused{false};
static std::atomic<bool> processingCommand{false};
static uint8_t nativeAdvance = 0;
static int historySlot = -1;
static Nb4RaceRecord result;
static bool queueCommand(uint8_t command) {
  uint8_t empty = 0;
  return raceCommand.compare_exchange_strong(empty, command);
}
void nb4RaceNativeTimerReset() { if (!processingCommand.load()) queueCommand(5); }
Nb4RacePhase nb4RacePhase() { return racePhase.load(); }
uint32_t nb4RaceElapsed() { return raceElapsed.load(); }
bool nb4RaceStart() {
  if (!nb4HistoryCanReserve() || nb4RacePhase() == Nb4RacePhase::Running) return false;
  if (queueCommand(1)) return true;
  // Starting includes a native reset, so a reset queued by a physical key can
  // safely coalesce with Start before the mixer consumes it.
  uint8_t reset = 5;
  return raceCommand.compare_exchange_strong(reset, 1);
}
bool nb4RaceFinish() { return queueCommand(2); }
bool nb4RaceTogglePause() { return queueCommand(6); }
bool nb4RaceIsPaused() { return racePaused.load(); }
bool nb4RaceTimerOverride() { return manualTimer; }
bool nb4RaceTimerHeld() { return nb4RaceIsPaused() || nb4RacePhase() == Nb4RacePhase::Finished || (nb4RacePhase() == Nb4RacePhase::Ready && !nb4HistoryCanReserve()); }
void nb4RaceTimerAdvance(uint8_t tick10ms) { nativeAdvance = tick10ms; }
static void beginRace() {
  racePaused.store(false);
  historySlot = nb4HistoryReserve();
  memset(&result, 0, sizeof(result));
  memcpy(result.model, g_model.header.name, min(sizeof(result.model) - 1, sizeof(g_model.header.name)));
#if defined(RTCLOCK)
  result.date = rtcIsValid() ? g_rtcTime : 0;
#endif
  memset(&_nb4LapState, 0, sizeof(_nb4LapState));
  publishLaps();
  raceElapsed.store(0);
  racePhase.store(Nb4RacePhase::Running);
}
static void finishRace() {
  if (nb4RacePhase() != Nb4RacePhase::Running) return;
  result.duration = nb4RaceElapsed();
  result.laps = _nb4LapState.laps;
  result.best = _nb4LapState.best;
  result.last = _nb4LapState.last;
  result.average = nb4RacingRaceAverage();
  memcpy(result.times, _nb4LapState.times, sizeof(result.times));
  // Reservation precedes the session. Publishing copies only bounded RAM.
  nb4HistoryPublish(historySlot, result);
  historySlot = -1;
  timersStates[0].state = TMR_STOPPED;
  racePaused.store(false);
  racePhase.store(Nb4RacePhase::Finished);
}
static void undoLap();
void nb4RaceProcessCommands() {
  processingCommand.store(true);
  switch (raceCommand.exchange(0)) {
    case 6:
      if (nb4RacePhase() == Nb4RacePhase::Running) {
        racePaused.store(!racePaused.load());
        manualTimer = true;
        break;
      }
      // Starting from Ready/Finished uses exactly the existing race start.
      [[fallthrough]];
    case 1:
      if (nb4RacePhase() != Nb4RacePhase::Running && nb4HistoryCanReserve()) {
        timerReset(0);
        timersStates[0].state = TMR_RUNNING;
        manualTimer = true;
        if (nb4PitEnabled()) { timerReset(NB4_PIT_TIMER); timersStates[NB4_PIT_TIMER].state = TMR_RUNNING; }
        beginRace();
      }
      break;
    case 2: finishRace(); break;
    case 3:
      if (nb4RacePhase() != Nb4RacePhase::Running) {
        raceElapsed.store(0);
        memset(&_nb4LapState, 0, sizeof(_nb4LapState));
        publishLaps();
        manualTimer = false;
        timerReset(0);
        racePhase.store(Nb4RacePhase::Ready);
      }
      break;
    case 4: if (nb4RacePhase() == Nb4RacePhase::Running) { undoLap(); publishLaps(); } break;
    case 5:
      nb4HistoryRelease(historySlot); historySlot = -1;
      raceElapsed.store(0); memset(&_nb4LapState, 0, sizeof(_nb4LapState)); publishLaps();
      racePaused.store(false);
      manualTimer = false; nativeAdvance = 0;
      // timerReset/timerSet already applied the native value. A deferred reset
      // here would erase a value assigned by Lua or another native action.
      racePhase.store(Nb4RacePhase::Ready);
      break;
  }
  processingCommand.store(false);
}
static uint8_t _nb4SteeringChannel = 0xff;
static uint8_t _nb4ThrottleChannel = 0xff;

static void resetOutputState()
{
  _nb4RacingState.absPhase = 0;
  _nb4RacingState.absReleasing = 0;
  _nb4RacingState.steerPos = 0;
  _nb4RacingState.steerValid = 0;
}

static inline int32_t scalePercent(int32_t value, uint8_t percent)
{
  return (value * (int32_t)percent) / 100;
}

int16_t nb4RacingThrottle(const Nb4RacingData& cfg, int16_t value, bool cut,
                          bool idleUp, Nb4RacingState& st, uint8_t tick10ms)
{

  if (cut) {
    st.absPhase = 0;
    st.absReleasing = 0;
    return (int16_t)((int32_t)cfg.engineCutPos * RESX / 100);
  }

  if (idleUp && cfg.idleUp > 0) {
    int32_t floorValue = scalePercent(RESX, cfg.idleUp);
    if (value >= 0 && value < floorValue) value = (int16_t)floorValue;
  }

  if (value >= 0) {

    if (value == 0 && cfg.dragBrake > 0) {

      value = (int16_t)-scalePercent(RESX, cfg.dragBrake);
    } else {
      st.absPhase = 0;
      st.absReleasing = 0;
      return value;
    }
  }

  // 3. Brake limit.
  if (cfg.brakeMax < 100) {
    value = (int16_t)scalePercent(value, cfg.brakeMax);
  }

  if (cfg.absEnable && cfg.absRate > 0) {
    int32_t threshold = scalePercent(RESX, cfg.absPoint);
    if (-(int32_t)value >= threshold && threshold > 0) {

      uint16_t halfCycle = (uint16_t)(50 / cfg.absRate);
      if (halfCycle == 0) halfCycle = 1;

      st.absPhase += tick10ms;
      if (st.absPhase >= halfCycle) {
        st.absPhase = 0;
        st.absReleasing = st.absReleasing ? 0 : 1;
      }

      if (st.absReleasing && cfg.absRelease > 0) {
        value = (int16_t)scalePercent(value, 100 - cfg.absRelease);
      }
    } else {
      st.absPhase = 0;
      st.absReleasing = 0;
    }
  }

  return value;
}

int16_t nb4RacingSteering(const Nb4RacingData& cfg, int16_t value,
                          Nb4RacingState& st, uint8_t tick10ms)
{
  if (!st.steerValid) {
    st.steerValid = 1;
    st.steerPos = value;
    return value;
  }

  bool returning = (abs(value) < abs(st.steerPos));
  uint8_t speed = returning ? cfg.steerSpeedReturn : cfg.steerSpeedTurn;

  if (speed == 0 || tick10ms == 0) {
    if (speed == 0) st.steerPos = value;
    return st.steerPos;
  }

  int32_t step = (2 * (int32_t)RESX) / ((int32_t)speed * 10);
  if (step < 1) step = 1;
  step *= tick10ms;

  int32_t delta = (int32_t)value - (int32_t)st.steerPos;
  if (delta > step) delta = step;
  if (delta < -step) delta = -step;

  st.steerPos = (int16_t)(st.steerPos + delta);
  return st.steerPos;
}

bool nb4RacingLapTick(Nb4LapState& st, bool pressed, uint8_t tick10ms,
                      uint8_t lapLimit)
{
  bool closed = false;
  bool finished = st.laps >= NB4_MAX_LAPS ||
                  (lapLimit > 0 && st.laps >= lapLimit);

  if (!finished) {
    if (UINT32_MAX - st.elapsed < tick10ms)
      st.elapsed = UINT32_MAX;
    else
      st.elapsed += tick10ms;
  }

  if (!finished && pressed && !st.pressed) {
    st.last = st.elapsed;
    if (st.best == 0 || st.last < st.best) st.best = st.last;
    if (st.laps < NB4_MAX_LAPS) st.times[st.laps] = st.last;
    st.elapsed = 0;
    if (st.laps < NB4_MAX_LAPS) st.laps++;
    closed = true;
  }
  st.pressed = pressed ? 1 : 0;
  return closed;
}

uint8_t nb4RacingLaps() { return lapCount.load(); }
uint32_t nb4RacingLastLap() { return lastLap.load(); }
uint32_t nb4RacingBestLap() { return bestLap.load(); }
uint32_t nb4RacingCurrentLap() { return lapElapsed.load(); }

void nb4RacingFormatTime(char* buf, uint32_t cs)
{
  if (cs == 0) {
    buf[0] = '-';
    buf[1] = '\0';
    return;
  }
  char* p = buf;
  uint32_t secs = cs / 100;
  if (secs >= 60) {
    p = strAppendUnsigned(p, secs / 60);
    *p++ = ':';
    p = strAppendUnsigned(p, secs % 60, 2);
  } else {
    p = strAppendUnsigned(p, secs);
  }
  *p++ = '.';
  p = strAppendUnsigned(p, cs % 100, 2);
  *p = '\0';
}

uint32_t nb4RacingLapTime(uint8_t idx)
{
  return idx < lapCount.load() && idx < NB4_MAX_LAPS ? lapTimes[idx].load(std::memory_order_relaxed) : 0;
}
uint32_t nb4RacingRaceTotal() { return lapTotal.load(); }
uint32_t nb4RacingRaceAverage() { return lapAverage.load(); }

void nb4RacingLapsReset()
{
  queueCommand(5);
}

void nb4RacingMarkLap() { _nb4LapTouch.store(1); }

bool nb4RacingUndoLap() { return nb4RacePhase() == Nb4RacePhase::Running && nb4RacingLaps() && queueCommand(4); }

static void undoLap()
{

  Nb4LapState& st = _nb4LapState;
  bool undone = st.laps > 0;
  if (undone) {
    st.laps--;
    uint32_t lap = st.times[st.laps];
    st.times[st.laps] = 0;
    if (UINT32_MAX - st.elapsed < lap)
      st.elapsed = UINT32_MAX;
    else
      st.elapsed += lap;
    st.last = st.laps ? st.times[st.laps - 1] : 0;
    st.best = 0;
    for (uint8_t i = 0; i < st.laps; i++)
      if (st.best == 0 || st.times[i] < st.best) st.best = st.times[i];
  }

}

int32_t nb4RacingLapDelta() { return lapDelta.load(); }
uint8_t nb4RacingBestLapIndex() { return bestLapIndex.load(); }

void nb4RacingReset()
{
  nb4ControlsReload();
  racePaused.store(false);
  resetOutputState();
  _nb4SteeringChannel = 0xff;
  _nb4ThrottleChannel = 0xff;
  nb4HistoryRelease(historySlot);
  historySlot = -1;
  memset(&_nb4LapState, 0, sizeof(_nb4LapState));
  publishLaps();
  racePhase.store(Nb4RacePhase::Ready);
  raceElapsed.store(0);
  raceCommand.store(0);
  _nb4LapTouch.store(0);
  _nb4LapPending = 0;
  nativeAdvance = 0;
  manualTimer = false;
}

void nb4RacingDefaults(Nb4RacingData& cfg)
{
  memset(&cfg, 0, sizeof(cfg));
  cfg.version = NB4_RACING_VERSION;
  cfg.brakeMax = 100;
  cfg.absPoint = 50;         // ABS engages at half brake travel
  cfg.absRate = 10;          // Ten pulses per second
  cfg.absRelease = 40;
  cfg.engineCutPos = -100;
  cfg.steeringChannel = 0;   // CH1
  cfg.throttleChannel = 1;   // CH2
  cfg.vehicleType = NB4_VEHICLE_UNSET;
  cfg.homeTimer = NB4_HOME_TIMER_AUTO;
}

static bool repairSwappedDefaultInputs()
{
  ExpoData& steering = g_model.expoData[0];
  ExpoData& throttle = g_model.expoData[1];
  const auto defaultLine = [](const ExpoData& line, uint8_t channel,
                              int16_t source) {
    return line.chn == channel && line.srcRaw == source && line.mode == 3 &&
           line.weight == 100 && line.offset == 0 && line.swtch == 0 &&
           line.curve.type == CURVE_REF_EXPO && line.curve.value == 0;
  };

  // V2 could create the otherwise untouched default pair as TH/ST when an old
  // general setting selected that channel order.  Only repair that exact pair;
  // deliberately edited inputs and mixes remain entirely under EdgeTX control.
  if (!defaultLine(steering, 0, MIXSRC_FIRST_STICK + ADC_MAIN_TH) ||
      !defaultLine(throttle, 1, MIXSRC_FIRST_STICK + ADC_MAIN_ST))
    return false;

  steering.srcRaw = MIXSRC_FIRST_STICK + ADC_MAIN_ST;
  throttle.srcRaw = MIXSRC_FIRST_STICK + ADC_MAIN_TH;
  strncpy(g_model.inputNames[0], getMainControlLabel(ADC_MAIN_ST),
          LEN_INPUT_NAME);
  strncpy(g_model.inputNames[1], getMainControlLabel(ADC_MAIN_TH),
          LEN_INPUT_NAME);
  return true;
}

bool nb4RacingMigrate(Nb4RacingData& cfg)
{
  if (cfg.version == 0) {
    nb4RacingDefaults(cfg);
    return true;
  }

  if (cfg.version == 1) {
    cfg.steeringChannel = 0;
    cfg.throttleChannel = 1;
    cfg.vehicleType = NB4_VEHICLE_CUSTOM;
    cfg.version = NB4_RACING_VERSION;
    return true;
  }

  if (cfg.version == 2) {
    repairSwappedDefaultInputs();
    cfg.version = NB4_RACING_VERSION;
    return true;
  }

  if (cfg.version != NB4_RACING_VERSION) {
    nb4RacingDefaults(cfg);
    return true;
  }

  bool changed = false;
  if (cfg.steeringChannel >= MAX_OUTPUT_CHANNELS) {
    cfg.steeringChannel = 0;
    changed = true;
  }
  if (cfg.throttleChannel >= MAX_OUTPUT_CHANNELS ||
      cfg.throttleChannel == cfg.steeringChannel) {
    cfg.throttleChannel = cfg.steeringChannel == 1 ? 0 : 1;
    changed = true;
  }
  if (cfg.lapCount > NB4_MAX_LAPS) {
    cfg.lapCount = NB4_MAX_LAPS;
    changed = true;
  }
  return changed;
}

void nb4RacingPresetElectric(Nb4RacingData& cfg)
{
  nb4RacingDefaults(cfg);
  cfg.brakeMax = 100;
  cfg.dragBrake = 0;
  cfg.absEnable = 0;
  cfg.steerSpeedTurn = 0;
  cfg.steerSpeedReturn = 0;
  cfg.idleUp = 0;
  cfg.idleUpSw = 0;
  cfg.engineCutSw = 0;
  cfg.vehicleType = NB4_VEHICLE_ELECTRIC;
}

void nb4RacingPresetNitro(Nb4RacingData& cfg)
{
  nb4RacingDefaults(cfg);
  cfg.brakeMax = 80;
  cfg.dragBrake = 8;        // Minimum engine braking after release
  cfg.absEnable = 0;
  cfg.idleUp = 15;
  cfg.engineCutPos = -100;

  cfg.idleUpSw = 0;
  cfg.engineCutSw = 0;
  cfg.vehicleType = NB4_VEHICLE_NITRO;
}

void nb4RacingSetupRaceTimer(bool nitro)
{
  TimerData& t = g_model.timers[0];

  t.mode = TMRMODE_THR_START;  // Starts on throttle and keeps counting
  t.start = 0;
  t.swtch = 0;
  t.minuteBeep = 1;
  t.countdownBeep = 0;
  t.persistent = nitro ? 1 : 0;
  t.showElapsed = 0;

  storageDirty(EE_MODEL);
}

int32_t nb4RacingApplyChannel(uint8_t channel, int32_t valueQ8,
                              uint8_t tick10ms)
{
  const Nb4RacingData& cfg = g_model.nb4Racing;

  if (cfg.version != NB4_RACING_VERSION) return valueQ8;

  if (_nb4SteeringChannel != cfg.steeringChannel ||
      _nb4ThrottleChannel != cfg.throttleChannel) {
    resetOutputState();
    _nb4SteeringChannel = cfg.steeringChannel;
    _nb4ThrottleChannel = cfg.throttleChannel;
  }

  bool steeringActive = channel == cfg.steeringChannel &&
                        (cfg.steerSpeedTurn > 0 || cfg.steerSpeedReturn > 0);
  bool throttleActive = channel == cfg.throttleChannel &&
                        (cfg.brakeMax < 100 || cfg.dragBrake > 0 ||
                         cfg.absEnable ||
                         (cfg.idleUp > 0 && cfg.idleUpSw != 0) ||
                         cfg.engineCutSw != 0);
  if (!steeringActive && !throttleActive) return valueQ8;

  int16_t value = (int16_t)limit<int32_t>(INT16_MIN, valueQ8 / 256,
                                          INT16_MAX);

  if (steeringActive)
    value = nb4RacingSteering(cfg, value, _nb4RacingState, tick10ms);
  if (throttleActive) {
    bool cut = cfg.engineCutSw != 0 && getSwitch(cfg.engineCutSw);
    bool idleUp = cfg.idleUpSw != 0 && getSwitch(cfg.idleUpSw);
    value = nb4RacingThrottle(cfg, value, cut, idleUp, _nb4RacingState, tick10ms);
  }
  return (int32_t)value * 256;
}

void nb4RacingTick(uint8_t tick10ms)
{
  const Nb4RacingData& cfg = g_model.nb4Racing;
  if (cfg.version != NB4_RACING_VERSION) return;

  const uint8_t advance = nativeAdvance;
  nativeAdvance = 0;
  bool touch = _nb4LapTouch.exchange(0) != 0;
  bool pressed = (cfg.lapSw != 0 && getSwitch(cfg.lapSw)) || touch;
  if (nb4RacePhase() == Nb4RacePhase::Ready && advance) beginRace();
  if (nb4RacePhase() != Nb4RacePhase::Running || nb4RaceIsPaused()) {
    _nb4LapState.pressed = pressed;
    return;
  }
  raceElapsed.store(min<uint64_t>(UINT32_MAX, uint64_t(nb4RaceElapsed()) + advance));
  if (nb4RacingLapTick(_nb4LapState, pressed, advance, cfg.lapCount)) {
    publishLaps();
    _nb4LapPending.store(1);
    if (_nb4LapState.laps >= NB4_MAX_LAPS ||
        (cfg.lapCount && _nb4LapState.laps >= cfg.lapCount)) finishRace();
  }
  lapElapsed.store(_nb4LapState.elapsed);
}

void nb4RacingPerMain()
{
  nb4PitPerMain();

  if (!_nb4LapPending.exchange(0)) return;

  AUDIO_KEY_PRESS();
#if defined(HAPTIC)
  haptic.play(10, 0, 0);
#endif
  if (g_model.nb4Racing.lapAnnounce) {
    playNumber((int32_t)min<uint32_t>(nb4RacingLastLap() / 10, INT32_MAX),
               0, PREC1, 0);

    int32_t delta = nb4RacingLapDelta();
    if (nb4RacingLaps() >= 2 && delta > -6000 && delta < 6000 && delta != 0)
      playNumber(delta / 10, 0, PREC1, 0);
  }
}

#endif  // RADIO_NB4_FAMILY
