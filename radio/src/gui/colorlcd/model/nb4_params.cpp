/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_params.h"

#if defined(RADIO_NB4_FAMILY)

#include "edgetx.h"
#include "gvar_numberedit.h"
#include "libui/static.h"
#include "nb4_car_state.h"  // nb4Text
#include "numberedit.h"
#include "source_numberedit.h"
#include "curve_param.h"
#include "sourcechoice.h"
#include "strhelpers.h"
#include "switchchoice.h"
#include "choice.h"
#include "nb4_racing.h"
#include "toggleswitch.h"
#include "gui_common.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

namespace {

struct Entry { Nb4Param param; uint8_t channel; unsigned count; };
constexpr unsigned kMaxEntries = 64;
Entry _entries[kMaxEntries];
unsigned _entryCount = 0;

/* Objects created here, used to identify fields managed by this adapter. */
constexpr unsigned kMaxObjects = 128;
const void* _objects[kMaxObjects];
unsigned _objectCount = 0;

void noteObject(Window* w)
{
  if (w && _objectCount < kMaxObjects) _objects[_objectCount++] = w->getLvObj();
}

void record(Nb4Param p, uint8_t channel)
{
  for (unsigned i = 0; i < _entryCount; i += 1)
    if (_entries[i].param == p && _entries[i].channel == channel) {
      _entries[i].count += 1;
      return;
    }
  if (_entryCount < kMaxEntries)
    _entries[_entryCount++] = {p, channel, 1};
}

uint8_t channelOf(Nb4Param p, const Nb4ParamCtx& ctx)
{
  switch (p) {
    case Nb4Param::ChannelTravelMin:
    case Nb4Param::ChannelTravelMax:
    case Nb4Param::ChannelSubtrim:
    case Nb4Param::ChannelReverse:
      return ctx.channel;

    case Nb4Param::InputDualRate:
    case Nb4Param::InputResponse:
      return (uint8_t)ctx.line;
    default:
      return Nb4ParamCtx::NONE;
  }
}

int32_t channelLimit() { return g_model.extendedLimits ? LIMIT_EXT_MAX : LIMIT_STD_MAX; }

void addPpmDisplay(GVarNumberEdit* edit, bool magnitude = false)
{
  edit->setFastStep(20);
  edit->setAccelFactor(16);
  edit->setDisplayHandler([magnitude](int value) {
    if (g_eeGeneral.ppmunit == PPM_US) value = value * 128 / 25;
    if (magnitude && value < 0) value = -value;
    return formatNumberAsString(value, PREC1);
  });
}

Window* racingNumber(Window* parent, const rect_t& rect, std::function<int32_t()> get,
                     std::function<void(int32_t)> set, int32_t vmin, int32_t vmax,
                     const char* suffix, const char* zeroText)
{
  auto edit = new NumberEdit(parent, rect, vmin, vmax, std::move(get),
                             [set](int32_t v) { set(v); SET_DIRTY(); });
  if (suffix) edit->setSuffix(suffix);
  if (zeroText) edit->setZeroText(zeroText);
  return edit;
}

}  // namespace

const char* nb4ParamUnit()
{

  return g_eeGeneral.ppmunit == PPM_US ? nb4Text("(us)", "(us)")
                                       : nb4Text("(%)", "(%)");
}

