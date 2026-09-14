/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_ui.h"
#include "nb4_home.h"
#include "nb4_racing.h"
#include "etx_lv_theme.h"
#include "hal/adc_driver.h"
#include "layer.h"
#include "quick_menu.h"
#include "view_main.h"
#include <array>

extern "C" {
LV_FONT_DECLARE(lv_font_nb4_gauge_22);
LV_FONT_DECLARE(lv_font_nb4_percent_15);
}

namespace {

alignas(4) constexpr auto nearBlackPixels = [] {
  std::array<uint16_t, 128 * 128> pixels{};
  for (unsigned y = 0; y < 128; y += 2)
    for (unsigned x = 0; x < 128; x += 2)
      pixels[y * 128 + x] = RGB(8, 8, 8);
  return pixels;
}();
const lv_img_dsc_t nearBlackSurface = {
  {LV_IMG_CF_TRUE_COLOR, 0, 0, 128, 128},
  sizeof(nearBlackPixels), reinterpret_cast<const uint8_t*>(nearBlackPixels.data())
};

void drawNearBlackCard(lv_event_t* event)
{
  auto object = lv_event_get_target(event);
  auto context = lv_event_get_draw_ctx(event);
  lv_area_t area, clip;
  lv_obj_get_coords(object, &area);
  if (!_lv_area_intersect(&clip, &area, context->clip_area)) return;

  const auto radius = lv_obj_get_style_radius(object, LV_PART_MAIN);
  const bool masked = !_lv_area_is_in(&clip, &area, radius);
  lv_draw_mask_radius_param_t mask;
  int16_t maskId = -1;
  if (masked) {
    lv_draw_mask_radius_init(&mask, &area, radius, false);
    maskId = lv_draw_mask_add(&mask, nullptr);
  }
  lv_draw_rect_dsc_t background;
  lv_draw_rect_dsc_init(&background);
  background.bg_opa = LV_OPA_TRANSP;
  background.bg_img_src = &nearBlackSurface;
  background.bg_img_tiled = true;
  lv_draw_rect(context, &background, &area);
  if (masked) {
    lv_draw_mask_remove_id(maskId);
    lv_draw_mask_free_param(&mask);
  }
}
}  // namespace

