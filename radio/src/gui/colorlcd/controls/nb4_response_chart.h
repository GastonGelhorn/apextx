/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <functional>

#include "window.h"

#if defined(RADIO_NB4_FAMILY)

class Nb4ResponseChart : public Window
{
 public:

  Nb4ResponseChart(Window* parent, const rect_t& rect,
                   std::function<int(int)> map, std::function<int()> position,
                   const char* leftZone, const char* middleZone,
                   const char* rightZone, lv_color_t leftColor,
                   lv_color_t rightColor);
  ~Nb4ResponseChart() override;

  void checkEvents() override;

  void setReadoutHandler(std::function<void(int, int)> handler)
  {
    readout = std::move(handler);
    reportReadout();
  }

  static coord_t heightFor(coord_t width);

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "Nb4ResponseChart"; }
#endif

 protected:
  std::function<int(int)> map;
  std::function<int()> positionFunc;
  std::function<void(int, int)> readout;

  lv_obj_t* curveLine = nullptr;      // Left half
  lv_obj_t* curveLineRight = nullptr;
  lv_color_t leftColor = {};
  lv_color_t rightColor = {};
  unsigned splitAt = 0;               // Neutral column
  lv_obj_t* dot = nullptr;
  lv_obj_t* crossV = nullptr;
  lv_obj_t* crossH = nullptr;
  lv_point_t crossPoints[4] = {};
  lv_point_t* curvePoints = nullptr;
  lv_point_t axisPoints[12] = {};
  unsigned curveCount = 0;

  lv_coord_t px = 0, py = 0, pw = 0, ph = 0;

  int32_t fingerprint = 0;
  int lastPosition = INT32_MIN;

  uint8_t stillFrames = 0;
  bool crossShown = false;

  void buildFrame();
  void setCrossShown(bool shown);
  void rebuildCurve();
  void updateDot();
  void reportReadout();
  int32_t sampleMap() const;
  lv_coord_t plotX(int input) const;
  lv_coord_t plotY(int output) const;
};

#endif  // RADIO_NB4_FAMILY
