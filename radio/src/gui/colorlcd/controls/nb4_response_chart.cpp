/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_response_chart.h"

#if defined(RADIO_NB4_FAMILY)

#include "edgetx.h"
#include "etx_lv_theme.h"
#include "libui/static.h"
#include "strhelpers.h"

static constexpr lv_coord_t CHART_LEFT = 34;
static constexpr lv_coord_t CHART_RIGHT = 6;
static constexpr lv_coord_t CHART_TOP = 8;
static constexpr lv_coord_t CHART_TICKS = 13;   // X-axis tick row
static constexpr lv_coord_t CHART_CAPTION = 22; // Zone label row

coord_t Nb4ResponseChart::heightFor(coord_t width)
{

  const coord_t plot = (coord_t)((width - CHART_LEFT - CHART_RIGHT) * 3 / 5);
  const coord_t labels = CHART_TOP + CHART_TICKS + CHART_CAPTION;
  const coord_t available = lv_disp_get_ver_res(nullptr) - EdgeTxStyles::MENU_HEADER_HEIGHT
      - 2 * EdgeTxStyles::UI_ELEMENT_HEIGHT - 3 * EdgeTxStyles::STD_FONT_HEIGHT
      - labels - PAD_LARGE * 2 - PAD_TINY;
  return min<coord_t>(plot, max<coord_t>(64, available)) + labels;
}

lv_coord_t Nb4ResponseChart::plotX(int input) const
{
  const int v = limit<int>(-RESX, input, RESX);
  return (lv_coord_t)(px + (int32_t)(v + RESX) * (pw - 1) / (2 * RESX));
}

lv_coord_t Nb4ResponseChart::plotY(int output) const
{
  const int v = limit<int>(-RESX, output, RESX);
  return (lv_coord_t)(py + ph - 1 - (int32_t)(v + RESX) * (ph - 1) / (2 * RESX));
}

