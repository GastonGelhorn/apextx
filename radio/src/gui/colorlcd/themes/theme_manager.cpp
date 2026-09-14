/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
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
#include "theme_manager.h"
#include "nb4_health.h"
#include "nb4_palettes.h"
#include "nb4_car_state.h"

#include "hal/abnormal_reboot.h"
#include "../../storage/sdcard_common.h"
#include "../../storage/yaml/yaml_bits.h"
#include "../../storage/yaml/yaml_tree_walker.h"
#include "etx_lv_theme.h"
#include "topbar.h"
#include "view_main.h"
#include "storage/sdcard_yaml.h"
#include "lib_file.h"
#include "pagegroup.h"

#define SET_DIRTY() storageDirty(EE_GENERAL)

#define MAX_FILES 9

constexpr const char* RGBSTRING = "RGB(";

ThemePersistance ThemePersistance::themePersistance;

static uint32_t r_color(const YamlNode* node, const char* val, uint8_t val_len)
{
  if ((val_len >= 6) && (strncmp(val, RGBSTRING, strlen(RGBSTRING)) == 0) &&
      (val[val_len - 1] == ')')) {
    char text[32];
    if (val_len >= sizeof(text)) return 0;
    memcpy(text, val, val_len);
    text[val_len] = 0;
    int r, g, b;
    int numTokens = sscanf(text, "RGB(%i,%i,%i)", &r, &g, &b);

    if (numTokens == 3 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255)
      return RGB(r, g, b);

  } else if (val_len > 2 && val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
    val += 2;
    val_len -= 2;

    auto rgb24 = yaml_hex2uint(val, val_len);
    return RGB((rgb24 & 0xFF0000) >> 16, (rgb24 & 0xFF00) >> 8, rgb24 & 0xFF);
  }

  TRACE("Theme: Invalid color value");
  return 0;
}

static bool w_color(const YamlNode* node, uint32_t val, yaml_writer_func wf,
                    void* opaque)
{
  uint32_t color = (uint32_t)GET_RED(val) << 16 |
                   (uint32_t)GET_GREEN(val) << 8 | (uint32_t)GET_BLUE(val);

  if (!wf(opaque, "0x", 2)) return false;
  return wf(opaque, yaml_rgb2hex(color), 3 * 2);
}

PACK(struct YAMLThemeSummary {
  char name[SELECTED_THEME_NAME_LEN + 1];
  char author[ThemeFile::AUTHOR_LENGTH + 1];
  char info[ThemeFile::INFO_LENGTH + 1];
});

PACK(struct YAMLThemeColors {
  uint32_t colors[THEME_COLOR_COUNT - 1];  // We don't use CUSTOM
});

PACK(struct YAMLTheme {
  struct YAMLThemeSummary summary;
  struct YAMLThemeColors colors;

  YAMLTheme() { memset(this, 0, sizeof(YAMLTheme)); }
});

static const struct YamlNode struct_YAMLThemeSummary[] = {
    YAML_STRING("name", (SELECTED_THEME_NAME_LEN + 1)),
    YAML_STRING("author", (ThemeFile::AUTHOR_LENGTH + 1)),
    YAML_STRING("info", (ThemeFile::INFO_LENGTH + 1)), YAML_END};