const char* nb4ParamLabel(Nb4Param p)
{
  switch (p) {
    case Nb4Param::ThrottleReversed:
      return nb4Text("Invertir el gatillo", "Reverse trigger");
    case Nb4Param::ThrottleTraceSource:   return STR_TTRACE;
    case Nb4Param::ThrottleTrimIdleOnly:  return STR_TTRIM;
    case Nb4Param::ThrottleTrimSource:    return STR_TTRIM_SW;
    case Nb4Param::ChannelTravelMin:      return STR_MIN;
    case Nb4Param::ChannelTravelMax:      return STR_MAX;
    case Nb4Param::ChannelSubtrim:        return STR_LIMITS_HEADERS_SUBTRIM;
    case Nb4Param::ChannelReverse:        return STR_INVERTED;
    case Nb4Param::SteeringTrim:
      return nb4Text("Trim del volante", "Wheel trim");
    case Nb4Param::ThrottleTrim:
      return nb4Text("Trim del gatillo", "Trigger trim");
    case Nb4Param::VehicleType:
      return nb4Text("Tipo de coche", "Vehicle type");
    case Nb4Param::SteerSpeedTurn:        return STR_NB4_STEER_TURN;
    case Nb4Param::SteerSpeedReturn:      return STR_NB4_STEER_RETURN;
    case Nb4Param::BrakeMax:              return STR_NB4_BRAKE_MAX;
    case Nb4Param::DragBrake:             return STR_NB4_DRAG_BRAKE;
    case Nb4Param::AbsEnable:             return STR_NB4_ABS;
    case Nb4Param::AbsPoint:              return STR_NB4_ABS_POINT;
    case Nb4Param::AbsRate:               return STR_NB4_ABS_RATE;
    case Nb4Param::AbsRelease:            return STR_NB4_ABS_RELEASE;
    case Nb4Param::IdleUp:                return STR_NB4_IDLE_UP;
    case Nb4Param::IdleUpSwitch:          return STR_NB4_IDLE_UP_SW;
    case Nb4Param::EngineCutSwitch:       return STR_NB4_ENGINE_CUT;
    case Nb4Param::EngineCutPos:          return STR_NB4_CUT_POS;

    case Nb4Param::InputDualRate:         return nb4Text("Dual rate", "Dual rate");
    case Nb4Param::InputResponse:         return nb4Text("Respuesta", "Response");
    default:                              return "";
  }
}

namespace {
Window* buildControl(Window* parent, const rect_t& rect, Nb4Param p,
                     const Nb4ParamCtx& ctx);
}  // namespace

