/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   libopenui - https://github.com/opentx/libopenui
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "dialog.h"
#include "nb4_help.h"

#include "edgetx.h"
#include "mainwindow.h"
#include "progress.h"
#include "etx_lv_theme.h"
#include "keyboard_base.h"
#include "button.h"
#include "bitmaps.h"
#include "page.h"
#include "static.h"
#include "theme_manager.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_ui.h"
#include "layer.h"
#endif

#include <algorithm>

//-----------------------------------------------------------------------------

class BaseDialogForm : public Window
{
 public:
  BaseDialogForm(Window* parent, lv_coord_t width, bool flexLayout) : Window(parent, rect_t{})
  {
    etx_scrollbar(lvobj);
    padAll(PAD_TINY);
    if (flexLayout)
      setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, width, LV_SIZE_CONTENT);
  }

 protected:
  void onClicked() override { Keyboard::hide(false); }
};

BaseDialog::BaseDialog(const char* title,
                       bool closeIfClickedOutside, lv_coord_t width,
                       lv_coord_t maxHeight, bool flexLayout) :
    ModalWindow(closeIfClickedOutside)
{
  content = new Window(this, rect_t{});
  content->setWindowFlag(OPAQUE);
  content->padAll(PAD_ZERO);
  content->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, width, LV_SIZE_CONTENT);
  etx_solid_bg(content->getLvObj());
  lv_obj_center(content->getLvObj());

  header = new StaticText(content, {0, 0, LV_PCT(100), 0}, title ? title : "", COLOR_THEME_PRIMARY2_INDEX);
  etx_solid_bg(header->getLvObj(), COLOR_THEME_SECONDARY1_INDEX);
  header->padAll(PAD_SMALL);
  header->show(title != nullptr);