Nb4ResponseChart::Nb4ResponseChart(Window* parent, const rect_t& rect,
                                   std::function<int(int)> map,
                                   std::function<int()> position,
                                   const char* leftZone, const char* middleZone,
                                   const char* rightZone, lv_color_t leftColor,
                                   lv_color_t rightColor) :
    Window(parent, rect), map(std::move(map)), positionFunc(std::move(position)),
    leftColor(leftColor), rightColor(rightColor)
{
  setWindowFlag(NO_FOCUS);

  px = CHART_LEFT;
  py = CHART_TOP;
  pw = rect.w - CHART_LEFT - CHART_RIGHT;
  ph = rect.h - CHART_TOP - CHART_TICKS - CHART_CAPTION;

  buildFrame();

  const int yTicks[] = {100, 50, 0, -50, -100};
  for (int t : yTicks) {
    char text[8];
    snprintf(text, sizeof(text), "%d", t);
    auto label = new StaticText(
        this, rect_t{0, (coord_t)(plotY(t * RESX / 100) - 7), CHART_LEFT - 4, 14},
        text, COLOR_THEME_PRIMARY3_INDEX, FONT(XXS) | RIGHT);
    (void)label;
  }

  /* X-axis labels below the chart. */
  const int xTicks[] = {-100, -50, 0, 50, 100};
  for (int t : xTicks) {
    char text[8];
    snprintf(text, sizeof(text), "%d", t);
    const coord_t labelX = limit<coord_t>(0, plotX(t * RESX / 100) - 18, rect.w - 36);
    new StaticText(this,
                   rect_t{labelX,
                          (coord_t)(py + ph + 1), 36, CHART_TICKS},
                   text, COLOR_THEME_PRIMARY3_INDEX, FONT(XXS) | CENTERED);
  }

  const coord_t zoneY = py + ph + CHART_TICKS;
  const coord_t third = pw / 3;
  new StaticText(this, rect_t{px, zoneY, third, CHART_CAPTION}, leftZone,
                 COLOR_THEME_PRIMARY1_INDEX, FONT(XS));
  new StaticText(this, rect_t{(coord_t)(px + third), zoneY, third, CHART_CAPTION},
                 middleZone, COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | CENTERED);
  new StaticText(this, rect_t{(coord_t)(px + 2 * third), zoneY, third, CHART_CAPTION},
                 rightZone, COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | RIGHT);

  rebuildCurve();
  fingerprint = sampleMap();

  if (positionFunc) {

    crossV = lv_line_create(lvobj);
    etx_obj_add_style(crossV, styles->graph_dashed, LV_PART_MAIN);
    lv_obj_set_style_line_color(crossV, lv_color_hex(0xFFD21E), LV_PART_MAIN);
    lv_obj_set_style_line_opa(crossV, LV_OPA_60, LV_PART_MAIN);
    crossH = lv_line_create(lvobj);
    etx_obj_add_style(crossH, styles->graph_dashed, LV_PART_MAIN);
    lv_obj_set_style_line_color(crossH, lv_color_hex(0xFFD21E), LV_PART_MAIN);
    lv_obj_set_style_line_opa(crossH, LV_OPA_60, LV_PART_MAIN);

    dot = lv_obj_create(lvobj);
    lv_obj_set_size(dot, 9, 9);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 2, LV_PART_MAIN);

    etx_bg_color(dot, COLOR_THEME_SECONDARY1_INDEX, LV_PART_MAIN);
    lv_obj_set_style_border_color(dot, lv_color_hex(0xFFD21E), LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(crossV, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(crossH, LV_OBJ_FLAG_HIDDEN);
    updateDot();
  }
}

Nb4ResponseChart::~Nb4ResponseChart()
{
  delete[] curvePoints;
}

void Nb4ResponseChart::buildFrame()
{

  for (int i = 1; i <= 3; i += 1) {
    auto v = lv_line_create(lvobj);
    etx_obj_add_style(v, styles->graph_dashed, LV_PART_MAIN);
    lv_obj_set_style_line_opa(v, LV_OPA_30, LV_PART_MAIN);
    axisPoints[(i - 1) * 2] = {(lv_coord_t)(px + pw * i / 4), py};
    axisPoints[(i - 1) * 2 + 1] = {(lv_coord_t)(px + pw * i / 4),
                                   (lv_coord_t)(py + ph - 1)};
    lv_line_set_points(v, &axisPoints[(i - 1) * 2], 2);

    auto h = lv_line_create(lvobj);
    etx_obj_add_style(h, styles->graph_dashed, LV_PART_MAIN);
    lv_obj_set_style_line_opa(h, LV_OPA_30, LV_PART_MAIN);
    axisPoints[6 + (i - 1) * 2] = {px, (lv_coord_t)(py + ph * i / 4)};
    axisPoints[6 + (i - 1) * 2 + 1] = {(lv_coord_t)(px + pw - 1),
                                       (lv_coord_t)(py + ph * i / 4)};
    lv_line_set_points(h, &axisPoints[6 + (i - 1) * 2], 2);
  }

  static lv_point_t zeroH[2], zeroV[2];
  zeroH[0] = {px, plotY(0)};
  zeroH[1] = {(lv_coord_t)(px + pw - 1), plotY(0)};
  auto h0 = lv_line_create(lvobj);
  etx_obj_add_style(h0, styles->graph_border, LV_PART_MAIN);
  etx_line_color(h0, COLOR_THEME_PRIMARY2_INDEX, LV_PART_MAIN);
  lv_line_set_points(h0, zeroH, 2);

  zeroV[0] = {plotX(0), py};
  zeroV[1] = {plotX(0), (lv_coord_t)(py + ph - 1)};
  auto v0 = lv_line_create(lvobj);
  etx_obj_add_style(v0, styles->graph_dashed, LV_PART_MAIN);
  etx_line_color(v0, COLOR_THEME_PRIMARY2_INDEX, LV_PART_MAIN);
  lv_line_set_points(v0, zeroV, 2);

  curveLine = lv_line_create(lvobj);
  lv_obj_set_style_line_width(curveLine, 3, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(curveLine, true, LV_PART_MAIN);
  lv_obj_set_style_line_color(curveLine, leftColor, LV_PART_MAIN);
  lv_obj_set_style_line_opa(curveLine, LV_OPA_COVER, LV_PART_MAIN);

  curveLineRight = lv_line_create(lvobj);
  lv_obj_set_style_line_width(curveLineRight, 3, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(curveLineRight, true, LV_PART_MAIN);
  lv_obj_set_style_line_color(curveLineRight, rightColor, LV_PART_MAIN);
  lv_obj_set_style_line_opa(curveLineRight, LV_OPA_COVER, LV_PART_MAIN);
}

void Nb4ResponseChart::rebuildCurve()
{
  if (!curveLine || pw <= 1) return;
  if (!curvePoints) {
    curveCount = (unsigned)pw;
    curvePoints = new lv_point_t[curveCount];
  }
  for (unsigned i = 0; i < curveCount; i += 1) {
    const int input =
        divRoundClosest((int32_t)((int)i - (pw - 1) / 2) * RESX, (pw - 1) / 2);
    curvePoints[i] = {(lv_coord_t)(px + i), plotY(map ? map(input) : 0)};
  }

  splitAt = curveCount / 2;
  lv_line_set_points(curveLine, curvePoints, splitAt + 1);
  lv_line_set_points(curveLineRight, &curvePoints[splitAt],
                     curveCount - splitAt);
}

void Nb4ResponseChart::setCrossShown(bool shown)
{
  if (shown == crossShown || !crossV || !crossH) return;
  crossShown = shown;
  if (shown) {
    lv_obj_clear_flag(crossV, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(crossH, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(crossV, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(crossH, LV_OBJ_FLAG_HIDDEN);
  }
}

void Nb4ResponseChart::updateDot()
{
  if (!dot || !positionFunc) return;
  const int input = positionFunc();
  const int output = map ? map(input) : 0;
  const lv_coord_t cx = plotX(input), cy = plotY(output);
  lv_obj_set_pos(dot, (lv_coord_t)(cx - 4), (lv_coord_t)(cy - 4));

  if (crossV && crossH) {
    crossPoints[0] = {cx, py};
    crossPoints[1] = {cx, (lv_coord_t)(py + ph - 1)};
    lv_line_set_points(crossV, &crossPoints[0], 2);
    crossPoints[2] = {px, cy};
    crossPoints[3] = {(lv_coord_t)(px + pw - 1), cy};
    lv_line_set_points(crossH, &crossPoints[2], 2);
  }
}

void Nb4ResponseChart::reportReadout()
{
  if (!readout || !positionFunc) return;
  const int input = positionFunc();
  const int output = map ? map(input) : 0;
  readout(divRoundClosest(input * 100, RESX), divRoundClosest(output * 100, RESX));
}

int32_t Nb4ResponseChart::sampleMap() const
{
  if (!map) return 0;
  int32_t sum = 0;
  for (int x : {-1024, -512, 0, 512, 1024}) sum = sum * 31 + map(x);
  return sum;
}

void Nb4ResponseChart::checkEvents()
{

  const int32_t now = sampleMap();
  if (now != fingerprint) {
    fingerprint = now;
    rebuildCurve();
    updateDot();
    reportReadout();
  }

  if (positionFunc) {
    const int pos = positionFunc();
    if (pos != lastPosition) {
      lastPosition = pos;
      stillFrames = 0;
      setCrossShown(false);
      updateDot();
      reportReadout();
    } else if (!crossShown && stillFrames < 255) {

      if (++stillFrames >= 8) {
        updateDot();
        setCrossShown(true);
      }
    }
  }

  Window::checkEvents();
}

#endif  // RADIO_NB4_FAMILY