static const struct YamlNode struct_YAMLThemeColors[] = {
    YAML_UNSIGNED_CUST("PRIMARY1", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("PRIMARY2", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("PRIMARY3", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("SECONDARY1", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("SECONDARY2", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("SECONDARY3", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("FOCUS", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("EDIT", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("ACTIVE", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("WARNING", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("DISABLED", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("QM_BG", 32, r_color, w_color),
    YAML_UNSIGNED_CUST("QM_FG", 32, r_color, w_color),
    YAML_END};

// This hack is required to ensure the YAML file written is backward compatible
// with the old parser. Otherwise older versions of EdgeTX will not load the
// theme files.
// TODO: Remove this sometime in the future
static const struct YamlNode w_struct_YAMLTheme[] = {
    YAML_STRUCT("---\r\nsummary",
                (SELECTED_THEME_NAME_LEN + ThemeFile::AUTHOR_LENGTH + ThemeFile::INFO_LENGTH + 3) * 8,
                struct_YAMLThemeSummary, NULL),
    YAML_STRUCT("colors", (THEME_COLOR_COUNT - 1) * 32, struct_YAMLThemeColors,
                NULL),
    YAML_END};
static const struct YamlNode r_struct_YAMLTheme[] = {
    YAML_STRUCT("summary",
                (SELECTED_THEME_NAME_LEN + ThemeFile::AUTHOR_LENGTH + ThemeFile::INFO_LENGTH + 3) * 8,
                struct_YAMLThemeSummary, NULL),
    YAML_STRUCT("colors", (THEME_COLOR_COUNT - 1) * 32, struct_YAMLThemeColors,
                NULL),
    YAML_END};

static const char* const colorNames[THEME_COLOR_COUNT] = {
    STR_THEME_COLOR_PRIMARY1,   STR_THEME_COLOR_PRIMARY2,
    STR_THEME_COLOR_PRIMARY3,   STR_THEME_COLOR_SECONDARY1,
    STR_THEME_COLOR_SECONDARY2, STR_THEME_COLOR_SECONDARY3,
    STR_THEME_COLOR_FOCUS,      STR_THEME_COLOR_EDIT,
    STR_THEME_COLOR_ACTIVE,     STR_THEME_COLOR_WARNING,
    STR_THEME_COLOR_DISABLED,   STR_THEME_COLOR_QM_BG,
    STR_THEME_COLOR_QM_FG,      STR_THEME_COLOR_CUSTOM,
};

ThemeFile::ThemeFile(std::string themePath, bool loadYAML) : path(themePath)
{
  if (loadYAML && path.size()) {
    deSerialize();
  }

  auto found = path.rfind('/');
  if (found != std::string::npos) {
    int n = 0;
    while (n < MAX_FILES) {
      auto baseFileName(path.substr(0, found + 1) +
                        (n != 0 ? "screenshot" + std::to_string(n) : "logo") +
                        ".png");
      if (isFileAvailable(baseFileName.c_str(), true)) {
        _imageFileNames.emplace_back(baseFileName);
      } else {
        break;
      }

      n++;
    }
  }
}

ThemeFile& ThemeFile::operator=(const ThemeFile& theme)
{
  if (this == &theme) return *this;

  path = theme.path;
  name = theme.name;
  author = theme.author;
  info = theme.info;
  colorList.assign(theme.colorList.begin(), theme.colorList.end());
  _imageFileNames.assign(theme._imageFileNames.begin(),
                         theme._imageFileNames.end());

  return *this;
};

std::vector<std::string> ThemeFile::getThemeImageFileNames()
{
  return _imageFileNames;
}

void ThemeFile::serialize()
{
  struct YAMLTheme yt;
  struct YamlNode themeRootNode = YAML_ROOT(w_struct_YAMLTheme);

  strAppend(yt.summary.name, name.c_str(), SELECTED_THEME_NAME_LEN);
  strAppend(yt.summary.author, author.c_str(), ThemeFile::AUTHOR_LENGTH);
  strAppend(yt.summary.info, info.c_str(), ThemeFile::INFO_LENGTH);
  for (auto colorEntry : colorList) {
    yt.colors.colors[colorEntry.colorNumber] = colorEntry.colorValue;
  }

  auto err = writeFileYaml(path.c_str(), &themeRootNode, (uint8_t*)&yt, 0);
  if (err != nullptr) {
    ALERT(STR_WARNING, err, AU_WARNING1);
  }
}

void ThemeFile::deSerialize()
{
#if defined(RADIO_NB4_FAMILY)
  FILINFO fileInfo;
  if (f_stat(path.c_str(), &fileInfo) != FR_OK || fileInfo.fsize > 16384) {
    valid = false;
    return;
  }
#endif
  struct YAMLTheme yt;
  struct YamlNode themeRootNode = YAML_ROOT(r_struct_YAMLTheme);

  // initialize the color table to defaults (in case of missing entries)
  for (uint8_t i = COLOR_THEME_PRIMARY1_INDEX; i < THEME_COLOR_COUNT - 1; i += 1)
    yt.colors.colors[i] = defaultColors[i];

  YamlTreeWalker tree;
  tree.reset(&themeRootNode, (uint8_t*)&yt);
  auto err = readYamlFile(path.c_str(), YamlTreeWalker::get_parser_calls(),
                          &tree, nullptr);

  if (err == nullptr) {
    if (!yt.summary.name[0]) { valid = false; return; }
    name = yt.summary.name;
    author = yt.summary.author;
    info = yt.summary.info;
    for (int i = 0; i < THEME_COLOR_COUNT - 1; i += 1) {
      colorList.emplace_back(
          ColorEntry{(LcdColorIndex)(i), yt.colors.colors[i]});
    }
  } else {
    valid = false;
    TRACE("Theme: skipping invalid file %s: %s", path.c_str(), err);
  }
}

void ThemeFile::setColor(LcdColorIndex colorIndex, uint32_t color)
{
  auto colorEntry =
      std::find(colorList.begin(), colorList.end(), ColorEntry{colorIndex, 0});
  if (colorEntry != colorList.end())
    colorEntry->colorValue = color;
  else
    colorList.emplace_back(ColorEntry{colorIndex, color});
}

void ThemeFile::applyColors()
{
  for (auto color : colorList) {
    lcdColorTable[color.colorNumber] = color.colorValue;
  }
}

void ThemeFile::applyBackground()
{
  auto instance = MainWindow::instance();
  std::string backgroundImageFileName(getPath());
  auto pos = backgroundImageFileName.rfind('/');
  if (pos != std::string::npos) {
    auto rootDir = backgroundImageFileName.substr(0, pos + 1);
#if defined(LCD_DUAL_ORIENTATION)

    rootDir = rootDir + "background_" + std::to_string(LCD_PHYS_W) + "x" +
              std::to_string(LCD_PHYS_H) + ".png";
#else
    rootDir = rootDir + "background_" + std::to_string(lv_disp_get_hor_res(nullptr)) + "x" +
              std::to_string(lv_disp_get_ver_res(nullptr)) + ".png";
#endif

    if (isFileAvailable(rootDir.c_str())) {
      instance->setBackgroundImage((char*)rootDir.c_str());
      return;
    }

    rootDir = backgroundImageFileName.substr(0, pos + 1);
    rootDir = rootDir + "background.png";

    if (isFileAvailable(rootDir.c_str())) {
      instance->setBackgroundImage((char*)rootDir.c_str());
      return;
    }
  }

  // Use EdgeTxTheme default background
  // TODO: This needs to be made user configurable
  instance->setBackgroundImage("");
}

void ThemeFile::applyTheme()
{
  applyColors();
#if defined(RADIO_NB4_FAMILY)

  nb4ApplyAccent();
#endif
  applyBackground();
  styles->applyColors();
  // applyColors() mutates the SHARED lv_style_t objects that every etx_* helper
  // attached by reference, but LVGL does not know they changed. Without this,
  // switching palette only repaints windows that happen to be rebuilt.
  lv_obj_report_style_change(nullptr);
}

// avoid leaking memory
void ThemePersistance::clearThemes()
{
  for (auto theme : themes) {
    delete theme;
  }
  themes.clear();
}

void ThemePersistance::scanThemeFolder(char* themeFolder)
{
  char themePath[FF_MAX_LFN + 1];
  char* s = strAppend(themePath, THEMES_PATH "/", FF_MAX_LFN);
  s = strAppend(s, themeFolder, FF_MAX_LFN - (s - themePath));
  strAppend(s, "/theme.yml", FF_MAX_LFN - (s - themePath));
  if (isFileAvailable(themePath, true)) {
    TRACE("scanForThemes: found file %s", themePath);
    auto theme = new ThemeFile(themePath);
#if defined(RADIO_NB4_FAMILY)
    // Packaged reference palettes must not duplicate the integrated themes.
    if (nb4PaletteIndexByName(theme->getName().c_str()) >= 0) {
      delete theme;
      return;
    }
#endif
    if (theme->isValid()) themes.emplace_back(theme);
    else delete theme;
  }
}

void ThemePersistance::scanForThemes()
{
  DIR dir;
  FILINFO fno;

  char fullPath[FF_MAX_LFN + 1];

  strAppend(fullPath, THEMES_PATH, FF_MAX_LFN);

  TRACE("opening directory: %s", fullPath);
  FRESULT res = f_opendir(&dir, fullPath);  // Open the directory
  if (res == FR_OK) {
    TRACE("scanForThemes: open successful");
    // read all entries
    bool firstTime = true;
    for (unsigned inspected = 0; inspected < MAX_FILES; ++inspected) {
      res = sdReadDir(&dir, &fno, firstTime);

      if (res != FR_OK || fno.fname[0] == 0)
        break;  // Break on error or end of dir

      if (strlen((const char*)fno.fname) > SD_SCREEN_FILE_LENGTH) continue;
      if (fno.fattrib & AM_DIR)
        scanThemeFolder(fno.fname);
    }

    f_closedir(&dir);
    std::sort(themes.begin(), themes.end(), [](ThemeFile* a, ThemeFile* b) {
      return a->getName().compare(b->getName()) < 0;
    });
  }
}

void ThemePersistance::refresh()
{
  const auto selected = getCurrentTheme() ? getCurrentTheme()->getName() : "";
  clearThemes();
  if (!UNEXPECTED_SHUTDOWN() && !nb4HealthRecovery())
    scanForThemes();
  insertDefaultTheme();
  currentTheme = 0;
  for (unsigned i = 0; i < themes.size(); ++i)
    if (themes[i]->getName() == selected) currentTheme = i;
}

void ThemePersistance::loadDefaultTheme()
{
  refresh();
  if (nb4HealthRecovery()) {
    applyTheme(0);
    setThemeIndex(0);
    return;
  }

  int index = 0;
  bool found = false;

  // Load theme from 'selectedtheme.txt' file, if not set in radio settings
  // TODO: remove this sometime in the future
  if (g_eeGeneral.selectedTheme[0] == 0) {
    constexpr const char* SELECTED_THEME_FILE =
        THEMES_PATH "/selectedtheme.txt";

    FIL file;
    FRESULT status = f_open(&file, SELECTED_THEME_FILE, FA_READ);

    if (status == FR_OK) {
      char line[256];
      unsigned int len;

      status = f_read(&file, line, sizeof(line) - 1, &len);
      if (status == FR_OK) {
        line[len] = '\0';

        for (auto theme : themes) {
          if (theme->getPath() == std::string(line)) {
            found = true;
            break;
          }
          index++;
        }

        // Force default if no match for last selected theme
        if (!found) index = 0;
      }

      f_close(&file);

      // Delete old file
      f_unlink(SELECTED_THEME_FILE);
    }

    // Save selected theme (sets to default if nothing found)
    setDefaultTheme(index);

    // Reset values (used below);
    index = 0;
    found = false;
  }

#if defined(RADIO_NB4_FAMILY)
  // Normalize legacy development names before resolving the saved theme. This
  // preserves the selected palette across upgrades and writes its English name
  // back to radio settings.
  const int builtinPalette = nb4PaletteIndexByName(g_eeGeneral.selectedTheme);
  if (builtinPalette >= 0 &&
      strcmp(g_eeGeneral.selectedTheme, nb4Palette(builtinPalette).name) != 0) {
    strAppend(g_eeGeneral.selectedTheme, nb4Palette(builtinPalette).name,
              SELECTED_THEME_NAME_LEN);
    SET_DIRTY();
  }
#endif

  for (auto theme : themes) {
    if (theme->getName().compare(0, SELECTED_THEME_NAME_LEN, g_eeGeneral.selectedTheme) == 0) {
      found = true;
      break;
    }
    index++;
  }

  // Force default if no match for last selected theme
  if (!found) index = 0;

  applyTheme(index);
  setThemeIndex(index);
}

char** ThemePersistance::getColorNames() { return (char**)colorNames; }

bool ThemePersistance::deleteThemeByIndex(int index)
{
  if (index >= 0 && index < (int)themes.size() && !themes[index]->isBuiltin()) {
    ThemeFile* theme = themes[index];

    char newFile[FF_MAX_LFN + 10];
    strAppend(newFile, theme->getPath().c_str(), FF_MAX_LFN);
    strcat(newFile, ".deleted");

    if (isFileAvailable(newFile, true)) {
      // .deleted file already exists, remove it
      f_unlink(newFile);
    }

    // for now we are just renaming the file so we don't find it
    FRESULT status = f_rename(theme->getPath().c_str(), newFile);
    refresh();

    // make sure currentTheme stays in bounds
    if (getThemeIndex() >= (int)themes.size()) setThemeIndex(themes.size() - 1);

    return status == FR_OK;
  }
  return false;
}

bool ThemePersistance::createNewTheme(const std::string& name, ThemeFile& theme)
{
  char fullPath[FF_MAX_LFN + 1];
  char* s = strAppend(fullPath, THEMES_PATH, FF_MAX_LFN);
  s = strAppend(s, "/", FF_MAX_LFN - (s - fullPath));
  s = strAppend(s, name.c_str(), FF_MAX_LFN - (s - fullPath));

  if (!isFileAvailable(THEMES_PATH)) {
    FRESULT result = f_mkdir(THEMES_PATH);
    if (result != FR_OK) return false;
  }

  FRESULT result = f_mkdir(fullPath);
  s = strAppend(s, "/", FF_MAX_LFN - (s - fullPath));
  strAppend(s, "theme.yml", FF_MAX_LFN - (s - fullPath));
  if (result == FR_EXIST) {
    if (isFileAvailable(fullPath, true)) {
      POPUP_WARNING(STR_THEME_EXISTS);
      return false;
    }
  } else if (result != FR_OK) return false;
  theme.setPath(fullPath);
  theme.serialize();
  refresh();
  return true;
}

void ThemePersistance::setDefaultTheme(int index)
{
  if (index >= 0 && index < (int)themes.size()) {
    strAppend(g_eeGeneral.selectedTheme, themes[index]->getName().c_str(),
              SELECTED_THEME_NAME_LEN);
    SET_DIRTY();
    currentTheme = index;
  }
}

class DefaultEdgeTxTheme : public ThemeFile
{
 public:
#if defined(RADIO_NB4_FAMILY)
  DefaultEdgeTxTheme() : ThemeFile(THEMES_PATH "/ApexTX/", false)
#else
  DefaultEdgeTxTheme() : ThemeFile(THEMES_PATH "/EdgeTX/", false)
#endif
  {
    builtin = true;
#if defined(RADIO_NB4_FAMILY)
    setName("Classic");
    setAuthor("Upstream contributors");
    setInfo("Classic color scheme");
#else
    setName("EdgeTX Default");
    setAuthor("EdgeTX Team");
    setInfo("Default EdgeTX Color Scheme");
#endif

    // initialize the default color table
    for (uint8_t i = COLOR_THEME_PRIMARY1_INDEX; i < THEME_COLOR_COUNT - 1; i += 1)
      colorList.emplace_back(ColorEntry{(LcdColorIndex)i, defaultColors[i]});
  }
};

#if defined(RADIO_NB4_FAMILY)
class Nb4BuiltinTheme : public ThemeFile
{
 public:
  explicit Nb4BuiltinTheme(unsigned index)
  {
    const Nb4Palette& palette = nb4Palette(index);
    builtin = true;
    setName(palette.name);
    setAuthor("ApexTX");
    setInfo(palette.info());
    static_assert(NB4_PALETTE_COLORS == THEME_COLOR_COUNT - 1,
                  "the palette must cover every theme color");
    for (unsigned i = 0; i < NB4_PALETTE_COLORS; ++i) {
      const auto c = palette.colors[i];
      colorList.push_back({(LcdColorIndex)i, RGB((c >> 16) & 255, (c >> 8) & 255, c & 255)});
    }
  }
};
#endif

void ThemePersistance::insertDefaultTheme()
{
  auto themeFile = new DefaultEdgeTxTheme();
  themes.insert(themes.begin(), themeFile);
#if defined(RADIO_NB4_FAMILY)
  // Motorsport palettes extend EdgeTX's catalogue.  The original built-in
  // theme remains available as the stable base for all native controls, but it
  // goes BEHIND them: index 0 is both the factory default and the fallback for
  // a selectedTheme name that no longer resolves (see loadDefaultTheme), and a
  // track radio must never land on a light theme by accident.
  for (unsigned i = nb4PaletteCount(); i > 0; --i)
    themes.insert(themes.begin(), new Nb4BuiltinTheme(i - 1));
#endif
}

HeaderDateTime::HeaderDateTime(Window* parent, coord_t x, coord_t y) :
  Window(parent, {x, y, HDR_DATE_WIDTH, HDR_DATE_LINE2 + HDR_DATE_HEIGHT + 2})
{
  date = etx_label_create(lvobj, FONT_XS_INDEX);
  lv_obj_set_pos(date, 0, 0);
  lv_obj_set_size(date, HDR_DATE_WIDTH, HDR_DATE_HEIGHT);
  lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  etx_txt_color(date, COLOR_THEME_PRIMARY2_INDEX);

  time = etx_label_create(lvobj, FONT_XS_INDEX);
  lv_obj_set_pos(time, 0, HDR_DATE_LINE2);
  lv_obj_set_size(time, HDR_DATE_WIDTH, HDR_DATE_HEIGHT);
  lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  etx_txt_color(time, COLOR_THEME_PRIMARY2_INDEX);

  lv_obj_add_flag(lvobj, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);

  checkEvents();
}

void HeaderDateTime::checkEvents()
{
  const TimerOptions timerOptions = {.options = SHOW_TIME};
  struct gtm t;
  gettime(&t);

  if (t.tm_min != lastTime.tm_min || t.tm_hour != lastTime.tm_hour ||
      t.tm_mday != lastTime.tm_mday || t.tm_mon != lastTime.tm_mon) {
    char str[10];
#if defined(TRANSLATIONS_CN) || defined(TRANSLATIONS_TW)
    sprintf(str, "%02d-%02d", t.tm_mon + 1, t.tm_mday);
#else
    sprintf(str, "%d %s", t.tm_mday, STR_MONTHS[t.tm_mon]);
#endif
    lv_label_set_text(date, str);

    getTimerString(str, getValue(MIXSRC_TX_TIME), timerOptions);
    lv_label_set_text(time, str);

    lastTime = t;
  }
}

void HeaderDateTime::setColor(LcdFlags color)
{
  etx_txt_color_from_flags(date, color);
  etx_txt_color_from_flags(time, color);
}

HeaderIcon::HeaderIcon(Window* parent, EdgeTxIcon icon, std::function<void()> action) :
  StaticIcon(parent, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_FOCUS_INDEX),
  action(std::move(action))
{
  this->icon = new StaticIcon(this, 0, 0, icon, COLOR_THEME_PRIMARY2_INDEX);
  this->icon->center(width() - PAD_SMALL, height());
#if defined(HARDWARE_TOUCH)
  if (this->action) {
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
    addCustomButton(0, 0, [=]() { this->action(); });
  }
#endif
}

HeaderIcon::HeaderIcon(Window* parent, const char* iconFile, std::function<void()> action) :
  StaticIcon(parent, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_FOCUS_INDEX),
  action(std::move(action))
{
  this->icon = new StaticIcon(this, 0, 0, iconFile, COLOR_THEME_PRIMARY2_INDEX);
  this->icon->center(width(), height());
#if defined(HARDWARE_TOUCH)
  if (this->action) {
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
    addCustomButton(0, 0, [=]() { this->action(); });
  }
#endif
}

HeaderBackIcon::HeaderBackIcon(Window* parent, std::function<void()> action) :
  StaticIcon(parent, lv_disp_get_hor_res(nullptr) - PageGroup::PAGE_GROUP_BACK_BTN_XO, 0, ICON_TOPRIGHT_BG, COLOR_THEME_FOCUS_INDEX),
  action(std::move(action))
{
  (new StaticIcon(this, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX))->center(width() + PAD_MEDIUM, height());
#if defined(HARDWARE_TOUCH)
  if (this->action) {
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
    addCustomButton(0, 0, [=]() { this->action(); });
  }
#endif
}

UsbSDConnected::UsbSDConnected() :
    Window(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)})
{
  setWindowFlag(OPAQUE);

  etx_solid_bg(lvobj, COLOR_THEME_PRIMARY1_INDEX);
  new HeaderDateTime(this, lv_disp_get_hor_res(nullptr) - TopBar::HDR_DATE_XO, PAD_MEDIUM);

  auto icon = new StaticIcon(this, 0, 0, ICON_USB_PLUGGED, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_center(icon->getLvObj());
}
