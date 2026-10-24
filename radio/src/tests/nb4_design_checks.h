/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

// Design-system rules checked on every NB4 view (see docs/nb4/DESIGN.md).
// nb4DesignViolations() walks the visible LVGL tree of one view and returns
// what breaks the rules; the route test runs it on every route, in both
// orientations and every language.

#include "gtests.h"
#include "nb4_ui.h"

#include <map>
#include <set>
#include <string>
#include <vector>

struct Nb4DesignViolation {
  std::string rule;    // overlap, bounds, clip, touch, icon, grid
  std::string detail;
};

namespace nb4design {

inline bool isLabel(lv_obj_t* o) { return lv_obj_check_type(o, &lv_label_class); }
inline bool isImage(lv_obj_t* o) { return lv_obj_has_class(o, &lv_img_class); }

inline bool hidden(lv_obj_t* o)
{
  return lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) ||
         lv_obj_get_style_opa(o, LV_PART_MAIN) <= LV_OPA_MIN ||
         lv_obj_get_width(o) <= 0 || lv_obj_get_height(o) <= 0;
}

inline bool interactive(lv_obj_t* o)
{
  if (!lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) return false;
  if (lv_obj_has_state(o, LV_STATE_DISABLED)) return false;
  return lv_obj_get_group(o) != nullptr || lv_obj_check_type(o, &lv_btn_class);
}

inline std::string describe(lv_obj_t* o)
{
  std::string text;
  if (isLabel(o)) {
    const char* t = lv_label_get_text(o);
    text = t ? t : "";
  } else {
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o) && text.empty(); ++i)
      if (isLabel(lv_obj_get_child(o, i))) {
        const char* t = lv_label_get_text(lv_obj_get_child(o, i));
        text = t ? t : "";
      }
  }
  for (auto& c : text) if (c == '\n' || c == '\t') c = ' ';
  if (text.size() > 24) text = text.substr(0, 24) + "...";
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  char where[64];
  snprintf(where, sizeof(where), " %s@%d,%d %dx%d",
           isLabel(o) ? "label" : isImage(o) ? "icon" :
           lv_obj_check_type(o, &lv_btn_class) ? "button" : "obj",
           a.x1, a.y1, lv_area_get_width(&a), lv_area_get_height(&a));
  return "'" + text + "'" + where;
}

// The text's own box inside a label, so a wide label box that only holds a
// short word is not an overlap.
inline lv_area_t inkBox(lv_obj_t* label)
{
  lv_area_t a;
  lv_obj_get_content_coords(label, &a);
  const char* text = lv_label_get_text(label);
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  lv_point_t size;
  lv_txt_get_size(&size, text ? text : "", font,
                  lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                  lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                  lv_area_get_width(&a), LV_TEXT_FLAG_NONE);
  const lv_coord_t w = LV_MIN(size.x, lv_area_get_width(&a));
  const lv_coord_t h = LV_MIN(size.y, lv_area_get_height(&a));
  switch (lv_obj_calculate_style_text_align(label, LV_PART_MAIN, text)) {
    case LV_TEXT_ALIGN_CENTER: a.x1 += (lv_area_get_width(&a) - w) / 2; break;
    case LV_TEXT_ALIGN_RIGHT: a.x1 = a.x2 - w + 1; break;
    default: break;
  }
  a.x2 = a.x1 + w - 1;
  a.y2 = a.y1 + h - 1;
  return a;
}

inline bool overlaps(const lv_area_t& a, const lv_area_t& b, lv_coord_t margin = 0)
{
  return a.x1 + margin <= b.x2 && b.x1 + margin <= a.x2 &&
         a.y1 + margin <= b.y2 && b.y1 + margin <= a.y2;
}

inline bool onGrid(lv_coord_t v) { return v >= 0 && v % 4 == 0; }

class Walker
{
 public:
  Walker(std::vector<Nb4DesignViolation>& out, lv_coord_t w, lv_coord_t h) :
      out(out), screen{0, 0, (lv_coord_t)(w - 1), (lv_coord_t)(h - 1)}
  {
  }