#if defined(RADIO_NB4_FAMILY)

  if (title) {

    // The close button is a full touch target, so the title bar is as tall.
    constexpr coord_t headerH = 44;
    header->padTop((headerH - EdgeTxStyles::STD_FONT_HEIGHT) / 2);
    header->padBottom(headerH - EdgeTxStyles::STD_FONT_HEIGHT -
                      (headerH - EdgeTxStyles::STD_FONT_HEIGHT) / 2);
    header->padRight(headerH);
    auto close = new TextButton(content, {0, 0, headerH, headerH}, LV_SYMBOL_CLOSE,
                                [this]() { onCancel(); return 0; });
    closeButton = close;
    lv_obj_add_flag(close->getLvObj(), LV_OBJ_FLAG_FLOATING);
    lv_obj_align(close->getLvObj(), LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(close->getLvObj(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(close->getLvObj(), 0, LV_PART_MAIN);
    etx_txt_color(close->getLvObj(), COLOR_THEME_PRIMARY2_INDEX, LV_PART_MAIN);

    lv_group_remove_obj(close->getLvObj());
  }
#endif

  form = new BaseDialogForm(content, width, flexLayout);
  if (maxHeight != LV_SIZE_CONTENT)
    lv_obj_set_style_max_height(form->getLvObj(), maxHeight - EdgeTxStyles::UI_ELEMENT_HEIGHT, LV_PART_MAIN);

#if defined(RADIO_NB4_FAMILY)

  form->padTop(PAD_LARGE * 2);
  form->padBottom(PAD_LARGE * 2);
  form->padLeft(PAD_LARGE);
  form->padRight(PAD_LARGE);

  form->padRow(PAD_LARGE * 2);

  header->padAll(PAD_MEDIUM);
  setScopeText(nb4InheritedScope());
#endif
}

#if defined(RADIO_NB4_FAMILY)
void BaseDialog::setScopeText(const std::string& text)
{
  scopeText = text;
  if (!scopeLabel && !text.empty()) {
    scopeLabel = new StaticText(content, {0, 0, LV_PCT(100), 22}, text.c_str(),
      COLOR_THEME_PRIMARY3_INDEX, FONT(XS));
    scopeLabel->padLeft(PAD_MEDIUM);
    scopeLabel->padRight(PAD_MEDIUM);
    lv_label_set_long_mode(scopeLabel->getLvObj(), LV_LABEL_LONG_DOT);
    lv_obj_move_to_index(scopeLabel->getLvObj(), 1);
    const auto maxHeight = lv_obj_get_style_max_height(form->getLvObj(), 0);
    if (maxHeight > 22 && maxHeight < LV_COORD_MAX)
      lv_obj_set_style_max_height(form->getLvObj(), maxHeight - 22, 0);
  }
  if (scopeLabel) { scopeLabel->setText(text); scopeLabel->show(!text.empty()); }
}

void BaseDialog::useSectionHeader()
{
  // These are navigation/help pages, not transient popups. The Home icons
  // visible behind them must not act as an invisible Back target.
  closeWhenClickOutside = false;
  sectionHeader = true;
  // The QuickMenu header treatment, with a real section title in place of a logo.
  const auto bg = pageLayout ? COLOR_THEME_HEADER_BG_INDEX : COLOR_THEME_QM_BG_INDEX;
  const auto fg = pageLayout ? COLOR_THEME_HEADER_FG_INDEX : COLOR_THEME_QM_FG_INDEX;
  etx_solid_bg(header->getLvObj(), bg);
  etx_txt_color(header->getLvObj(), fg);
  etx_font(header->getLvObj(), FONT_BOLD_INDEX);
  header->setHeight(pageLayout ? Nb4Ui::HeaderH : Nb4Ui::Touch);
  header->padAll(PAD_ZERO);
  header->padBottom(PAD_ZERO);
  header->padLeft(PageHeader::PAGE_TITLE_LEFT);
  header->padRight(pageLayout ? 124 : helpButton ? 92 : 44);
  lv_obj_set_style_pad_top(
      header->getLvObj(),
      std::max<coord_t>(0, (44 - getFontHeight(FONT(BOLD))) / 2),
      LV_PART_MAIN);
  lv_obj_set_style_text_align(header->getLvObj(), pageLayout ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_border_side(header->getLvObj(), LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(header->getLvObj(), pageLayout ? 0 : 3, 0);
  etx_border_color(header->getLvObj(), fg);
  lv_label_set_long_mode(header->getLvObj(), LV_LABEL_LONG_CLIP);
  if (closeButton) {
    closeButton->setSize(pageLayout ? 72 : Nb4Ui::Touch, Nb4Ui::Touch);
    etx_txt_color(closeButton->getLvObj(), fg);
  }
  updateSectionTitleStyle();
}

void BaseDialog::usePageLayout()
{
  pageLayout = true;
  useSectionHeader();
  const coord_t w = lv_disp_get_hor_res(nullptr);
  const coord_t h = lv_disp_get_ver_res(nullptr);
  content->setSize(w, h);
  lv_obj_align(content->getLvObj(), LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_clear_flag(content->getLvObj(), LV_OBJ_FLAG_SCROLLABLE);
  etx_solid_bg(content->getLvObj(), COLOR_THEME_SECONDARY3_INDEX);
  etx_solid_bg(lvobj, COLOR_THEME_SECONDARY3_INDEX);
  form->setWidth(LV_PCT(100));
  form->setHeight(0);
  lv_obj_set_flex_grow(form->getLvObj(), 1);
  lv_obj_set_style_max_height(form->getLvObj(),
      h - Nb4Ui::HeaderH - (scopeLabel ? 22 : 0), 0);
  lv_obj_set_scroll_dir(form->getLvObj(), LV_DIR_VER);
  form->padAll(PAD_MEDIUM);
  form->padRow(Nb4Ui::Gap);
  if (closeButton) {
    closeButton->setText(STR_NB4_BACK);
    Nb4Ui::button(closeButton->getLvObj());
    lv_obj_set_style_bg_opa(closeButton->getLvObj(), LV_OPA_COVER, 0);
    lv_obj_align(closeButton->getLvObj(), LV_ALIGN_TOP_RIGHT, -Nb4Ui::SpaceXS,
                  (Nb4Ui::HeaderH - Nb4Ui::Touch) / 2);
  }
  if (helpButton)
    lv_obj_align(helpButton->getLvObj(), LV_ALIGN_TOP_RIGHT, -80,
                  (Nb4Ui::HeaderH - Nb4Ui::Touch) / 2);
  // A settings page owns the entire viewport. Parked grids and Home are
  // restored by Layer when Back closes it, without drawing through a dimmer.
  Layer::cover(this);
  setRouteIcon(ICON_RADIO_SETUP);
  updateSectionTitleStyle();
}

void BaseDialog::updateSectionTitleStyle()
{
  if (!sectionHeader || !header) return;
  // A long route title takes two lines, smaller if it must, rather than
  // losing its end under the buttons.
  auto o = header->getLvObj();
  const coord_t border = lv_obj_get_style_border_width(o, LV_PART_MAIN);
  const coord_t dialogW = lv_obj_get_style_width(content->getLvObj(), LV_PART_MAIN);
  // A dialog sized by its content has no width to fit against yet.
  const coord_t width = LV_COORD_IS_SPEC(dialogW) ? LV_COORD_MAX
                        : dialogW - lv_obj_get_style_pad_left(o, LV_PART_MAIN) -
                              lv_obj_get_style_pad_right(o, LV_PART_MAIN) - 2 * border;
  const coord_t barHeight = pageLayout ? Nb4Ui::HeaderH : Nb4Ui::Touch;
  const coord_t height = nb4FitHeaderTitle(o, width, barHeight - 2 * border);
  lv_obj_set_style_pad_top(o, std::max<coord_t>(0, (barHeight - 2 * border - height) / 2),
                           LV_PART_MAIN);
}

void BaseDialog::setRouteIcon(uint8_t icon)
{
  useSectionHeader();
  if (!routeHeaderIcon) {
    routeHeaderIcon = new HeaderIcon(content, (EdgeTxIcon)icon);
    lv_obj_add_flag(routeHeaderIcon->getLvObj(), LV_OBJ_FLAG_FLOATING);
    lv_obj_align(routeHeaderIcon->getLvObj(), LV_ALIGN_TOP_LEFT, 0, 0);
  } else {
    routeHeaderIcon->setIcon((EdgeTxIcon)icon);
  }
  routeHeaderIcon->bringToTop();
  if (helpButton) helpButton->bringToTop();
  if (closeButton) closeButton->bringToTop();
}

bool BaseDialog::setHelpHandler(std::function<void()> action)
{
  helpHandler = std::move(action);
  if (helpHandler) closeWhenClickOutside = false;
  if (!helpButton && helpHandler) {
    helpButton = new TextButton(content, {0, 0, 44, 44}, "?",
      [this] { if (helpHandler) helpHandler(); return 0; });
    Nb4Ui::help(helpButton->getLvObj());
    lv_obj_add_flag(helpButton->getLvObj(), LV_OBJ_FLAG_FLOATING);
    lv_obj_align(helpButton->getLvObj(), LV_ALIGN_TOP_RIGHT, pageLayout ? -80 : -48,
                  pageLayout ? (Nb4Ui::HeaderH - Nb4Ui::Touch) / 2 : 0);
    header->padRight(pageLayout ? 124 : 92);
    updateSectionTitleStyle();
    lv_group_remove_obj(helpButton->getLvObj());
  }
  if (helpButton) helpButton->show(bool(helpHandler));
  return true;
}

void BaseDialog::useBrandHeader()
{
  useSectionHeader();
  header->show(false);

  auto brand = new Window(content, {0, 0, LV_PCT(100), 44});
  brand->setWindowFlag(OPAQUE);
  brand->padAll(PAD_ZERO);
  etx_solid_bg(brand->getLvObj(), COLOR_THEME_QM_BG_INDEX);
  lv_obj_set_style_border_side(brand->getLvObj(), LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(brand->getLvObj(), 3, 0);
  etx_border_color(brand->getLvObj(), COLOR_THEME_QM_FG_INDEX);
  lv_obj_move_to_index(brand->getLvObj(), 0);

  auto mask = getBuiltinIcon(ICON_TOP_LOGO);
  const coord_t logoW = mask ? mask->width : 0;
  const coord_t logoH = mask ? mask->height : 0;
  new StaticIcon(brand, 8, std::max<coord_t>(0, (44 - logoH) / 2),
                 ICON_TOP_LOGO, COLOR_THEME_QM_FG_INDEX);
  const coord_t identityW = logoW + 16;
  lv_obj_update_layout(content->getLvObj());
  const coord_t brandW = lv_obj_get_content_width(brand->getLvObj());
  const coord_t titleW = coord_t(std::max<int>(40, brandW - identityW - Nb4Ui::Touch - Nb4Ui::SpaceXS));
  auto title = new StaticText(
      brand, {identityW, 0, titleW, getFontHeight(FONT(BOLD))},
      header->getText(), COLOR_THEME_QM_FG_INDEX, FONT(BOLD) | CENTERED);
  title->setHeight(nb4FitHeaderTitle(title->getLvObj(), titleW, 44 - 2 * 3));
  lv_obj_align(title->getLvObj(), LV_ALIGN_LEFT_MID, identityW, 0);
}
#endif

void BaseDialog::setTitle(const char* title)
{
  header->setText(title);
#if defined(RADIO_NB4_FAMILY)
  updateSectionTitleStyle();
#endif
}

//-----------------------------------------------------------------------------

ProgressDialog::ProgressDialog(const char* title,
                               std::function<void()> onClose) :
    BaseDialog(title, false), onClose(std::move(onClose))
{
  progress = new Progress(form, rect_t{0, 0, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT});
  updateProgress(0);
}

void ProgressDialog::setTitle(std::string title)
{
  header->setText(title);
  header->show();
}

void ProgressDialog::updateProgress(int percentage)
{
  progress->setValue(percentage);
  lv_refr_now(nullptr);
}

void ProgressDialog::closeDialog()
{
  deleteLater();
  onClose();
}

//-----------------------------------------------------------------------------

MessageDialog::MessageDialog(const char* title,
                             const char* message, const char* info,
                             LcdFlags messageFlags, LcdFlags infoFlags) :
    BaseDialog(title, true)
{
  messageWidget = new StaticText(form, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
                                 message, COLOR_THEME_PRIMARY1_INDEX, messageFlags);

  if (info) {
    infoWidget = new StaticText(form, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
                                info, COLOR_THEME_PRIMARY1_INDEX, infoFlags);
  }
}

void MessageDialog::onClicked() { deleteLater(); }

//-----------------------------------------------------------------------------

DynamicMessageDialog::DynamicMessageDialog(
    const char* title, std::function<std::string()> textHandler,
    const char* message, const int lineHeight, LcdColorIndex color, LcdFlags textFlags) :
    BaseDialog(title, true)
{
  messageWidget = new StaticText(form, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
                                 message, COLOR_THEME_PRIMARY1_INDEX, CENTERED);

  infoWidget = new DynamicText(form, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
                               textHandler, color, textFlags);
}

void DynamicMessageDialog::onClicked() { deleteLater(); }

//-----------------------------------------------------------------------------

ConfirmDialog::ConfirmDialog(const char* title,
                             const char* message,
                             std::function<void(void)> confirmHandler,
                             std::function<void(void)> cancelHandler) :
    BaseDialog(title, false),
    confirmHandler(std::move(confirmHandler)),
    cancelHandler(std::move(cancelHandler))
{
  if (message) {
    new StaticText(form, {0, 0, LV_PCT(100), 0}, message, COLOR_THEME_PRIMARY1_INDEX, CENTERED);
  }

  auto box = new Window(form, rect_t{});
  box->padAll(PAD_TINY);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, 40, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(box->getLvObj(), LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);

  new TextButton(box, rect_t{0, 0, 96, 0}, STR_NO, [=]() -> int8_t {
    onCancel();
    return 0;
  });

  new TextButton(box, rect_t{0, 0, 96, 0}, STR_YES, [=]() -> int8_t {
    this->deleteLater();
    this->confirmHandler();
    return 0;
  });
}

void ConfirmDialog::onCancel()
{
  deleteLater();
  if (cancelHandler) cancelHandler();
}

//-----------------------------------------------------------------------------

LabelDialog::LabelDialog(const char *label, int length, const char* title,
            std::function<void(std::string)> _saveHandler) :
    ModalWindow(false), saveHandler(std::move(_saveHandler))
{
  assert(length <= MAX_LABEL_LENGTH);

  strncpy(this->label, label, length);
  this->label[length] = '\0';

  auto form = new Window(this, rect_t{});
  form->padAll(PAD_ZERO);
  form->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, lv_disp_get_hor_res(nullptr) * 0.8,
                      LV_SIZE_CONTENT);
  etx_solid_bg(form->getLvObj());
  lv_obj_center(form->getLvObj());

  auto hdr = new StaticText(form, {0, 0, LV_PCT(100), 0}, title,
                            COLOR_THEME_PRIMARY2_INDEX);
  etx_solid_bg(hdr->getLvObj(), COLOR_THEME_SECONDARY1_INDEX);
  hdr->padAll(PAD_MEDIUM);

  auto box = new Window(form, rect_t{});
  box->padAll(PAD_MEDIUM);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, 40, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(box->getLvObj(), LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);

  new TextEdit(box, rect_t{0, 0, LV_PCT(100), 0}, this->label, length);

  box = new Window(form, rect_t{});
  box->padAll(PAD_MEDIUM);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, 40, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(box->getLvObj(), LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);

  new TextButton(box, rect_t{0, 0, 96, 0}, STR_CANCEL, [=]() {
    deleteLater();
    return 0;
  });

  new TextButton(box, rect_t{0, 0, 96, 0}, STR_SAVE, [=]() {
    deleteLater();
    if (saveHandler != nullptr) saveHandler(this->label);
    return 0;
  });
}