Nb4Numeric nb4ParamNumeric(Nb4Param p, const Nb4ParamCtx& ctx)
{
  Nb4Numeric n;

  LimitData* output =
      ctx.channel < MAX_OUTPUT_CHANNELS ? limitAddress(ctx.channel) : nullptr;
  const int32_t lim = channelLimit();

  auto racing = [&](std::function<int32_t()> get, std::function<void(int32_t)> set,
                    int32_t lo, int32_t hi, int32_t step, const char* suffix,
                    const char* zeroText) {
    n.kind = Nb4NumericKind::Plain;
    n.min = lo; n.max = hi; n.step = step;
    n.suffix = suffix; n.zeroText = zeroText;
    n.get = std::move(get);
    n.set = std::move(set);
  };

#define NB4_RACING_FIELD(f)                                                 \
  []() { return (int32_t)g_model.nb4Racing.f; },                            \
      [](int32_t v) { g_model.nb4Racing.f = v; SET_DIRTY(); }

  switch (p) {
    case Nb4Param::ChannelTravelMin:
      if (!output) break;
      n.kind = Nb4NumericKind::Gvar;
      n.min = -lim; n.max = 0; n.step = 5;
      n.prec1 = true; n.magnitude = ctx.magnitude;
      n.voffset = -LIMIT_STD_MAX; n.vdefault = -lim;
      n.get = [output]() { return (int32_t)output->min; };
      n.set = [output](int32_t v) { output->min = v; SET_DIRTY(); };
      break;

    case Nb4Param::ChannelTravelMax:
      if (!output) break;
      n.kind = Nb4NumericKind::Gvar;
      n.min = 0; n.max = +lim; n.step = 5;
      n.prec1 = true; n.magnitude = ctx.magnitude;
      n.voffset = +LIMIT_STD_MAX; n.vdefault = lim;
      n.get = [output]() { return (int32_t)output->max; };
      n.set = [output](int32_t v) { output->max = v; SET_DIRTY(); };
      break;

    case Nb4Param::ChannelSubtrim:
      if (!output) break;
      n.kind = Nb4NumericKind::Gvar;
      n.min = -LIMIT_STD_MAX; n.max = +LIMIT_STD_MAX; n.step = 5;
      n.prec1 = true;
      n.get = [output]() { return (int32_t)output->offset; };
      n.set = [output](int32_t v) { output->offset = v; SET_DIRTY(); };
      break;

    case Nb4Param::SteeringTrim:
    case Nb4Param::ThrottleTrim: {
      const uint8_t throttleIdx = inputMappingGetThrottle();
      const uint8_t idx = p == Nb4Param::ThrottleTrim
                              ? throttleIdx
                              : (uint8_t)(throttleIdx == 0 ? 1 : 0);
      const int32_t limitTrim = g_model.extendedTrims ? TRIM_EXTENDED_MAX : TRIM_MAX;
      n.kind = Nb4NumericKind::Plain;
      n.min = -limitTrim; n.max = +limitTrim; n.step = 1;
      n.get = [idx]() { return (int32_t)getTrimValue(0, idx); };
      n.set = [idx](int32_t v) { setTrimValue(0, idx, v); SET_DIRTY(); };
      break;
    }

    case Nb4Param::SteerSpeedTurn:
      racing(NB4_RACING_FIELD(steerSpeedTurn), 0, 50, 1, nullptr, STR_NONE);
      break;
    case Nb4Param::SteerSpeedReturn:
      racing(NB4_RACING_FIELD(steerSpeedReturn), 0, 50, 1, nullptr, STR_NONE);
      break;
    case Nb4Param::BrakeMax:
      racing(NB4_RACING_FIELD(brakeMax), 0, 100, 5, "%", nullptr);
      break;
    case Nb4Param::DragBrake:
      racing(NB4_RACING_FIELD(dragBrake), 0, 100, 5, "%", nullptr);
      break;
    case Nb4Param::AbsPoint:
      racing(NB4_RACING_FIELD(absPoint), 0, 100, 5, "%", nullptr);
      break;
    case Nb4Param::AbsRate:

      racing(NB4_RACING_FIELD(absRate), 1, 20, 1, "Hz", nullptr);
      break;
    case Nb4Param::AbsRelease:
      racing(NB4_RACING_FIELD(absRelease), 0, 100, 5, "%", nullptr);
      break;
    case Nb4Param::IdleUp:
      racing(NB4_RACING_FIELD(idleUp), 0, 100, 5, "%", nullptr);
      break;

    case Nb4Param::EngineCutPos: {
      n.kind = Nb4NumericKind::Plain;
      n.min = -100; n.max = 100; n.step = 5; n.suffix = "%";
      n.get = []() { return (int32_t)g_model.nb4Racing.engineCutPos; };
      n.set = [](int32_t v) { g_model.nb4Racing.engineCutPos = (int8_t)v; SET_DIRTY(); };
      break;
    }

    case Nb4Param::InputDualRate: {
      if (ctx.line < 0 || ctx.line >= MAX_EXPOS) break;
      ExpoData* input = expoAddress(ctx.line);
      n.kind = Nb4NumericKind::Source;

      n.min = -100; n.max = 100; n.step = 1; n.suffix = "%";
      n.get = [input]() { return (int32_t)input->weight; };
      n.set = [input](int32_t v) { input->weight = v; SET_DIRTY(); };
      break;
    }

    case Nb4Param::InputResponse: {
      if (ctx.line < 0 || ctx.line >= MAX_EXPOS) break;
      ExpoData* input = expoAddress(ctx.line);
      if (input->curve.type != CURVE_REF_EXPO && input->curve.type != CURVE_REF_DIFF)
        break;
      n.kind = Nb4NumericKind::Source;

      n.min = -100; n.max = 100; n.step = 1; n.suffix = "%";
      n.get = [input]() { return (int32_t)input->curve.value; };
      n.set = [input](int32_t v) { input->curve.value = v; SET_DIRTY(); };
      break;
    }

    default:
      break;
  }
#undef NB4_RACING_FIELD

  if (n.kind == Nb4NumericKind::Gvar && n.get && GV_IS_GV_VALUE(n.get())) {
    n.storedReference = n.kind;
    n.kind = Nb4NumericKind::None;
  }
  if (n.kind == Nb4NumericKind::Source && n.get) {
    SourceNumVal v;
    v.rawValue = (int16_t)n.get();
    if (v.isSource) {
      n.storedReference = n.kind;
      n.kind = Nb4NumericKind::None;
    }
  }

  if (n.kind == Nb4NumericKind::Gvar) {
    auto raw = n.get;
    auto write = n.set;
    const int32_t voffset = n.voffset;
    n.get = [raw, voffset]() { return raw() + voffset; };
    n.set = [write, voffset](int32_t v) { write(v - voffset); };
  } else if (n.kind == Nb4NumericKind::Source) {
    auto raw = n.get;
    auto write = n.set;
    n.get = [raw]() {
      SourceNumVal v;
      v.rawValue = (int16_t)raw();
      return (int32_t)v.value;
    };
    n.set = [write](int32_t v) {
      SourceNumVal out;
      out.isSource = false;
      out.value = (int16_t)v;
      write(out.rawValue);
    };
  }
  if (n.valid() && n.magnitude && n.max <= 0) {
    // Travel is displayed as a positive magnitude. Both the numeric editor and
    // +/- buttons must operate in that same domain, even for the negative limit.
    auto get = n.get;
    auto set = n.set;
    const int32_t lo = n.min;
    n.min = -n.max;
    n.max = -lo;
    n.vdefault = -n.vdefault;
    n.get = [get]() { return -get(); };
    n.set = [set](int32_t value) { set(-value); };
  }
  return n;
}

