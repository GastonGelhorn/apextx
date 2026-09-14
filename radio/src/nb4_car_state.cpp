/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_car_state.h"
#include "nb4_racing.h"
#include "nb4_controls.h"
#include "hal/adc_driver.h"
#include "model_init.h"
#include "storage/storage.h"
#include "nb4_axis.h"
#if defined(RADIO_NB4) && defined(AFHDS3)
#include "pulses/afhds3.h"
#endif

#if defined(SIMU)
uint8_t nb4SimuChargeSource = 0;
#endif

uint8_t nb4BatteryPercent(uint16_t cellMv)
{

  static const struct { uint16_t mv; uint8_t percent; } knees[] = {
    {3300, 0}, {3500, 15}, {3700, 45}, {3850, 75}, {4150, 100},
  };
  const unsigned count = sizeof(knees) / sizeof(knees[0]);
  if (cellMv <= knees[0].mv) return 0;
  if (cellMv >= knees[count - 1].mv) return 100;
  for (unsigned k = 1; k < count; ++k) {
    if (cellMv >= knees[k].mv) continue;
    const uint16_t lowMv = knees[k - 1].mv;
    const uint8_t lowPc = knees[k - 1].percent;
    const uint32_t spanMv = (uint32_t)knees[k].mv - lowMv;
    const uint32_t spanPc = (uint32_t)knees[k].percent - lowPc;
    return (uint8_t)(lowPc +
      ((uint32_t)(cellMv - lowMv) * spanPc + spanMv / 2) / spanMv);
  }
  return 100;
}

const char* nb4Text(const char* es, const char* en)
{
  return g_eeGeneral.uiLanguage[0] == 'e' && g_eeGeneral.uiLanguage[1] == 's' ? es : en;
}

void nb4VisualDefaults()
{
  g_eeGeneral.nb4UiVersion = NB4_UI_VERSION;
  g_eeGeneral.nb4Home = NB4_HOME_INSTRUMENTS;
  g_eeGeneral.nb4Orientation = 0;
  strAppend(g_eeGeneral.selectedTheme, "ApexTX Dark", SELECTED_THEME_NAME_LEN);

  g_eeGeneral.modelGVDisabled = 1;
}

void nb4FactoryReset(Nb4Reset what)
{
  if (what == Nb4Reset::Radio || what == Nb4Reset::Both) {

    CalibData calibration[MAX_CALIB_ANALOG_INPUTS];
    memcpy(calibration, g_eeGeneral.calib, sizeof(calibration));
    char language[sizeof(g_eeGeneral.uiLanguage)];
    memcpy(language, g_eeGeneral.uiLanguage, sizeof(language));

    generalDefault();

    memcpy(g_eeGeneral.calib, calibration, sizeof(calibration));
    memcpy(g_eeGeneral.uiLanguage, language, sizeof(language));
    storageDirty(EE_GENERAL);
  }

  if (what == Nb4Reset::Model || what == Nb4Reset::Both) {

    preModelLoad();

    char name[LEN_MODEL_NAME + 1] = {};
    memcpy(name, g_model.header.name, LEN_MODEL_NAME);

    setModelDefaults(0);

    memcpy(g_model.header.name, name, LEN_MODEL_NAME);
    storageDirty(EE_MODEL);

    postModelLoad(false);
  }

  storageCheck(true);
}

static Nb4Reading reading(int32_t value, Nb4Unit unit, Nb4Validity validity = Nb4Validity::Valid)
{
  return {value, validity, unit};
}

static Nb4Reading trimReading(unsigned index)
{
  const auto trim = getRawTrimValue(0, index);
  const bool enabled = trim.mode != TRIM_MODE_NONE && trim.mode != TRIM_MODE_3POS;
  return reading(enabled ? getTrimValue(0, index) : 0, Nb4Unit::Trim,
                 enabled ? Nb4Validity::Valid : Nb4Validity::Absent);
}