namespace Nb4Ui {
lv_color_t color(LcdColorIndex i) { return makeLvColor(COLOR(i)); }
static bool light() { return lv_color_brightness(color(COLOR_THEME_PRIMARY2_INDEX)) > 150; }

lv_color_t throttleColor() { return lv_color_hex(light() ? 0x1B7A34 : 0x3FD956); }
lv_color_t steeringColor() { return lv_color_hex(light() ? 0x0A5FA8 : 0x35A7F0); }
lv_color_t brakeColor()    { return lv_color_hex(light() ? 0xB02A18 : 0xFF5A3C); }

void panel(lv_obj_t* o)
{
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  etx_solid_bg(o, COLOR_THEME_SECONDARY3_INDEX);
  lv_obj_set_style_radius(o, 9, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  etx_border_color(o, COLOR_THEME_SECONDARY2_INDEX);
}

void card(lv_obj_t* o)
{
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  etx_solid_bg(o, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_radius(o, 10, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  etx_border_color(o, COLOR_THEME_SECONDARY2_INDEX);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_event_cb(o, drawNearBlackCard);
  if (lcdColorTable[COLOR_THEME_PRIMARY2_INDEX] == RGB(8, 8, 8) &&
      lcdColorTable[COLOR_THEME_SECONDARY3_INDEX] == RGB(0, 0, 0)) {
    etx_solid_bg(o, COLOR_THEME_SECONDARY3_INDEX);
    lv_obj_add_event_cb(o, drawNearBlackCard, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_set_style_border_post(o, true, 0);
  }
}

void well(lv_obj_t* o)
{
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  etx_solid_bg(o, COLOR_THEME_SECONDARY3_INDEX);
  lv_obj_set_style_radius(o, 7, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  etx_border_color(o, COLOR_THEME_SECONDARY2_INDEX);
  lv_obj_set_style_pad_all(o, 0, 0);
}

void chip(lv_obj_t* o)
{
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(o, 11, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  etx_border_color(o, COLOR_THEME_SECONDARY2_INDEX);
  lv_obj_set_style_pad_all(o, 0, 0);
}

void passThrough(lv_obj_t* root)
{
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto child = lv_obj_get_child(root, i);
    lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(child, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    passThrough(child);
  }
}

void button(lv_obj_t* o, bool primary)
{
  lv_obj_set_style_radius(o, 7, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  etx_border_color(o, primary ? COLOR_THEME_FOCUS_INDEX : COLOR_THEME_SECONDARY2_INDEX);
  etx_solid_bg(o, primary ? COLOR_THEME_FOCUS_INDEX : COLOR_THEME_SECONDARY3_INDEX);
  etx_txt_color(o, primary ? COLOR_THEME_PRIMARY2_INDEX : COLOR_THEME_PRIMARY1_INDEX);
  if (lv_obj_get_child_cnt(o))
    etx_txt_color(lv_obj_get_child(o, 0), primary ? COLOR_THEME_PRIMARY2_INDEX : COLOR_THEME_PRIMARY1_INDEX);
  if (primary) {
    auto ink = lv_color_brightness(color(COLOR_THEME_FOCUS_INDEX)) > 140 ? lv_color_hex(0x071015) : lv_color_hex(0xFFFFFF);
    lv_obj_set_style_text_color(o, ink, 0);
    if (lv_obj_get_child_cnt(o)) lv_obj_set_style_text_color(lv_obj_get_child(o, 0), ink, 0);
  }
  // Keep a visible keyboard focus, without a permanent thick touch outline.
  lv_obj_set_style_outline_width(o, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(o, 2, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_color(o, color(COLOR_THEME_FOCUS_INDEX), LV_STATE_FOCUS_KEY);
}

TextButton* action(Window* parent, rect_t r, const char* title, std::function<void()> fn, bool primary)
{
  r.h = max<coord_t>(Touch, r.h);
  auto result = new TextButton(parent, r, title, [fn] { fn(); return 0; });
  result->setFont(FONT_STD_INDEX);
  button(result->getLvObj(), primary);
  return result;
}

void header(Window* parent, const char* title, std::function<void()> back)
{
  auto b = action(parent, {8, 4, 44, 44}, LV_SYMBOL_LEFT, back);
  lv_obj_set_style_border_width(b->getLvObj(), 0, 0);
  etx_solid_bg(b->getLvObj(), COLOR_THEME_PRIMARY2_INDEX);
  nb4Label(parent, {60, 15, parent->width() - 72, 26}, title, FONT(BOLD));
  nb4Hairline(parent, {8, 51, parent->width() - 16, 1});
}

void inkIfChanged(StaticText* label, LcdColorIndex& last, LcdColorIndex want)
{
  if (last == want) return;
  last = want;
  etx_txt_color(label->getLvObj(), want);
}

const char* phaseText()
{
  switch (nb4RacePhase()) {
    case Nb4RacePhase::Running: return nb4RaceIsPaused() ? nb4Text("En pausa", "Paused") : nb4Text("En marcha", "Running");
    case Nb4RacePhase::Finished: return nb4Text("Finalizada", "Finished");
    default: return nb4Text("Preparada", "Ready");
  }
}

std::string timeText(uint32_t cs, bool available)
{
  if (!available) return "--";
  char text[20];
  snprintf(text, sizeof(text), "%02lu:%02lu.%lu", (unsigned long)(cs / 6000),
      (unsigned long)(cs / 100 % 60), (unsigned long)(cs / 10 % 10));
  return text;
}

std::string timerText(int32_t seconds, bool available)
{
  if (!available) return "--";
  const bool negative = seconds < 0;
  uint32_t value = negative ? uint32_t(-int64_t(seconds)) : uint32_t(seconds);
  char text[20];
  if (value >= 3600)
    snprintf(text, sizeof(text), "%s%lu:%02lu:%02lu", negative ? "-" : "",
      (unsigned long)(value / 3600), (unsigned long)(value / 60 % 60),
      (unsigned long)(value % 60));
  else
    snprintf(text, sizeof(text), "%s%02lu:%02lu", negative ? "-" : "",
      (unsigned long)(value / 60), (unsigned long)(value % 60));
  return text;
}
}

// ---------------------------------------------------------------------------
// Shared instrument components
// ---------------------------------------------------------------------------

namespace {

constexpr coord_t DIAL_R = 70;         // Outer track radius
constexpr coord_t DIAL_TRACK = 8;
constexpr coord_t DIAL_BORDER = 1;
constexpr coord_t TICK_OUT = 58, TICK_MINOR_IN = 53, TICK_MAJOR_IN = 49;
constexpr coord_t ZERO_IN = 49, ZERO_OUT = 62;
constexpr coord_t NEEDLE_LEN = 45, NEEDLE_HUB = 9;

constexpr coord_t NEEDLE_OX = 43, NEEDLE_OY = 48, NEEDLE_W = 86, NEEDLE_H = 74;

constexpr coord_t LABEL_R = DIAL_R + 13;

void polar(coord_t cx, coord_t cy, int deg, int r, lv_point_t& p)
{
  const int32_t s = lv_trigo_sin((int16_t)deg);
  const int32_t c = lv_trigo_sin((int16_t)(deg + 90));
  p.x = (coord_t)(cx + (r * s + (s >= 0 ? 16384 : -16384)) / 32768);
  p.y = (coord_t)(cy - (r * c + (c >= 0 ? 16384 : -16384)) / 32768);
}

void gaugeFont(StaticText* label, const lv_font_t* font, coord_t height)
{
  lv_obj_set_style_text_font(label->getLvObj(), font, 0);
  lv_obj_set_style_pad_top(label->getLvObj(), (height - font->line_height) / 2, 0);
}

void centerGaugeValue(StaticText* digits, StaticText* pct, const char* text,
                      coord_t center, uint8_t& lastWidth)
{
  const uint8_t width = lv_txt_get_width(text, strlen(text),
                                        &lv_font_nb4_gauge_22, 0, LV_TEXT_FLAG_NONE);
  if (width == lastWidth) return;
  lastWidth = width;
  const coord_t pctWidth = lv_txt_get_width("%", 1, &lv_font_nb4_percent_15,
                                           0, LV_TEXT_FLAG_NONE);
  const coord_t right = center + (width - pctWidth) / 2;
  lv_obj_set_width(digits->getLvObj(), right);
  lv_obj_set_x(pct->getLvObj(), right);
}

const uint8_t wedgeDownMask[9 * 5] = {
  255, 255, 255, 255, 255, 255, 255, 255, 255,
    0, 255, 255, 255, 255, 255, 255, 255,   0,
    0,   0, 255, 255, 255, 255, 255,   0,   0,
    0,   0,   0, 255, 255, 255,   0,   0,   0,
    0,   0,   0,   0, 255,   0,   0,   0,   0,
};

const uint8_t wedgeRightMask[5 * 9] = {
  255,   0,   0,   0,   0,
  255, 255,   0,   0,   0,
  255, 255, 255,   0,   0,
  255, 255, 255, 255,   0,
  255, 255, 255, 255, 255,
  255, 255, 255, 255,   0,
  255, 255, 255,   0,   0,
  255, 255,   0,   0,   0,
  255,   0,   0,   0,   0,
};

lv_obj_t* maskCanvas(lv_obj_t* parent, const uint8_t* mask, coord_t w, coord_t h,
                     coord_t x, coord_t y, lv_color_t ink)
{
  auto c = lv_canvas_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_canvas_set_buffer(c, (void*)mask, w, h, LV_IMG_CF_ALPHA_8BIT);
  lv_obj_set_style_img_recolor(c, ink, LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(c, LV_OPA_COVER, LV_PART_MAIN);
  return c;
}

lv_obj_t* solidRect(lv_obj_t* parent, coord_t x, coord_t y, coord_t w, coord_t h,
                    LcdColorIndex ink, coord_t radius = 0)
{
  auto o = lv_obj_create(parent);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_radius(o, radius, 0);
  etx_solid_bg(o, ink);
  return o;
}

StaticText* caption(Window* parent, rect_t r, const char* text, LcdFlags align = 0,
                    LcdColorIndex ink = COLOR_THEME_PRIMARY3_INDEX)
{
  auto t = nb4Label(parent, r, text, FONT(XXS) | align, ink);
  lv_obj_set_style_text_letter_space(t->getLvObj(), 1, LV_PART_MAIN);
  return t;
}

void fittedNumber(StaticText* label, const std::string& text, LcdFlags maximum)
{

  lv_obj_update_layout(label->getLvObj());
  const LcdFlags sizes[] = {FONT(XL), FONT(L), FONT(BOLD), FONT(XS), FONT(XXS)};
  LcdFlags selected = FONT(XXS);
  for (const auto size : sizes) {
    if (getFontHeight(size) <= getFontHeight(maximum) &&
        getTextWidth(text.c_str(), 0, size) <= lv_obj_get_content_width(label->getLvObj())) {
      selected = size;
      break;
    }
  }
  auto object = label->getLvObj();
  if (lv_obj_get_style_text_font(object, LV_PART_MAIN) != getFont(selected)) {
    etx_font(object, FONT_INDEX(selected));
    lv_obj_set_style_pad_top(object, max<coord_t>(0, (label->height() - getFontHeight(selected)) / 2), 0);
  }
  label->setText(text);
}

/* Outlined "TRIM  +2" chip used by both instruments. */
StaticText* trimChip(Window* parent, rect_t r, lv_color_t accent)
{
  auto box = new Window(parent, r);
  Nb4Ui::chip(box->getLvObj());
  caption(box, {6, 7, 24, 12}, "TRIM");
  auto value = nb4Label(box, {34, 4, 28, 17}, "--", FONT(XS) | RIGHT);
  lv_obj_set_style_text_color(value->getLvObj(), accent, 0);
  return value;
}

const char* stateName(uint8_t index)
{
  switch (index) {
    case 1: return nb4Text("En marcha", "Running");
    case 2: return nb4Text("Finalizado", "Finished");
    case 3: return nb4Text("Desactivado", "Disabled");
    case 4: return nb4Text("En pausa", "Paused");
    default: return nb4Text("Preparado", "Ready");
  }
}

LcdColorIndex stateColor(uint8_t index)
{
  return index == 0 ? COLOR_THEME_EDIT_INDEX
       : index == 1 ? COLOR_THEME_FOCUS_INDEX
                    : COLOR_THEME_PRIMARY3_INDEX;
}

}  // namespace

// ---------------------------------------------------------------------------
// Nb4Dial
// ---------------------------------------------------------------------------

Nb4Dial::Nb4Dial(Window* parent, rect_t r, bool landscape) : Window(parent, r)
{
  setWindowFlag(OPAQUE);
  Nb4Ui::card(lvobj);
  const auto accent = Nb4Ui::steeringColor();
  radius = DIAL_R;
  needleLen = NEEDLE_LEN;

  cx = (coord_t)(r.w / 2);
  cy = landscape ? (coord_t)98 : (coord_t)132;
  digitsCx = cx;
  const coord_t wellTop = landscape ? 150 : 188;
  const coord_t numberHeight = lv_font_nb4_gauge_22.line_height;
  const coord_t percentHeight = lv_font_nb4_percent_15.line_height;
  digitsTop = wellTop - numberHeight - 2;

  auto title = nb4Label(this, landscape ? rect_t{10, 4, 100, 20} : rect_t{8, 6, 86, 20},
                        nb4Text("DIRECCIÓN", "STEERING"), FONT(BOLD));
  lv_obj_set_style_text_color(title->getLvObj(), accent, 0);
  if (!landscape)
    caption(this, {8, 28, 120, 12}, nb4Text("ÁNGULO DE GIRO", "STEERING ANGLE"));
  chipVal = trimChip(this, {(coord_t)(width() - 76), 4, 68, 22}, accent);

  for (unsigned pass = 0; pass < 3; ++pass) {
    const coord_t inset = pass == 0 ? DIAL_BORDER : 0;
    const coord_t arcRadius = DIAL_R + inset;
    const coord_t arcWidth = DIAL_TRACK + 2 * inset;
    auto a = lv_arc_create(lvobj);

    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(a, cx - arcRadius, cy - arcRadius);
    lv_obj_set_size(a, 2 * arcRadius, 2 * arcRadius);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a, 0, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_pad_all(a, 0, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);

    lv_obj_set_style_arc_width(a, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, arcWidth, LV_PART_INDICATOR);
    lv_arc_set_rotation(a, 0);
    if (pass < 2) {
      lv_arc_set_bg_angles(a, pass == 0 ? 149 : 150, pass == 0 ? 31 : 30);
      etx_arc_color(a, pass == 0 ? COLOR_THEME_SECONDARY2_INDEX :
                                   COLOR_THEME_SECONDARY3_INDEX, LV_PART_MAIN);
      lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
      if (pass == 1) track = a;
    } else {
      lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_arc_color(a, accent, LV_PART_INDICATOR);

      lv_arc_set_angles(a, 270, 270);
      arc = a;
    }
  }

  unsigned tick = 0;
  for (int v = -100; v <= 100; v += 5) {
    if (v == 0) continue;
    const bool major = (v % 50) == 0;
    const int deg = v * 6 / 5;  // 1.2 degrees per point
    lv_point_t inner, outer;
    polar(cx, cy, deg, major ? TICK_MAJOR_IN : TICK_MINOR_IN, inner);
    polar(cx, cy, deg, TICK_OUT, outer);
    const coord_t x0 = min<coord_t>(inner.x, outer.x) - 1;
    const coord_t y0 = min<coord_t>(inner.y, outer.y) - 1;
    tickPts[tick][0] = {(lv_coord_t)(inner.x - x0), (lv_coord_t)(inner.y - y0)};
    tickPts[tick][1] = {(lv_coord_t)(outer.x - x0), (lv_coord_t)(outer.y - y0)};
    auto line = lv_line_create(lvobj);
    lv_obj_set_pos(line, x0, y0);
    lv_obj_set_size(line, (coord_t)(abs(outer.x - inner.x) + 3),
                    (coord_t)(abs(outer.y - inner.y) + 3));
    lv_obj_set_style_line_width(line, 1, 0);
    etx_line_color(line, COLOR_THEME_PRIMARY3_INDEX);
    lv_line_set_points(line, tickPts[tick], 2);
    ++tick;
  }

  solidRect(lvobj, (coord_t)(cx - 1), (coord_t)(cy - ZERO_OUT), 3,
            (coord_t)(ZERO_OUT - ZERO_IN), COLOR_THEME_PRIMARY1_INDEX);

  struct { int value; const char* text; coord_t w; } marks[] = {
    {-100, "-100", 26}, {-50, "-50", 24}, {50, "+50", 24}, {100, "+100", 26},
  };
  for (auto& m : marks) {
    lv_point_t p;
    polar(cx, cy, m.value * 6 / 5, LABEL_R, p);
    caption(this, {(coord_t)(p.x - m.w / 2), (coord_t)(p.y - 6), m.w, 12}, m.text, CENTERED);
  }
  caption(this, {(coord_t)(cx - 6), (coord_t)(cy - DIAL_R - 14), 12, 12}, "0", CENTERED);

  needle = lv_line_create(lvobj);
  lv_obj_set_pos(needle, (coord_t)(cx - NEEDLE_OX), (coord_t)(cy - NEEDLE_OY));
  lv_obj_set_size(needle, NEEDLE_W, NEEDLE_H);
  lv_obj_set_style_line_width(needle, 3, 0);
  lv_obj_set_style_line_color(needle, accent, 0);
  lv_obj_set_style_line_rounded(needle, true, 0);
  needlePts[0] = {NEEDLE_OX, NEEDLE_OY};
  needlePts[1] = {NEEDLE_OX, (lv_coord_t)(NEEDLE_OY - NEEDLE_LEN)};
  lv_line_set_points(needle, needlePts, 2);

  auto hub = solidRect(lvobj, (coord_t)(cx - 6), (coord_t)(cy - 6), 12, 12,
                       COLOR_THEME_PRIMARY1_INDEX, 6);
  lv_obj_set_style_bg_color(hub, accent, 0);

  digits = nb4Label(this, {0, digitsTop, (coord_t)(cx + 20), numberHeight}, "+0", FONT(L) | RIGHT);
  gaugeFont(digits, &lv_font_nb4_gauge_22, numberHeight);
  lv_obj_set_style_text_color(digits->getLvObj(), accent, 0);
  pct = nb4Label(this, {(coord_t)(cx + 20), (coord_t)(digitsTop + numberHeight - percentHeight),
                       20, percentHeight}, "%", FONT(STD));
  gaugeFont(pct, &lv_font_nb4_percent_15, percentHeight);
  lv_obj_set_style_text_color(pct->getLvObj(), accent, 0);

  const coord_t wellWidth = landscape ? 158 : width() - 12;
  const rect_t wellBox = {(coord_t)((width() - wellWidth) / 2),
                         wellTop,
                         wellWidth, landscape ? (coord_t)34 : (coord_t)50};
  auto box = new Window(this, wellBox);
  Nb4Ui::well(box->getLvObj());
  ruleX0 = landscape ? 15 : 18;
  ruleSpan = wellWidth - 2 * ruleX0 - 2;
  const coord_t ruleY = landscape ? 11 : 23;
  wedgeY = (coord_t)(ruleY - 8);
  if (!landscape) caption(box, {4, 2, (coord_t)(wellWidth - 10), 12}, "TRIM", CENTERED);
  solidRect(box->getLvObj(), ruleX0, ruleY, ruleSpan, 1, COLOR_THEME_SECONDARY2_INDEX);
  for (unsigned i = 0; i < 5; ++i)
    solidRect(box->getLvObj(), (coord_t)(ruleX0 + i * ruleSpan / 4), (coord_t)(ruleY + 1), 1, 5,
              COLOR_THEME_PRIMARY3_INDEX);
  for (unsigned i = 0; i < 4; ++i)
    solidRect(box->getLvObj(), (coord_t)(ruleX0 + (2 * i + 1) * ruleSpan / 8), (coord_t)(ruleY + 1), 1, 3,
              COLOR_THEME_SECONDARY2_INDEX);
  const char* ruleText[] = {"-100", "-50", "0", "+50", "+100"};
  for (unsigned i = 0; i < 5; ++i)
    caption(box, {(coord_t)(ruleX0 + i * ruleSpan / 4 - 13), (coord_t)(ruleY + 7), 26, 12},
            ruleText[i], CENTERED);
  wedge = maskCanvas(box->getLvObj(), wedgeDownMask, 9, 5,
                     (coord_t)(ruleX0 + 60), wedgeY, accent);

  Nb4Ui::passThrough(lvobj);
  checkEvents();
}

void Nb4Dial::checkEvents()
{
  Window::checkEvents();
  if (deleted()) return;

  const int32_t raw = -(int32_t)calibratedAnalogs[ADC_MAIN_ST];

  const int deg = limit<int>(-120, divRoundClosest(raw * 120, RESX), 120);
  const int16_t at = (int16_t)((270 + deg + 360) % 360);
  const int16_t wantStart = deg >= 0 ? 270 : at;
  const int16_t wantEnd = deg >= 0 ? at : 270;
  if (wantStart != arcStart) { arcStart = wantStart; lv_arc_set_start_angle(arc, (uint16_t)wantStart); }
  if (wantEnd != arcEnd) { arcEnd = wantEnd; lv_arc_set_end_angle(arc, (uint16_t)wantEnd); }

  // 2) Needle: draw through the endpoint pixel, as in widgets/outputs.cpp.
  lv_point_t inner, tip;
  polar(NEEDLE_OX, NEEDLE_OY, deg, NEEDLE_HUB, inner);
  polar(NEEDLE_OX, NEEDLE_OY, deg, NEEDLE_LEN, tip);
  if (tip.x != tipX || tip.y != tipY) {
    tipX = tip.x; tipY = tip.y;
    needlePts[0] = inner; needlePts[1] = tip;
    lv_line_set_points(needle, needlePts, 2);
  }

  const int16_t percent = (int16_t)limit<int>(-100, divRoundClosest(raw * 100, RESX), 100);
  if (percent != lastPct) {
    lastPct = percent;
    char text[8];
    snprintf(text, sizeof(text), "%+d", (int)percent);
    digits->setText(text);
    centerGaugeValue(digits, pct, text, digitsCx, lastDw);
  }

  // 4) An absent trim is not the same as zero.
  const auto rawTrim = getRawTrimValue(0, ADC_MAIN_ST);
  const bool ok = rawTrim.mode != TRIM_MODE_NONE && rawTrim.mode != TRIM_MODE_3POS;
  const int32_t value = ok ? getTrimValue(0, ADC_MAIN_ST) : 0;
  if (value != lastTrim || ok != lastTrimOk) {
    lastTrim = value; lastTrimOk = ok;
    char text[12];
    if (ok) snprintf(text, sizeof(text), "%+d", (int)value); else strcpy(text, "--");
    chipVal->setText(text);
    if (ok && wedgeHidden) { lv_obj_clear_flag(wedge, LV_OBJ_FLAG_HIDDEN); wedgeHidden = false; }
    if (!ok && !wedgeHidden) { lv_obj_add_flag(wedge, LV_OBJ_FLAG_HIDDEN); wedgeHidden = true; }
    if (ok) {
      const int percentTrim = limit<int>(-100, divRoundClosest(value * 100, TRIM_MAX), 100);
      const coord_t wx = (coord_t)(ruleX0 + divRoundClosest((percentTrim + 100) * ruleSpan, 200) - 4);
      if (wx != lastWedgeX) { lastWedgeX = wx; lv_obj_set_x(wedge, wx); }
    }
  }
}

// ---------------------------------------------------------------------------
// Nb4Column
// ---------------------------------------------------------------------------

Nb4Column::Nb4Column(Window* parent, rect_t r, bool landscape) : Window(parent, r)
{
  setWindowFlag(OPAQUE);
  Nb4Ui::card(lvobj);
  gasInk = Nb4Ui::throttleColor();
  brakeInk = Nb4Ui::brakeColor();

  axisY = landscape ? 120 : 140;
  semi = landscape ? 70 : 88;
  fillX = landscape ? 53 : 50;
  fillW = landscape ? 14 : 12;
  markerX = landscape ? 49 : 47;
  markerW = landscape ? 22 : 20;
  triX = 1;
  const coord_t trackX = landscape ? 51 : 48;
  const coord_t trackW = landscape ? 18 : 16;
  const coord_t rightX = landscape ? 75 : 64;
  const coord_t rightW = width() - rightX - 4;
  digitsCx = rightX + rightW / 2;
  digitsTop = axisY - 14;

  auto t1 = nb4Label(this, {8, 6, 34, 20}, nb4Text("GAS", "THR"), FONT(BOLD));
  lv_obj_set_style_text_color(t1->getLvObj(), gasInk, 0);
  nb4Label(this, {44, 6, 62, 20}, nb4Text("/ FRENO", "/ BRAKE"), FONT(BOLD));
  chipVal = trimChip(this, {(coord_t)(width() - 72), landscape ? (coord_t)5 : (coord_t)28,
                           68, 22}, gasInk);

  const coord_t trimTop = axisY - semi - 24;
  auto trimWell = new Window(this, {4, trimTop, 40, (coord_t)(2 * semi + 34)});
  Nb4Ui::well(trimWell->getLvObj());
  trimAxisY = axisY - trimTop - 1;  // Subtract the track border
  caption(trimWell, {1, 4, 36, 12}, "TRIM", CENTERED);
  solidRect(trimWell->getLvObj(), 32, (coord_t)(trimAxisY - semi), 1,
            (coord_t)(2 * semi), COLOR_THEME_SECONDARY2_INDEX);
  const char* scale[] = {"+100", "+50", "0", "-50", "-100"};
  const coord_t scaleY[] = {
    (coord_t)(trimAxisY - semi),
    (coord_t)(trimAxisY - semi / 2),
    trimAxisY,
    (coord_t)(trimAxisY + semi / 2),
    (coord_t)(trimAxisY + semi),
  };
  for (unsigned i = 0; i < 5; ++i) {
    const coord_t y = scaleY[i];
    solidRect(trimWell->getLvObj(), 32, y, 5, 1, COLOR_THEME_PRIMARY3_INDEX);
    caption(trimWell, {8, (coord_t)(y - 6), 23, 12}, scale[i], RIGHT);
  }
  for (unsigned i = 0; i < 4; ++i) {
    const coord_t y = (coord_t)((scaleY[i] + scaleY[i + 1]) / 2);
    solidRect(trimWell->getLvObj(), 32, y, 3, 1, COLOR_THEME_SECONDARY2_INDEX);
  }

  auto trackBox = solidRect(lvobj, trackX, (coord_t)(axisY - semi), trackW,
                            (coord_t)(2 * semi), COLOR_THEME_SECONDARY3_INDEX,
                            (coord_t)(trackW / 2));
  lv_obj_set_style_border_width(trackBox, 1, 0);
  etx_border_color(trackBox, COLOR_THEME_SECONDARY2_INDEX);

  gas = solidRect(lvobj, fillX, axisY, fillW, 0, COLOR_THEME_PRIMARY1_INDEX,
                  (coord_t)(fillW / 2));
  lv_obj_set_style_bg_color(gas, lv_color_lighten(gasInk, 60), 0);
  lv_obj_set_style_bg_grad_color(gas, gasInk, 0);
  lv_obj_set_style_bg_grad_dir(gas, LV_GRAD_DIR_VER, 0);
  brake = solidRect(lvobj, fillX, axisY, fillW, 0, COLOR_THEME_PRIMARY1_INDEX,
                    (coord_t)(fillW / 2));
  lv_obj_set_style_bg_color(brake, brakeInk, 0);
  marker = solidRect(lvobj, markerX, (coord_t)(axisY - 1), markerW, 2,
                     COLOR_THEME_PRIMARY1_INDEX);
  tri = maskCanvas(trimWell->getLvObj(), wedgeRightMask, 5, 9,
                   triX, (coord_t)(trimAxisY - 4), gasInk);

  auto gasLabel = nb4Label(this, {rightX, landscape ? (coord_t)38 : (coord_t)56, rightW, 20},
                           nb4Text("GAS", "THR"), FONT(BOLD) | CENTERED);
  lv_obj_set_style_text_color(gasLabel->getLvObj(), gasInk, 0);
  auto brakeLabel = nb4Label(this, {rightX, landscape ? (coord_t)158 : (coord_t)192, rightW, 20},
                             nb4Text("FRENO", "BRAKE"), FONT(BOLD) | CENTERED);
  lv_obj_set_style_text_color(brakeLabel->getLvObj(), brakeInk, 0);
  if (landscape) {
    caption(this, {rightX, 60, rightW, 12}, nb4Text("ADELANTE", "FORWARD"), CENTERED);
    caption(this, {rightX, 180, rightW, 12}, nb4Text("MARCHA ATRÁS", "REVERSE"), CENTERED);
  }

  digits = nb4Label(this, {0, digitsTop, (coord_t)(digitsCx + 20), 29}, "+0", FONT(L) | RIGHT);
  gaugeFont(digits, &lv_font_nb4_gauge_22, 29);
  lv_obj_set_style_text_color(digits->getLvObj(), gasInk, 0);
  pct = nb4Label(this, {(coord_t)(digitsCx + 20), (coord_t)(digitsTop + 7), 20, 21}, "%", FONT(STD));
  gaugeFont(pct, &lv_font_nb4_percent_15, 21);
  lv_obj_set_style_text_color(pct->getLvObj(), gasInk, 0);

  Nb4Ui::passThrough(lvobj);
  checkEvents();
}

void Nb4Column::checkEvents()
{
  Window::checkEvents();
  if (deleted()) return;

  const int32_t raw = calibratedAnalogs[ADC_MAIN_TH];

  const int16_t px = (int16_t)limit<int>(-semi,
    divRoundClosest(raw * semi, RESX), semi);
  if (px != lastPx) {
    lastPx = px;
    if (px > 0) {
      lv_obj_set_y(gas, (coord_t)(axisY - px));
      lv_obj_set_height(gas, px);
      lv_obj_set_height(brake, 0);
    } else if (px < 0) {
      lv_obj_set_height(gas, 0);
      lv_obj_set_y(brake, axisY);
      lv_obj_set_height(brake, (coord_t)(-px));
    } else {
      lv_obj_set_height(gas, 0);
      lv_obj_set_height(brake, 0);
    }
    lv_obj_set_y(marker, (coord_t)(axisY - px - 1));
  }

  const int16_t percent = (int16_t)limit<int>(-100, divRoundClosest(raw * 100, RESX), 100);
  if (percent != lastPct) {
    lastPct = percent;
    char text[8];
    snprintf(text, sizeof(text), "%+d", (int)percent);
    digits->setText(text);
    centerGaugeValue(digits, pct, text, digitsCx, lastDw);
    const int8_t sign = percent < 0 ? -1 : 1;
    if (sign != lastSign) {
      lastSign = sign;
      const auto ink = sign < 0 ? brakeInk : gasInk;
      lv_obj_set_style_text_color(digits->getLvObj(), ink, 0);
      lv_obj_set_style_text_color(pct->getLvObj(), ink, 0);
    }
  }

  const auto rawTrim = getRawTrimValue(0, ADC_MAIN_TH);
  const bool ok = rawTrim.mode != TRIM_MODE_NONE && rawTrim.mode != TRIM_MODE_3POS;
  const int32_t value = ok ? getTrimValue(0, ADC_MAIN_TH) : 0;
  if (value != lastTrim || ok != lastTrimOk) {
    lastTrim = value; lastTrimOk = ok;
    char text[12];
    if (ok) snprintf(text, sizeof(text), "%+d", (int)value); else strcpy(text, "--");
    chipVal->setText(text);
    if (ok && triHidden) { lv_obj_clear_flag(tri, LV_OBJ_FLAG_HIDDEN); triHidden = false; }
    if (!ok && !triHidden) { lv_obj_add_flag(tri, LV_OBJ_FLAG_HIDDEN); triHidden = true; }
    if (ok) {
      const int percentTrim = limit<int>(-100, divRoundClosest(value * 100, TRIM_MAX), 100);
      const coord_t ty = (coord_t)(trimAxisY -
        divRoundClosest(percentTrim * semi, 100) - 4);
      if (ty != lastTriY) { lastTriY = ty; lv_obj_set_y(tri, ty); }
    }
  }
}

// ---------------------------------------------------------------------------
// Nb4Telltale
// ---------------------------------------------------------------------------

Nb4Telltale::Nb4Telltale(Window* parent, rect_t r, uint8_t kind, bool boxed) :
  Window(parent, r), kind(kind)
{
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);
  const coord_t pad = boxed ? 8 : 0;
  if (boxed) Nb4Ui::card(lvobj);
  else lv_obj_set_style_bg_opa(lvobj, LV_OPA_TRANSP, 0);

  const coord_t iconSlot = boxed ? 26 : 23;
  const coord_t contentX = (coord_t)(pad + iconSlot);
  const coord_t contentW = (coord_t)(width() - 2 * pad - iconSlot);

  if (kind != Link) {
    constexpr coord_t bodyW = 12, bodyH = 22;
    const coord_t bodyX = (coord_t)(pad + (iconSlot - bodyW) / 2);
    constexpr coord_t bodyY = 18;
    batteryIcon = lv_obj_create(lvobj);
    lv_obj_clear_flag(batteryIcon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(batteryIcon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(batteryIcon, bodyX, (coord_t)(bodyY - 3));
    lv_obj_set_size(batteryIcon, bodyW, (coord_t)(bodyH + 3));
    lv_obj_set_style_bg_opa(batteryIcon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(batteryIcon, 0, 0);
    lv_obj_set_style_pad_all(batteryIcon, 0, 0);
    lv_obj_add_event_cb(batteryIcon, drawBatteryIcon, LV_EVENT_DRAW_MAIN, this);
  }
  const char* names[] = {"RF", "TX", "RX"};
  const bool transmitter = kind == Transmitter;
  const coord_t labelX = transmitter && !boxed ? 0 : contentX;
  label = caption(this, {labelX, 6, 20, 12},
                  names[kind <= Receiver ? kind : 0]);
  if (transmitter) {
    const coord_t pillX = boxed ? (coord_t)(contentX + 15) : 14;
    const coord_t pillW = (coord_t)(width() - pillX - (boxed ? 4 : 0));
    auto pillBox = new Window(this, {pillX, 2, pillW, 15});
    Nb4Ui::chip(pillBox->getLvObj());
    lv_obj_set_style_radius(pillBox->getLvObj(), 7, 0);
    etx_border_color(pillBox->getLvObj(), COLOR_THEME_EDIT_INDEX);
    auto charging = caption(pillBox, {2, 1, (coord_t)(pillW - 4), 12},
                            "Charging", CENTERED, COLOR_THEME_EDIT_INDEX);
    lv_obj_set_style_text_letter_space(charging->getLvObj(), 0, LV_PART_MAIN);
    chargePill = pillBox->getLvObj();
    lv_obj_add_flag(chargePill, LV_OBJ_FLAG_HIDDEN);
  }
  value = nb4Label(this, {contentX, 15, contentW, 24}, "--", FONT(L));

  if (boxed && height() >= 56) {
    const coord_t segW = 6, segGap = 2, segY = (coord_t)(height() - 12);
    const coord_t total = (coord_t)(10 * segW + 9 * segGap);
    const coord_t x0 = (coord_t)((width() - total) / 2);
    segmentStrip = lv_obj_create(lvobj);
    lv_obj_clear_flag(segmentStrip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(segmentStrip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(segmentStrip, x0, segY);
    lv_obj_set_size(segmentStrip, total, 6);
    lv_obj_set_style_bg_opa(segmentStrip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(segmentStrip, 0, 0);
    lv_obj_set_style_pad_all(segmentStrip, 0, 0);
    lv_obj_add_event_cb(segmentStrip, drawSegmentStrip, LV_EVENT_DRAW_MAIN, this);
  }
  if (kind != Link) return;
  const coord_t barX = (coord_t)(pad + (iconSlot - 22) / 2);
  constexpr coord_t baseline = 40;
  for (unsigned i = 0; i < 4; ++i) {
    const coord_t h = (coord_t)(6 + 5 * i);
    bars[i] = solidRect(lvobj, (coord_t)(barX + 6 * i), (coord_t)(baseline - h), 4, h,
                        COLOR_THEME_SECONDARY2_INDEX, 1);
  }
  barInk = COLOR_THEME_SECONDARY2_INDEX;
}

void Nb4Telltale::drawBatteryIcon(lv_event_t* event)
{
  auto self = static_cast<Nb4Telltale*>(lv_event_get_user_data(event));
  auto context = lv_event_get_draw_ctx(event);
  lv_area_t iconArea;
  lv_obj_get_coords(self->batteryIcon, &iconArea);

  lv_area_t body = iconArea;
  body.y1 += 3;
  const auto ink = Nb4Ui::color(self->iconInk);

  if (self->battLevel > 0 && self->battLevel <= 4) {
    constexpr coord_t inset = 3;
    const coord_t available = (coord_t)(lv_area_get_height(&body) - 2 * inset);
    const coord_t fillH = (coord_t)((available * self->battLevel + 3) / 4);
    lv_area_t fill = {(coord_t)(body.x1 + inset),
                      (coord_t)(body.y2 - inset - fillH + 1),
                      (coord_t)(body.x2 - inset),
                      (coord_t)(body.y2 - inset)};
    lv_draw_rect_dsc_t fillStyle;
    lv_draw_rect_dsc_init(&fillStyle);
    fillStyle.bg_color = ink;
    fillStyle.bg_opa = LV_OPA_COVER;
    fillStyle.radius = 1;
    lv_draw_rect(context, &fillStyle, &fill);
  }

  lv_draw_rect_dsc_t bodyStyle;
  lv_draw_rect_dsc_init(&bodyStyle);
  bodyStyle.bg_opa = LV_OPA_TRANSP;
  bodyStyle.border_color = ink;
  bodyStyle.border_opa = LV_OPA_COVER;
  bodyStyle.border_width = 2;
  bodyStyle.radius = 2;
  lv_draw_rect(context, &bodyStyle, &body);

  lv_area_t terminal = {(coord_t)(iconArea.x1 + 3), iconArea.y1,
                        (coord_t)(iconArea.x2 - 3), (coord_t)(iconArea.y1 + 3)};
  lv_draw_rect_dsc_t terminalStyle;
  lv_draw_rect_dsc_init(&terminalStyle);
  terminalStyle.bg_color = ink;
  terminalStyle.bg_opa = LV_OPA_COVER;
  terminalStyle.radius = 1;
  lv_draw_rect(context, &terminalStyle, &terminal);
}

void Nb4Telltale::drawSegmentStrip(lv_event_t* event)
{
  auto self = static_cast<Nb4Telltale*>(lv_event_get_user_data(event));
  auto context = lv_event_get_draw_ctx(event);
  lv_area_t strip;
  lv_obj_get_coords(self->segmentStrip, &strip);

  lv_draw_rect_dsc_t style;
  lv_draw_rect_dsc_init(&style);
  style.bg_opa = LV_OPA_COVER;
  style.radius = 2;
  for (unsigned i = 0; i < 10; ++i) {
    style.bg_color = Nb4Ui::color(i < self->segCount
                                   ? self->segInk
                                   : COLOR_THEME_SECONDARY2_INDEX);
    const coord_t x = (coord_t)(strip.x1 + i * 8);
    lv_area_t segment = {x, strip.y1, (coord_t)(x + 5), strip.y2};
    lv_draw_rect(context, &style, &segment);
  }
}

void Nb4Telltale::setBatteryIcon(uint8_t level, LcdColorIndex tint)
{
  if (!batteryIcon || (level == battLevel && tint == iconInk)) return;
  battLevel = level;
  iconInk = tint;
  lv_obj_invalidate(batteryIcon);
}

void Nb4Telltale::setSegments(uint8_t count, LcdColorIndex tint)
{
  if (!segmentStrip || (count == segCount && tint == segInk)) return;
  segCount = count; segInk = tint;
  lv_obj_invalidate(segmentStrip);
}

void Nb4Telltale::refresh(const Nb4CarState& s)
{
  const Nb4Reading r = kind == Link ? s.link : kind == Transmitter ? s.transmitter : s.receiver;
  fittedNumber(value, nb4FormatReading(r), FONT(L));
  const auto validity = r.validity;
  const LcdColorIndex ink = validity == Nb4Validity::Alarm ? COLOR_THEME_WARNING_INDEX :
    validity == Nb4Validity::Valid ? COLOR_THEME_PRIMARY1_INDEX : COLOR_THEME_PRIMARY3_INDEX;
  Nb4Ui::inkIfChanged(value, valueInk, ink);
  Nb4Ui::inkIfChanged(label, labelInk, validity == Nb4Validity::Stale ?
    COLOR_THEME_WARNING_INDEX : COLOR_THEME_PRIMARY3_INDEX);

  if (kind == Transmitter) {

    const uint8_t level = g_vbat100mV > 0 ? min<uint8_t>(4, GET_TXBATT_BARS(4)) : 0;

    const uint8_t charge = s.chargeSource;
    if (charge != chargeShown) {
      chargeShown = charge;
      if (charge) lv_obj_clear_flag(chargePill, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(chargePill, LV_OBJ_FLAG_HIDDEN);
    }
    const LcdColorIndex tint =
      charge ? COLOR_THEME_EDIT_INDEX :
      validity == Nb4Validity::Absent ? COLOR_THEME_PRIMARY3_INDEX :
      level <= 1 ? COLOR_THEME_WARNING_INDEX :
      level == 2 ? COLOR_THEME_FOCUS_INDEX : COLOR_THEME_EDIT_INDEX;
    setBatteryIcon(level, tint);

    setSegments(g_vbat100mV > 0
                  ? (uint8_t)((nb4BatteryPercent((uint16_t)(g_vbat100mV * 100)) + 5) / 10)
                  : 0, tint);
    return;
  }

  if (kind == Receiver) {

    const uint8_t level = validity == Nb4Validity::Absent ? 0
                        : validity == Nb4Validity::Alarm ? 1 : 4;
    const LcdColorIndex tint =
      validity == Nb4Validity::Alarm ? COLOR_THEME_WARNING_INDEX :
      validity == Nb4Validity::Valid ? COLOR_THEME_EDIT_INDEX
                                     : COLOR_THEME_PRIMARY3_INDEX;
    setBatteryIcon(level, tint);

    setSegments(validity == Nb4Validity::Absent ? 0
              : validity == Nb4Validity::Alarm ? 2 : 10, tint);
    return;
  }
  const uint8_t count = validity == Nb4Validity::Absent ? 0 :
    (uint8_t)limit<int>(0, ((int)r.value + 24) / 25, 4);
  const LcdColorIndex lit = validity == Nb4Validity::Alarm ?
    COLOR_THEME_WARNING_INDEX : COLOR_THEME_EDIT_INDEX;

  if (count != barCount || lit != barInk) {
    barCount = count; barInk = lit;
    for (unsigned i = 0; i < 4; ++i)
      etx_bg_color(bars[i], i < count ? lit : COLOR_THEME_SECONDARY2_INDEX);
  }
  setSegments(validity == Nb4Validity::Absent ? 0 :
              (uint8_t)limit<int>(0, ((int)r.value + 9) / 10, 10), lit);
}

// ---------------------------------------------------------------------------
// Nb4Chrono
// ---------------------------------------------------------------------------

Nb4Chrono::Nb4Chrono(Window* parent, rect_t r, const Nb4CarState& state, bool landscape) :
  Window(parent, r), raceMode(state.homeShowsRace)
{
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);
  const unsigned index = state.homeTimerIndex < MAX_TIMERS ? state.homeTimerIndex : 0;
  char heading[LEN_TIMER_NAME + 12];
  if (raceMode) {
    strAppend(heading, nb4Text("VUELTA", "LAP"), sizeof(heading) - 1);
  } else if (state.homeTimerIndex < MAX_TIMERS && g_model.timers[index].name[0]) {
    snprintf(heading, sizeof(heading), "%.*s", (int)LEN_TIMER_NAME, g_model.timers[index].name);
  } else if (state.homeTimerVisible) {
    snprintf(heading, sizeof(heading), "%s %u", nb4Text("CRONO", "TIMER"), index + 1);
  } else {
    strAppend(heading, nb4Text("CRONO", "TIMER"), sizeof(heading) - 1);
  }
  const char* subtitle = raceMode ? nb4Text("VUELTA EN CURSO", "CURRENT LAP")
                       : state.homeTimerCountdown ? nb4Text("CUENTA ATRÁS", "COUNTDOWN")
                                                  : nb4Text("TRANSCURRIDO", "ELAPSED");
  if (landscape) {
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_TRANSP, 0);
    nb4Label(this, {6, 6, 70, 20}, heading, FONT(BOLD));
    stateLabel = caption(this, {6, 30, 70, 12}, stateName(0), 0, COLOR_THEME_EDIT_INDEX);
    nb4Hairline(this, {80, 10, 1, 36});
    timeLabel = nb4Label(this, {86, 8, 108, 40}, "--", FONT(XL) | CENTERED);
    return;
  }
  Nb4Ui::card(lvobj);
  auto ring = solidRect(lvobj, 8, 15, 34, 34, COLOR_THEME_PRIMARY2_INDEX, 17);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  etx_border_color(ring, COLOR_THEME_EDIT_INDEX);
  new StaticIcon(this, 10, 17, ICON_STATS_TIMERS, COLOR_THEME_EDIT_INDEX);
  nb4Label(this, {48, 12, 94, 20}, heading, FONT(BOLD));
  caption(this, {48, 34, 94, 12}, subtitle);
  nb4Hairline(this, {146, 12, 1, 40});
  timeLabel = nb4Label(this, {152, 4, (coord_t)(width() - 160), 40}, "--", FONT(XL) | CENTERED);

  auto pillBox = new Window(this, {(coord_t)(152 + (width() - 160 - 72) / 2), 46, 72, 18});
  Nb4Ui::chip(pillBox->getLvObj());
  lv_obj_set_style_radius(pillBox->getLvObj(), 8, 0);
  etx_border_color(pillBox->getLvObj(), COLOR_THEME_EDIT_INDEX);
  pill = pillBox->getLvObj();
  stateLabel = caption(pillBox, {2, 2, 66, 12}, stateName(0), CENTERED, COLOR_THEME_EDIT_INDEX);
}

void Nb4Chrono::refresh(const Nb4CarState& state)
{
  uint8_t index;
  bool available = true;
  int32_t raw;
  bool alarm = false;
  if (raceMode) {
    const auto phase = state.racePhase;
    index = phase == Nb4RacePhase::Running ? (nb4RaceIsPaused() ? 4 : 1) : phase == Nb4RacePhase::Finished ? 2 : 0;
    available = state.currentLap.validity != Nb4Validity::Absent;
    raw = state.currentLap.value;
  } else {
    available = state.homeTimer.validity != Nb4Validity::Absent;
    alarm = state.homeTimer.validity == Nb4Validity::Alarm;
    index = !available ? 3 : state.homeTimerIndex == 0 && nb4RaceIsPaused() ? 4 : state.homeTimerState == TMR_RUNNING ? 1 :
            state.homeTimerState == TMR_STOPPED ? 2 : 0;
    raw = state.homeTimer.value;
  }

  const int32_t guard = available ? raw : INT32_MIN + 1;
  if (guard != lastTime) {
    lastTime = guard;
    fittedNumber(timeLabel, raceMode ? Nb4Ui::timeText((uint32_t)raw, available)
                                    : Nb4Ui::timerText(raw, available), FONT(XL));
  }
  Nb4Ui::inkIfChanged(timeLabel, timeInk,
    alarm ? COLOR_THEME_WARNING_INDEX : COLOR_THEME_PRIMARY1_INDEX);
  if (index != lastState) {
    lastState = index;
    stateLabel->setText(stateName(index));
    const auto ink = stateColor(index);
    Nb4Ui::inkIfChanged(stateLabel, stateInk, ink);
    if (pill && ink != pillInk) { pillInk = ink; etx_border_color(pill, ink); }
  }
}

// ---------------------------------------------------------------------------
// Nb4Stats
// ---------------------------------------------------------------------------

Nb4Stats::Nb4Stats(Window* parent, rect_t r, bool landscape) : Window(parent, r)
{
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);
  if (landscape) lv_obj_set_style_bg_opa(lvobj, LV_OPA_TRANSP, 0);
  else Nb4Ui::card(lvobj);
  const coord_t inset = landscape ? 2 : 4;
  const coord_t usable = width() - 2 * inset - (landscape ? 0 : 2);
  const coord_t labelY = landscape ? 10 : 8;
  const char* names[] = {nb4Text("MEJOR", "BEST"), nb4Text("ÚLTIMA", "LAST"),
                         nb4Text("VUELTAS", "LAPS"), nb4Text("SESIÓN", "SESSION")};
  for (unsigned i = 0; i < 4; ++i) {
    const coord_t x = inset + usable * i / 4;
    const coord_t colW = usable * (i + 1) / 4 - usable * i / 4 - 2;
    caption(this, {x, labelY, colW, 12}, names[i], CENTERED);
    values[i] = nb4Label(this, {x, (coord_t)(labelY + 16), colW, 20}, "--:--.--",
                         FONT(BOLD) | CENTERED,
                         i == 0 ? COLOR_THEME_EDIT_INDEX : COLOR_THEME_PRIMARY1_INDEX);
    if (i) nb4Hairline(this, {(coord_t)(x - 1), labelY, 1, 28});
  }
}

void Nb4Stats::refresh(const Nb4CarState& state)
{
  const uint32_t source[4] = {
    state.laps ? (uint32_t)state.bestLap.value : 0xffffffffu,
    state.laps ? (uint32_t)state.lastLap.value : 0xffffffffu,
    (uint32_t)state.laps | ((uint32_t)g_model.nb4Racing.lapCount << 8),
    state.raceElapsed.validity != Nb4Validity::Absent ? (uint32_t)state.raceElapsed.value
                                                      : 0xffffffffu,
  };
  char text[24];
  for (unsigned i = 0; i < 4; ++i) {
    if (source[i] == last[i]) continue;
    last[i] = source[i];
    if (i == 2) {
      const unsigned target = g_model.nb4Racing.lapCount;
      if (target) snprintf(text, sizeof(text), "%u/%u", (unsigned)state.laps, target);
      else snprintf(text, sizeof(text), "%u", (unsigned)state.laps);
    } else if (source[i] == 0xffffffffu) {
      strcpy(text, "--:--.--");
    } else if (i == 3) {
      strAppend(text, Nb4Ui::timeText(source[i]).c_str(), sizeof(text) - 1);
    } else {
      nb4RacingFormatTime(text, source[i]);
    }
    fittedNumber(values[i], text, FONT(BOLD));
  }
}

// ---------------------------------------------------------------------------
// Nb4RacePanel (RacePage creates it with a height of 120)
// ---------------------------------------------------------------------------

Nb4RacePanel::Nb4RacePanel(Window* parent, rect_t r, bool race) :
  Window(parent, r), raceMode(race)
{
  setWindowFlag(OPAQUE);
  Nb4Ui::panel(lvobj);
  title = nb4Label(this, {10, 5, width() - 114, 17}, raceMode ?
    (width() < 260 ? nb4Text("CARRERA", "RACE") : nb4Text("TIEMPO DE CARRERA", "RACE TIME")) :
    nb4Text("CRONÓMETRO", "TIMER"), FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
  phase = nb4Label(this, {width() - 106, 5, 96, 17}, "", FONT(XS) | RIGHT, COLOR_THEME_EDIT_INDEX);
  timer = nb4Label(this, {8, 23, raceMode ? width() - 83 : width() - 16, 52}, raceMode ? "00:00.0" : "00:00", FONT(LXL));
  if (!raceMode) {
    lv_obj_set_style_text_align(timer->getLvObj(), LV_TEXT_ALIGN_CENTER, 0);
    progress = lv_bar_create(lvobj);
    lv_obj_set_pos(progress, 10, 82); lv_obj_set_size(progress, width() - 20, 5);
    lv_obj_set_style_border_width(progress, 0, 0);
    etx_bg_color(progress, COLOR_THEME_SECONDARY2_INDEX);
    etx_bg_color(progress, COLOR_THEME_FOCUS_INDEX, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(progress, 0, 0);
    timerInfo = nb4Label(this, {10, 92, width() - 20, 20}, "", FONT(XS) | CENTERED, COLOR_THEME_PRIMARY3_INDEX);
    return;
  }
  nb4Label(this, {width() - 73, 25, 63, 16}, nb4Text("VUELTA", "LAP"), FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
  laps = nb4Label(this, {width() - 73, 43, 63, 32}, "--", FONT(L));
  progress = lv_bar_create(lvobj);
  lv_obj_set_pos(progress, 10, 74); lv_obj_set_size(progress, width() - 20, 4);
  lv_obj_set_style_border_width(progress, 0, 0);
  etx_bg_color(progress, COLOR_THEME_SECONDARY2_INDEX);
  etx_bg_color(progress, COLOR_THEME_FOCUS_INDEX, LV_PART_INDICATOR);
  lv_obj_set_style_anim_time(progress, 0, 0);
  const coord_t statsY = height() > 130 ? 102 : 79;
  const char* names[] = {nb4Text("ÚLTIMA", "LAST"), nb4Text("MEJOR", "BEST"), "+/-"};
  StaticText** fields[] = {&last, &best, &delta};
  for (unsigned i = 0; i < 3; ++i) {
    coord_t x = 10 + i * (width() - 20) / 3, w = (width() - 20) / 3 - 4;
    nb4Label(this, {x, statsY, w, 16}, i == 2 ? nb4Text("DIF. MEJOR", "VS BEST") : names[i], FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
    *fields[i] = nb4Label(this, {x, statsY + 16, w, 28}, "--", FONT(L), i == 1 ? COLOR_THEME_EDIT_INDEX : COLOR_THEME_PRIMARY1_INDEX);
  }
}

void Nb4RacePanel::refresh(const Nb4CarState& state)
{
  if (!raceMode) {
    const bool available = state.homeTimer.validity != Nb4Validity::Absent;
    timer->setText(Nb4Ui::timerText(state.homeTimer.value, available));
    Nb4Ui::inkIfChanged(timer, timerInk, state.homeTimer.validity == Nb4Validity::Alarm ?
      COLOR_THEME_WARNING_INDEX : COLOR_THEME_PRIMARY1_INDEX);
    char heading[48];
    const unsigned index = state.homeTimerIndex < 2 ? state.homeTimerIndex : 0;
    const auto& cfg = g_model.timers[index];
    if (cfg.name[0]) snprintf(heading, sizeof(heading), "%s", cfg.name);
    else snprintf(heading, sizeof(heading), "%s %u", state.homeTimerCountdown ?
      nb4Text("CUENTA ATRÁS", "COUNTDOWN") : nb4Text("CRONÓMETRO", "TIMER"), index + 1);
    title->setText(heading);
    phase->setText(!available ? nb4Text("Desactivado", "Disabled") :
      state.homeTimerIndex == 0 && nb4RaceIsPaused() ? nb4Text("En pausa", "Paused") :
      state.homeTimerState == TMR_RUNNING ? nb4Text("En marcha", "Running") :
      state.homeTimerState == TMR_STOPPED ? nb4Text("Finalizado", "Finished") :
      nb4Text("Preparado", "Ready"));
    unsigned percent = 0;
    if (state.homeTimerStart) {
      percent = limit<int64_t>(0, int64_t(state.homeTimer.value) * 100 /
        state.homeTimerStart, 100);
    }
    lv_bar_set_value(progress, percent, LV_ANIM_OFF);
    timerInfo->setText(state.homeTimerCountdown ? nb4Text("Tiempo restante", "Time remaining") :
      nb4Text("Tiempo transcurrido", "Elapsed time"));
    return;
  }
  timer->setText(Nb4Ui::timeText(state.raceElapsed.value, state.raceElapsed.validity != Nb4Validity::Absent));
  phase->setText(Nb4Ui::phaseText());
  char text[24];
  const unsigned count = nb4RacingLaps(), target = g_model.nb4Racing.lapCount;
  if (target) snprintf(text, sizeof(text), "%02u/%u", count, target);
  else snprintf(text, sizeof(text), "%02u", count);
  laps->setText(text);
  lv_bar_set_value(progress, target ? min<unsigned>(100, count * 100 / target) : 0, LV_ANIM_OFF);
  nb4RacingFormatTime(text, nb4RacingLastLap()); last->setText(count ? text : "--");
  nb4RacingFormatTime(text, nb4RacingBestLap()); best->setText(count ? text : "--");
  int32_t d = nb4RacingLapDelta();
  const uint32_t magnitude = d < 0 ? uint32_t(-int64_t(d)) : uint32_t(d);
  snprintf(text, sizeof(text), "%s%lu.%02lu", d < 0 ? "-" : "+", (unsigned long)(magnitude / 100), (unsigned long)(magnitude % 100));
  delta->setText(count >= 2 ? text : "--");
  Nb4Ui::inkIfChanged(delta, deltaInk, count < 2 || !d ? COLOR_THEME_PRIMARY3_INDEX : d < 0 ? COLOR_THEME_EDIT_INDEX : COLOR_THEME_WARNING_INDEX);
}
#endif