namespace {

Window* buildNumeric(Window* parent, const rect_t& rect, const Nb4Numeric& n)
{

  const LcdFlags flags = (n.prec1 ? PREC1 : 0) | FONT(BOLD);
  if (n.kind == Nb4NumericKind::None) return nullptr;

  const bool magnitude = n.magnitude;
  const bool prec1 = n.prec1;
  auto reader = [get = n.get]() { return (int)get(); };
  auto writer = [set = n.set](int v) { set(v); };
  const int32_t lo = n.min, hi = n.max;

  auto edit = new NumberEdit(parent, rect, lo, hi, reader, writer, flags);
  if (n.suffix) edit->setSuffix(n.suffix);
  if (n.zeroText) edit->setZeroText(n.zeroText);
  if (prec1) {

    edit->setDisplayHandler([magnitude](int value) {
      if (g_eeGeneral.ppmunit == PPM_US) value = value * 128 / 25;
      if (magnitude && value < 0) value = -value;
      return formatNumberAsString(value, PREC1);
    });
    edit->setFastStep(20);
    edit->setAccelFactor(16);
  }
  return edit;
}

}  // namespace

Window* nb4ParamControl(Window* parent, const rect_t& rect, Nb4Param p,
                        const Nb4ParamCtx& ctx)
{
  record(p, channelOf(p, ctx));
  const Nb4Numeric spec = nb4ParamNumeric(p, ctx);
  Window* built = spec.valid() ? buildNumeric(parent, rect, spec)
                               : buildControl(parent, rect, p, ctx);
  if (built && _objectCount < kMaxObjects)
    _objects[_objectCount++] = built->getLvObj();
  return built;
}