const Nb4CarState& nb4ReadCarState()
{
  static Nb4CarState state;

  state = Nb4CarState{};
  state.timestampMs = timersGetMsTick();
  memcpy(state.model, g_model.header.name, min(sizeof(state.model) - 1, size_t(LEN_MODEL_NAME)));
  state.steeringChannel = g_model.nb4Racing.steeringChannel;
  state.throttleChannel = g_model.nb4Racing.throttleChannel;

  state.steeringInput = reading(divRoundClosest(
      (int32_t)nb4AxisVisual(false, calibratedAnalogs[ADC_MAIN_ST]) * 100, RESX), Nb4Unit::Percent);
  state.throttleInput = reading(divRoundClosest(calibratedAnalogs[ADC_MAIN_TH] * 100, RESX), Nb4Unit::Percent);
  for (unsigned i = 0; i < MAX_MIXERS; ++i) {
    const auto& mix = g_model.mixData[i];
    if (mix.srcRaw && mix.destCh < 8) state.channels[mix.destCh].assigned = true;
  }
  for (auto& function : g_model.customFn) {
    if (CFN_SWITCH(&function) && CFN_FUNC(&function) == FUNC_OVERRIDE_CHANNEL &&
        CFN_ACTIVE(&function) && CFN_CH_INDEX(&function) < 8)
      state.channels[CFN_CH_INDEX(&function)].assigned = true;
  }
  for (unsigned i = 0; i < 8; ++i) {
    auto& ch = state.channels[i];
    if (ch.assigned) {
      ch.output = reading(divRoundClosest(channelOutputs[i] * 100, RESX), Nb4Unit::Percent);
      ch.command = reading(divRoundClosest(ex_chans[i] * 100, RESX), Nb4Unit::Percent);
    }
  }
  state.steeringTrim = trimReading(ADC_MAIN_ST);
  state.throttleTrim = trimReading(ADC_MAIN_TH);
  if (g_model.timers[0].mode != TMRMODE_OFF)
    state.timer = reading(timersStates[0].val, Nb4Unit::Seconds);
  state.racePhase = nb4RacePhase();
  state.raceElapsed = reading(nb4RaceElapsed(), Nb4Unit::Centiseconds,
    state.racePhase != Nb4RacePhase::Ready || g_model.timers[0].mode != TMRMODE_OFF || nb4ControlsHasAction(NB4_CONTROL_RUN_PAUSE) ? Nb4Validity::Valid : Nb4Validity::Absent);
  state.lapsConfigured = g_model.nb4Racing.lapSw != 0 || nb4ControlsHasAction(NB4_CONTROL_LAP) || state.racePhase != Nb4RacePhase::Ready;
  state.laps = nb4RacingLaps();
  if (state.lapsConfigured) {
    state.currentLap = reading(nb4RacingCurrentLap(), Nb4Unit::Centiseconds);
    if (state.laps) {
      state.lastLap = reading(nb4RacingLastLap(), Nb4Unit::Centiseconds);
      state.bestLap = reading(nb4RacingBestLap(), Nb4Unit::Centiseconds);
    }
  }

  const uint8_t preference = g_model.nb4Racing.homeTimer;
  state.homeShowsRace = preference == NB4_HOME_TIMER_AUTO &&
                        (g_model.nb4Racing.lapSw != 0 || state.laps != 0);
  if (!state.homeShowsRace && preference != NB4_HOME_TIMER_HIDDEN) {
    int index = -1;
    if (preference == NB4_HOME_TIMER_1) index = 0;
    else if (preference == NB4_HOME_TIMER_2) index = 1;
    else if (g_model.timers[0].mode != TMRMODE_OFF) index = 0;
    else if (g_model.timers[1].mode != TMRMODE_OFF) index = 1;
    if (index >= 0) {
      const auto& cfg = g_model.timers[index];
      const auto& runtime = timersStates[index];
      int32_t shown = runtime.val;
      if (cfg.start && cfg.showElapsed && int32_t(cfg.start) != runtime.val)
        shown = int32_t(cfg.start) - runtime.val;
      state.homeTimerIndex = index;
      state.homeTimerState = runtime.state;
      state.homeTimerStart = cfg.start;
      state.homeTimerCountdown = cfg.start && !cfg.showElapsed;
      state.homeTimerVisible = preference != NB4_HOME_TIMER_AUTO || cfg.mode != TMRMODE_OFF;
      state.homeTimer = reading(shown, Nb4Unit::Seconds,
        cfg.mode == TMRMODE_OFF ? Nb4Validity::Absent : shown < 0 ? Nb4Validity::Alarm : Nb4Validity::Valid);
    }
  }
  if (state.homeShowsRace) state.homeTimerVisible = true;

  if (g_vbat100mV > 0)

#if defined(RADIO_NB4) && !defined(SIMU)
    state.chargeSource = nb4ChargeSource();
#elif defined(SIMU)
    state.chargeSource = nb4SimuChargeSource;
#endif
    state.transmitter = reading(g_vbat100mV * 100, Nb4Unit::Millivolts,
      g_vbat100mV < g_eeGeneral.vBatWarn ? Nb4Validity::Alarm : Nb4Validity::Valid);
#if defined(RADIO_NB4) && defined(AFHDS3)
  if (isModuleAFHDS3(INTERNAL_MODULE)) {
    const auto rx = afhds3::getReceiverTelemetry(INTERNAL_MODULE);
    const auto now = get_tmr10ms();
    const bool receiverLive = rx.qualityAvailable && TELEMETRY_STREAMING() &&
                              (tmr10ms_t)(now - rx.qualityTime) < TELEMETRY_TIMEOUT10ms;
    if (receiverLive)
      state.link = reading(rx.quality, Nb4Unit::Percent,
        !g_model.disableTelemetryWarning && rx.quality < g_model.rfAlarms.warning ? Nb4Validity::Alarm : Nb4Validity::Valid);
    if (receiverLive && rx.voltageAvailable)
      state.receiver = reading(rx.voltageMv, Nb4Unit::Millivolts,
        (tmr10ms_t)(now - rx.voltageTime) >= TELEMETRY_TIMEOUT10ms ? Nb4Validity::Stale : Nb4Validity::Valid);
    return state;
  }
#endif
  if (TELEMETRY_STREAMING()) state.link = reading(TELEMETRY_RSSI(), Nb4Unit::Percent,
    !g_model.disableTelemetryWarning && TELEMETRY_RSSI() < g_model.rfAlarms.warning ? Nb4Validity::Alarm : Nb4Validity::Valid);
  // AFHDS3's receiver supply sensor is ID 0, remapped to 0x1000 by iBus.
  // Never substitute another voltage sensor (e.g. the traction battery).
  for (unsigned i = 0; i < MAX_TELEMETRY_SENSORS; ++i) {
    const auto& sensor = g_model.telemetrySensors[i];
    auto& item = telemetryItems[i];
    if (sensor.type == TELEM_TYPE_CUSTOM && sensor.id == 0x1000 &&
        sensor.unit == UNIT_VOLTS && item.isAvailable()) {
      state.receiver = reading(convertTelemetryValue(item.value, sensor.unit, sensor.prec, UNIT_VOLTS, 3),
        Nb4Unit::Millivolts, item.isOld() || !TELEMETRY_STREAMING() ? Nb4Validity::Stale : Nb4Validity::Valid);
      break;
    }
  }
  return state;
}
Nb4SensorReading nb4ReadSensor(unsigned index)
{
  Nb4SensorReading r;
  if (index >= MAX_TELEMETRY_SENSORS) return r;
  const auto& sensor = g_model.telemetrySensors[index];
  if (!sensor.isAvailable()) return r;
  memcpy(r.name, sensor.label, min(sizeof(r.name) - 1, sizeof(sensor.label)));
  r.unit = sensor.unit; r.precision = sensor.prec;
  auto& item = telemetryItems[index];
  if (!item.isAvailable()) return r;
  r.value = item.value;
  r.validity = item.isOld() || !TELEMETRY_STREAMING() ? Nb4Validity::Stale : Nb4Validity::Valid;
  return r;
}
#endif