  void walk(lv_obj_t* o, const lv_area_t& clip, bool scrolled)
  {
    if (hidden(o)) return;
    lv_area_t box, seen;
    lv_obj_get_coords(o, &box);
    if (!_lv_area_intersect(&seen, &box, &clip)) return;

    lv_obj_t* parent = lv_obj_get_parent(o);
    if (parent && !scrolled) checkBounds(o, parent, box);
    if (isLabel(o)) checkText(o);
    if (interactive(o)) checkTouch(o, box);
    if (isImage(o)) checkIcon(o, box);
    if (lv_obj_has_flag(o, Nb4Ui::DesignSurface)) checkSurface(o);
    if (isLabel(o) || isImage(o) || interactive(o)) content.push_back({o, seen});

    // A control is one target; what it draws inside is its own business.
    if (interactive(o) && !lv_obj_has_flag(o, Nb4Ui::DesignSurface)) return;
    lv_area_t inner = seen;
    const bool scrolls = lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) &&
                         lv_obj_get_scroll_dir(o) != LV_DIR_NONE;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i)
      walk(lv_obj_get_child(o, i), inner, scrolled || scrolls);
  }

  void finish()
  {
    for (size_t i = 0; i < content.size(); ++i)
      for (size_t j = i + 1; j < content.size(); ++j) {
        lv_obj_t *a = content[i].first, *b = content[j].first;
        if (related(a, b)) continue;
        // An icon drawn on a badge, or a label on a control, is composed on
        // purpose; overlap is about two separate things fighting for room.
        const bool aImg = isImage(a), bImg = isImage(b);
        if (aImg && bImg) continue;
        // Only what is on screen: a scrolled-away line overlaps nothing.
        lv_area_t ra = area(a), rb = area(b);
        if (!_lv_area_intersect(&ra, &ra, &content[i].second) ||
            !_lv_area_intersect(&rb, &rb, &content[j].second))
          continue;
        if (overlaps(ra, rb, 1))
          add("overlap", describe(a) + " overlaps " + describe(b));
      }
  }

 private:
  std::vector<Nb4DesignViolation>& out;
  lv_area_t screen;
  std::vector<std::pair<lv_obj_t*, lv_area_t>> content;

  void add(const char* rule, const std::string& detail) { out.push_back({rule, detail}); }

  static bool related(lv_obj_t* a, lv_obj_t* b)
  {
    for (auto p = a; p; p = lv_obj_get_parent(p)) if (p == b) return true;
    for (auto p = b; p; p = lv_obj_get_parent(p)) if (p == a) return true;
    // Children of one control belong to that control.
    for (auto p = lv_obj_get_parent(a); p; p = lv_obj_get_parent(p))
      if (interactive(p))
        for (auto q = b; q; q = lv_obj_get_parent(q)) if (q == p) return true;
    return false;
  }

  static lv_area_t area(lv_obj_t* o)
  {
    if (isLabel(o)) return inkBox(o);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    return a;
  }

  void checkBounds(lv_obj_t* o, lv_obj_t* parent, const lv_area_t& box)
  {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_FLOATING)) return;
    lv_area_t limit;
    lv_obj_get_coords(parent, &limit);
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) return;
    const lv_coord_t over = LV_MAX(LV_MAX(limit.x1 - box.x1, box.x2 - limit.x2),
                                   LV_MAX(limit.y1 - box.y1, box.y2 - limit.y2));
    if (over > 0)
      add("bounds", describe(o) + " sticks out of its parent by " + std::to_string(over) + " px");
    if (box.x1 < screen.x1 || box.x2 > screen.x2 || box.y1 < screen.y1 || box.y2 > screen.y2)
      add("bounds", describe(o) + " leaves the screen");
  }

  void checkText(lv_obj_t* o)
  {
    const char* text = lv_label_get_text(o);
    if (!text || !*text) return;
    auto label = reinterpret_cast<lv_label_t*>(o);
    if (label->long_mode == LV_LABEL_LONG_SCROLL ||
        label->long_mode == LV_LABEL_LONG_SCROLL_CIRCULAR)
      return;
    if (label->long_mode == LV_LABEL_LONG_DOT && label->dot_end != 0xFFFFFFFFu) {
      add("clip", describe(o) + " is cut short with dots");
      return;
    }
    if (lv_obj_get_style_width(o, LV_PART_MAIN) == LV_SIZE_CONTENT &&
        lv_obj_get_style_height(o, LV_PART_MAIN) == LV_SIZE_CONTENT)
      return;
    lv_area_t content;
    lv_obj_get_content_coords(o, &content);
    const lv_font_t* font = lv_obj_get_style_text_font(o, LV_PART_MAIN);
    lv_point_t size;
    const bool wraps = label->long_mode == LV_LABEL_LONG_WRAP;
    lv_txt_get_size(&size, text, font,
                    lv_obj_get_style_text_letter_space(o, LV_PART_MAIN),
                    lv_obj_get_style_text_line_space(o, LV_PART_MAIN),
                    wraps ? lv_area_get_width(&content) : LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    // Glyph boxes carry a few pixels of empty line gap; allow those.
    const lv_coord_t slack = font->line_height / 5;
    if (size.y > lv_area_get_height(&content) + slack)
      add("clip", describe(o) + " needs " + std::to_string(size.y) + " px but has " +
                  std::to_string(lv_area_get_height(&content)));
    else if (!wraps && size.x > lv_area_get_width(&content) + 1)
      add("clip", describe(o) + " needs " + std::to_string(size.x) + " px wide but has " +
                  std::to_string(lv_area_get_width(&content)));
  }

  void checkTouch(lv_obj_t* o, const lv_area_t& box)
  {
    const lv_coord_t extra = 2 * (o->spec_attr ? o->spec_attr->ext_click_pad : 0);
    const lv_coord_t w = lv_area_get_width(&box) + extra;
    const lv_coord_t h = lv_area_get_height(&box) + extra;
    if (w < Nb4Ui::Touch || h < Nb4Ui::Touch)
      add("touch", describe(o) + " touch area " + std::to_string(w) + "x" +
                   std::to_string(h) + " is under " + std::to_string(Nb4Ui::Touch));
  }

  void checkIcon(lv_obj_t* o, const lv_area_t& box)
  {
    const lv_coord_t w = lv_area_get_width(&box), h = lv_area_get_height(&box);
    // Logos, QR codes and drawings are pictures, not icons.
    if (w > 48 || h > 48) return;
    lv_obj_t* parent = lv_obj_get_parent(o);
    // An icon on a header plate or a tile badge has to sit in its middle.
    lv_obj_t* slot = nullptr;
    if (parent && isImage(parent)) slot = parent;
    else if (parent && lv_obj_has_flag(o, Nb4Ui::IconInSlot))
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent) && !slot; ++i) {
        auto s = lv_obj_get_child(parent, i);
        if (s != o && lv_obj_has_flag(s, Nb4Ui::IconSlot)) slot = s;
      }
    // A plate that carries an icon is a slot, not an icon.
    bool plate = false;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i)
      if (isImage(lv_obj_get_child(o, i))) plate = true;
    if (!plate && (w > Nb4Ui::IconTile || h > Nb4Ui::IconTile))
      add("icon", describe(o) + " is larger than the " +
                  std::to_string(Nb4Ui::IconTile) + " px icon slot");
    if (!slot) return;
    lv_area_t s;
    lv_obj_get_coords(slot, &s);
    // Centre of the slot: of its ink for a shaped plate, of its box otherwise.
    double cx = (s.x1 + s.x2 + 1) / 2.0, cy = (s.y1 + s.y2 + 1) / 2.0;
    if (lv_obj_check_type(slot, &lv_canvas_class)) {
      const lv_img_dsc_t* img = lv_canvas_get_img(slot);
      if (img && img->header.cf == LV_IMG_CF_ALPHA_8BIT) {
        double sum = 0, sx = 0, sy = 0;
        for (uint32_t y = 0; y < img->header.h; ++y)
          for (uint32_t x = 0; x < img->header.w; ++x) {
            const double a = img->data[y * img->header.w + x];
            sum += a; sx += a * (x + 0.5); sy += a * (y + 0.5);
          }
        if (sum > 0) { cx = s.x1 + sx / sum; cy = s.y1 + sy / sum; }
      }
    }
    const double dx = (box.x1 + box.x2 + 1) / 2.0 - cx;
    const double dy = (box.y1 + box.y2 + 1) / 2.0 - cy;
    if (dx < -1.5 || dx > 1.5 || dy < -1.5 || dy > 1.5) {
      char text[48];
      snprintf(text, sizeof(text), " is off centre in its slot by %.1f,%.1f", dx, dy);
      add("icon", describe(o) + text);
    }
    if (box.x1 < s.x1 || box.x2 > s.x2 || box.y1 < s.y1 || box.y2 > s.y2)
      add("icon", describe(o) + " does not fit its slot");
  }

  // Inside a design-system surface, text and controls start on the spacing
  // grid and flex gaps use the spacing tokens.
  void checkSurface(lv_obj_t* o)
  {
    // Measured from inside the border, where the children's coordinates start.
    lv_area_t s;
    lv_obj_get_content_coords(o, &s);
    for (const auto part : {LV_STYLE_PAD_ROW, LV_STYLE_PAD_COLUMN}) {
      const auto v = lv_obj_get_style_prop(o, LV_PART_MAIN, part).num;
      if (lv_obj_get_style_layout(o, LV_PART_MAIN) && !onGrid(v))
        add("grid", describe(o) + " gap " + std::to_string(v) + " is not a spacing token");
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) {
      lv_obj_t* c = lv_obj_get_child(o, i);
      if (hidden(c) || !(isLabel(c) || interactive(c))) continue;
      if (lv_obj_has_flag(c, Nb4Ui::OffGrid)) continue;
      // Centred and right-aligned text floats; only left-aligned text and
      // controls line up with the card's edge.
      if (isLabel(c) && lv_obj_calculate_style_text_align(c, LV_PART_MAIN, lv_label_get_text(c)) !=
                            LV_TEXT_ALIGN_LEFT)
        continue;
      lv_area_t b = isLabel(c) ? inkBox(c) : lv_area_t{};
      if (!isLabel(c)) lv_obj_get_coords(c, &b);
      // Only what lines up with an edge; centred and value text float.
      const lv_coord_t left = b.x1 - s.x1, top = b.y1 - s.y1;
      if (left < Nb4Ui::SpaceL + 1 && !onGrid(left))
        add("grid", describe(c) + " starts " + std::to_string(left) + " px from its card edge");
      if (top < Nb4Ui::SpaceL + 1 && top >= 0 && !isLabel(c) && !onGrid(top))
        add("grid", describe(c) + " starts " + std::to_string(top) + " px below its card edge");
    }
  }
};

}  // namespace nb4design