namespace {

Window* buildControl(Window* parent, const rect_t& rect, Nb4Param p,
                     const Nb4ParamCtx& ctx)
{
  LimitData* output = ctx.channel < MAX_OUTPUT_CHANNELS
                          ? limitAddress(ctx.channel)
                          : nullptr;
  const int32_t lim = channelLimit();

  switch (p) {

    case Nb4Param::ThrottleReversed: {
      auto after = ctx.afterChange;
      return new ToggleSwitch(parent, rect, GET_DEFAULT(g_model.throttleReversed),
                              [after](uint8_t v) {
                                g_model.throttleReversed = v;
                                SET_DIRTY();
                                if (after) after();
                              });
    }

    case Nb4Param::ThrottleTraceSource: {
      auto sc = new SourceChoice(
          parent, rect, 0, MIXSRC_LAST_CH,
          []() { return throttleSource2Source(g_model.thrTraceSrc); },
          [](int16_t src) {
            const int16_t val = source2ThrottleSource(src);
            if (val >= 0) { g_model.thrTraceSrc = val; SET_DIRTY(); }
          });
      sc->setAvailableHandler(isThrottleSourceAvailable);
      return sc;
    }

    case Nb4Param::ThrottleTrimIdleOnly:
      return new ToggleSwitch(parent, rect, GET_SET_DEFAULT(g_model.thrTrim));

    case Nb4Param::ThrottleTrimSource:
      return new SourceChoice(
          parent, rect, MIXSRC_FIRST_TRIM, MIXSRC_LAST_TRIM,
          []() { return (int16_t)g_model.getThrottleStickTrimSource(); },
          [](int16_t src) {
            g_model.setThrottleStickTrimSource(src);
            SET_DIRTY();
          });

    case Nb4Param::ChannelTravelMin: {
      if (!output) return nullptr;
      auto edit = new GVarNumberEdit(parent, -lim, 0, GET_SET_DEFAULT(output->min),
                                     PREC1, -LIMIT_STD_MAX, -lim);
      addPpmDisplay(edit, ctx.magnitude);
      return edit;
    }

    case Nb4Param::ChannelTravelMax: {
      if (!output) return nullptr;
      auto edit = new GVarNumberEdit(parent, 0, +lim, GET_SET_DEFAULT(output->max),
                                     PREC1, +LIMIT_STD_MAX, lim);
      addPpmDisplay(edit, ctx.magnitude);
      return edit;
    }

    case Nb4Param::ChannelSubtrim: {
      if (!output) return nullptr;
      auto edit = new GVarNumberEdit(parent, -LIMIT_STD_MAX, +LIMIT_STD_MAX,
                                     GET_SET_DEFAULT(output->offset), PREC1);
      addPpmDisplay(edit);
      return edit;
    }

    case Nb4Param::ChannelReverse: {
      if (!output) return nullptr;
      auto after = ctx.afterChange;
      return new ToggleSwitch(parent, rect, GET_DEFAULT(output->revert),
                              [output, after](uint8_t v) {
                                output->revert = v;
                                SET_DIRTY();
                                if (after) after();
                              });
    }

    case Nb4Param::SteeringTrim:
    case Nb4Param::ThrottleTrim: {
      const uint8_t throttleIdx = inputMappingGetThrottle();
      const uint8_t idx = p == Nb4Param::ThrottleTrim
                              ? throttleIdx
                              : (uint8_t)(throttleIdx == 0 ? 1 : 0);
      const int32_t limit = g_model.extendedTrims ? TRIM_EXTENDED_MAX : TRIM_MAX;
      return new NumberEdit(parent, rect, -limit, +limit,
                            [idx]() { return getTrimValue(0, idx); },
                            [idx](int32_t v) {
                              setTrimValue(0, idx, v);
                              SET_DIRTY();
                            });
    }

    case Nb4Param::VehicleType: {

      auto choice = new Choice(
          parent, rect, NB4_VEHICLE_UNSET, NB4_VEHICLE_CUSTOM,
          []() { return (int)g_model.nb4Racing.vehicleType; },
          [](int v) { g_model.nb4Racing.vehicleType = v; SET_DIRTY(); });
      choice->setTextHandler([](int v) {
        switch (v) {
          case NB4_VEHICLE_ELECTRIC: return std::string(STR_NB4_ELECTRIC);
          case NB4_VEHICLE_NITRO:    return std::string(STR_NB4_NITRO);
          case NB4_VEHICLE_CUSTOM:
            return std::string(nb4Text("Personalizado", "Custom"));
          default:
            return std::string(nb4Text("Sin definir", "Not set"));
        }
      });
      return choice;
    }

    case Nb4Param::SteerSpeedTurn:

      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.steerSpeedTurn),
                          [](int32_t v) { g_model.nb4Racing.steerSpeedTurn = v; },
                          0, 50, nullptr, STR_NONE);

    case Nb4Param::SteerSpeedReturn:
      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.steerSpeedReturn),
                          [](int32_t v) { g_model.nb4Racing.steerSpeedReturn = v; },
                          0, 50, nullptr, STR_NONE);

    case Nb4Param::BrakeMax:
      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.brakeMax),
                          [](int32_t v) { g_model.nb4Racing.brakeMax = v; },
                          0, 100, "%", nullptr);

    case Nb4Param::DragBrake:
      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.dragBrake),
                          [](int32_t v) { g_model.nb4Racing.dragBrake = v; },
                          0, 100, "%", nullptr);

    case Nb4Param::AbsEnable:
      return new ToggleSwitch(parent, rect, GET_DEFAULT(g_model.nb4Racing.absEnable),
                              [](uint8_t v) {
                                g_model.nb4Racing.absEnable = v;
                                SET_DIRTY();
                              });

    case Nb4Param::AbsPoint:
      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.absPoint),
                          [](int32_t v) { g_model.nb4Racing.absPoint = v; },
                          0, 100, "%", nullptr);

    case Nb4Param::AbsRate:

      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.absRate),
                          [](int32_t v) { g_model.nb4Racing.absRate = v; },
                          1, 20, "Hz", nullptr);

    case Nb4Param::AbsRelease:
      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.absRelease),
                          [](int32_t v) { g_model.nb4Racing.absRelease = v; },
                          0, 100, "%", nullptr);

    case Nb4Param::IdleUp:
      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.idleUp),
                          [](int32_t v) { g_model.nb4Racing.idleUp = v; },
                          0, 100, "%", nullptr);

    case Nb4Param::IdleUpSwitch:
      return new SwitchChoice(parent, rect, SWSRC_FIRST, SWSRC_LAST,
                              GET_SET_DEFAULT(g_model.nb4Racing.idleUpSw));

    case Nb4Param::EngineCutSwitch:
      return new SwitchChoice(parent, rect, SWSRC_FIRST, SWSRC_LAST,
                              GET_SET_DEFAULT(g_model.nb4Racing.engineCutSw));

    case Nb4Param::EngineCutPos:
      return racingNumber(parent, rect, GET_DEFAULT(g_model.nb4Racing.engineCutPos),
                          [](int32_t v) { g_model.nb4Racing.engineCutPos = v; },
                          -100, 100, "%", nullptr);

    case Nb4Param::InputDualRate: {
      if (ctx.line < 0 || ctx.line >= MAX_EXPOS) return nullptr;
      ExpoData* input = expoAddress(ctx.line);

      auto edit = new SourceNumberEdit(parent, -100, 100, GET_DEFAULT(input->weight),
                                       [input](int32_t v) {
                                         input->weight = v;
                                         SET_DIRTY();
                                       },
                                       MIXSRC_FIRST);
      edit->setSuffix("%");
      return edit;
    }

    case Nb4Param::InputResponse: {
      if (ctx.line < 0 || ctx.line >= MAX_EXPOS) return nullptr;
      ExpoData* input = expoAddress(ctx.line);

      return new CurveParam(parent, rect, &input->curve,
                            [input](int32_t v) {
                              input->curve.value = v;
                              SET_DIRTY();
                            },
                            MIXSRC_FIRST, input->srcRaw);
    }

    default:
      return nullptr;
  }
}

class Nb4StepButton : public TextButton
{
 public:
  Nb4StepButton(Window* parent, const rect_t& rect, const char* text,
                std::function<void(int32_t)> apply, int32_t step) :
      TextButton(parent, rect, text,
                 [apply, step]() { apply(step); return (uint8_t)0; }),
      apply(std::move(apply)), step(step)
  {
    lv_obj_add_event_cb(lvobj, onRepeat, LV_EVENT_LONG_PRESSED_REPEAT, this);
  }

  bool onLongPress() override
  {
    repeats = 0;
    return true;
  }

 protected:
  std::function<void(int32_t)> apply;
  int32_t step;
  unsigned repeats = 0;

  static void onRepeat(lv_event_t* e)
  {
    auto* self = (Nb4StepButton*)lv_event_get_user_data(e);
    if (!self) return;
    self->repeats += 1;
    const unsigned r = self->repeats;
    const int32_t factor = r <= 5 ? 1 : r <= 15 ? 2 : r <= 30 ? 5 : 10;
    self->apply(self->step * factor);
  }
};