inline std::vector<Nb4DesignViolation> nb4DesignViolations(lv_obj_t* root)
{
  std::vector<Nb4DesignViolation> out;
  nb4design::Walker walker(out, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_area_t screen = {0, 0, (lv_coord_t)(lv_disp_get_hor_res(nullptr) - 1),
                      (lv_coord_t)(lv_disp_get_ver_res(nullptr) - 1)};
  walker.walk(root, screen, false);
  walker.finish();
  return out;
}

// Prints a count per rule and, with NB4_DESIGN_REPORT set, writes every
// violation to that file; then fails the test once per rule broken.
inline void nb4ReportDesign(const std::vector<Nb4DesignViolation>& all)
{
  std::map<std::string, unsigned> counts;
  for (const auto& v : all) ++counts[v.rule];
  if (const char* path = getenv("NB4_DESIGN_REPORT")) {
    if (FILE* f = fopen(path, "w")) {
      for (const auto& v : all) fprintf(f, "%s\t%s\n", v.rule.c_str(), v.detail.c_str());
      fclose(f);
    }
  }
  for (const auto& c : counts) {
    std::string first;
    for (const auto& v : all) if (v.rule == c.first) { first = v.detail; break; }
    ADD_FAILURE() << c.second << " design violation(s) of rule '" << c.first
                  << "', first: " << first;
  }
}