void referenceHint(Window* form, FlexGridLayout& grid, const Nb4Numeric& spec)
{
  if (spec.storedReference == Nb4NumericKind::None) return;

  const char* text = nullptr;
  if (spec.storedReference == Nb4NumericKind::Gvar) {
    text = modelGVEnabled()
               ? nb4Text("Este ajuste toma su valor de una variable del modelo. "
                         "El botón GV lo devuelve a una cifra.",
                         "This setting takes its value from a model variable. "
                         "The GV button turns it back into a figure.")
               : nb4Text("Este ajuste toma su valor de una variable del modelo. "
                         "Para devolverlo a una cifra, enciende las variables en "
                         "Avanzado > Variables del modelo.",
                         "This setting takes its value from a model variable. To "
                         "turn it back into a figure, switch variables on under "
                         "Advanced > Model variables.");
  } else {
    text = nb4Text("Este ajuste toma su valor de otro mando. El botón SRC lo "
                   "devuelve a una cifra.",
                   "This setting takes its value from another control. The SRC "
                   "button turns it back into a figure.");
  }

  auto line = form->newLine(grid);
  auto note = new StaticText(line, rect_t{}, text, COLOR_THEME_PRIMARY3_INDEX,
                             FONT(XS));
  lv_obj_set_style_grid_cell_column_span(note->getLvObj(), 2, LV_PART_MAIN);
  lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP);
}

}  // namespace

void nb4ParamRow(Window* form, FlexGridLayout& grid, Nb4Param p,
                 const Nb4ParamCtx& ctx, const char* labelOverride)
{
  const char* label = labelOverride ? labelOverride : nb4ParamLabel(p);
  const Nb4Numeric spec = nb4ParamNumeric(p, ctx);

  if (spec.valid()) {
    auto labelLine = form->newLine(grid);
    auto text = new StaticText(labelLine, rect_t{}, label);
    lv_obj_set_style_grid_cell_column_span(text->getLvObj(), 2, LV_PART_MAIN);

    auto controlLine = form->newLine(grid);
    auto box = new Window(controlLine, rect_t{});
    lv_obj_set_style_grid_cell_column_span(box->getLvObj(), 2, LV_PART_MAIN);
    box->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_SMALL, LV_SIZE_CONTENT);
    lv_obj_set_style_flex_cross_place(box->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);

    const int32_t step = spec.step;
    auto get = spec.get;
    auto set = spec.set;
    const int32_t lo = spec.min, hi = spec.max;
    auto nudge = [get, set, lo, hi](int32_t delta) {
      if (!get || !set) return (uint8_t)0;
      set(limit<int32_t>(lo, get() + delta, hi));
      return (uint8_t)0;
    };

    auto apply = [nudge](int32_t delta) { nudge(delta); };
    noteObject(new Nb4StepButton(
        box, rect_t{0, 0, EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_LARGE, 0},
        LV_SYMBOL_MINUS, apply, -step));
    nb4ParamControl(box, rect_t{}, p, ctx);
    noteObject(new Nb4StepButton(
        box, rect_t{0, 0, EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_LARGE, 0},
        LV_SYMBOL_PLUS, apply, step));
    return;
  }

  const bool wideControl = p == Nb4Param::InputResponse;
  const bool narrow = lv_disp_get_hor_res(nullptr) <= 320;

  if (wideControl && narrow) {
    auto labelLine = form->newLine(grid);
    auto text = new StaticText(labelLine, rect_t{}, label);
    lv_obj_set_style_grid_cell_column_span(text->getLvObj(), 2, LV_PART_MAIN);

    auto controlLine = form->newLine(grid);
    if (Window* control = nb4ParamControl(controlLine, rect_t{}, p, ctx))
      lv_obj_set_style_grid_cell_column_span(control->getLvObj(), 2, LV_PART_MAIN);
    referenceHint(form, grid, spec);
    return;
  }

  auto line = form->newLine(grid);
  new StaticText(line, rect_t{}, label);
  nb4ParamControl(line, rect_t{}, p, ctx);
  referenceHint(form, grid, spec);
}

namespace {

/* Half row: label above, [-] value [+] below. */
void buildTile(Window* parent, coord_t width, Nb4Param p, const Nb4ParamCtx& ctx,
               const char* label, const Nb4Numeric& spec)
{
  auto tile = new Window(parent, rect_t{0, 0, width, 0});
  tile->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(tile->getLvObj(), 0, LV_PART_MAIN);

  auto text = new StaticText(tile, rect_t{0, 0, width, 0}, label,
                             COLOR_THEME_PRIMARY1_INDEX, FONT(XS));
  lv_label_set_long_mode(text->getLvObj(), LV_LABEL_LONG_DOT);

  auto row = new Window(tile, rect_t{0, 0, width, 0});
  row->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_TINY, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(row->getLvObj(), 0, LV_PART_MAIN);
  lv_obj_set_style_flex_cross_place(row->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);

  const int32_t step = spec.step;
  auto get = spec.get;
  auto set = spec.set;
  const int32_t lo = spec.min, hi = spec.max;
  auto nudge = [get, set, lo, hi](int32_t delta) {
    if (!get || !set) return (uint8_t)0;
    set(limit<int32_t>(lo, get() + delta, hi));
    return (uint8_t)0;
  };

  const coord_t btn = EdgeTxStyles::UI_ELEMENT_HEIGHT;
  auto apply = [nudge](int32_t delta) { nudge(delta); };
  noteObject(new Nb4StepButton(row, rect_t{0, 0, btn, 0}, LV_SYMBOL_MINUS,
                               apply, -step));
  nb4ParamControl(row, rect_t{0, 0, (coord_t)(width - 2 * btn - PAD_TINY * 2), 0},
                  p, ctx);
  noteObject(new Nb4StepButton(row, rect_t{0, 0, btn, 0}, LV_SYMBOL_PLUS,
                               apply, step));
}

}  // namespace

void nb4ParamPair(Window* form, FlexGridLayout& grid, Nb4Param a,
                  const Nb4ParamCtx& ctxA, const char* labelA, Nb4Param b,
                  const Nb4ParamCtx& ctxB, const char* labelB)
{
  const Nb4Numeric specA = nb4ParamNumeric(a, ctxA);
  const Nb4Numeric specB = nb4ParamNumeric(b, ctxB);

  if (!specA.valid() || !specB.valid()) {
    nb4ParamRow(form, grid, a, ctxA, labelA);
    nb4ParamRow(form, grid, b, ctxB, labelB);
    return;
  }

  const coord_t total = lv_disp_get_hor_res(nullptr) - PAD_LARGE * 2;
  const coord_t half = (coord_t)((total - PAD_SMALL) / 2);

  auto line = form->newLine(grid);
  auto box = new Window(line, rect_t{0, 0, total, 0});
  lv_obj_set_style_grid_cell_column_span(box->getLvObj(), 2, LV_PART_MAIN);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_SMALL, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(box->getLvObj(), 0, LV_PART_MAIN);

  buildTile(box, half, a, ctxA, labelA, specA);
  buildTile(box, half, b, ctxB, labelB, specB);
}

void nb4ParamRegistryReset() { _entryCount = 0; _objectCount = 0; }

bool nb4ParamOwnsObject(const void* lvObject)
{
  const lv_obj_t* obj = (const lv_obj_t*)lvObject;
  while (obj) {
    for (unsigned i = 0; i < _objectCount; i += 1)
      if (_objects[i] == (const void*)obj) return true;
    obj = lv_obj_get_parent((lv_obj_t*)obj);
  }
  return false;
}

unsigned nb4ParamBuilt(Nb4Param p, uint8_t channel)
{
  for (unsigned i = 0; i < _entryCount; i += 1)
    if (_entries[i].param == p && _entries[i].channel == channel)
      return _entries[i].count;
  return 0;
}

unsigned nb4ParamRegistrySize() { return _entryCount; }

void nb4ParamRegistryEntry(unsigned index, Nb4Param* p, uint8_t* channel,
                           unsigned* count)
{
  if (index >= _entryCount) return;
  if (p) *p = _entries[index].param;
  if (channel) *channel = _entries[index].channel;
  if (count) *count = _entries[index].count;
}

#endif  // RADIO_NB4_FAMILY
