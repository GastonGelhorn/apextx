/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_menu_pages.h"
#include "nb4_routes.h"
#include "nb4_home.h"
#include "nb4_model_compat.h"
#include "storage/sdcard_common.h"
#include "storage/sdcard_yaml.h"
#include "storage/modelslist.h"
#include "layout.h"
#include "view_main.h"
#include "dialog.h"
#include "static.h"
#include "menu.h"
#include "lib_file.h"
#include <memory>

namespace {
struct ModelSnapshot {
  ModelData model;
  TopBarPersistentData topbar;
  std::vector<std::pair<unsigned, CustomScreenData>> screens;
  ModelSnapshot() : model(g_model), topbar(*g_model.getTopbarData()) {
    for (unsigned i = 0; i < MAX_CUSTOM_SCREENS; ++i)
      if (g_model.hasScreenData(i)) screens.emplace_back(i, *g_model.getScreenData(i));
  }
  void restore() {
    g_model.resetScreenData();
    g_model = model;
    *g_model.getTopbarData() = topbar;
    for (const auto& screen : screens) *g_model.getScreenData(screen.first) = screen.second;
  }
};
bool personalFolder(const std::string& folder) {
  return folder == PERS_TEMPL_PATH || folder == PERS_TEMPL_PATH_OLD;
}
bool safePart(const char* name) {
  return name && *name && strcmp(name, ".") && strcmp(name, "..") &&
         !strpbrk(name, "/\\:*?\"<>|") && name[0] != '.';
}
void showError(const char* error) {
  if (error) new MessageDialog(STR_NB4_TEMPLATES, error);
}
}

bool nb4TemplateNameValid(const char* name) {
  if (!safePart(name) || strlen(name) > LEN_MODEL_NAME) return false;
  for (const char* p = name; *p; ++p) if (uint8_t(*p) < 32) return false;
  return name[strlen(name) - 1] != ' ' && name[strlen(name) - 1] != '.';
}

const char* nb4CreateCarFromTemplate(const char* path) {
  if (modelslist.size() >= MAX_MODELS) return STR_TOO_MANY_MODELS;
  if (!path || strncmp(path, TEMPLATES_PATH "/", sizeof(TEMPLATES_PATH)) ||
      strstr(path, "/../")) return STR_NB4_UX_TEMPLATE_INVALID;
  if (nb4InspectModelFile(path)) return STR_NB4_UX_TEMPLATE_INVALID;
  nb4FlushSettings();
  if (!nb4ModelBlocked() && writeModel()) return STR_NB4_UX_TEMPLATE_ERROR;
  char filename[LEN_MODEL_FILENAME + 1] = MODEL_FILENAME_PATTERN;
  if (findNextFileIndex(filename, LEN_MODEL_FILENAME, MODELS_PATH) <= 0)
    return STR_NB4_UX_TEMPLATE_ERROR;
  const std::string destination = std::string(MODELS_PATH) + "/" + filename;
  const std::string temporary = destination + ".creating";
  if (sdCopyFile(path, temporary.c_str())) return STR_NB4_UX_TEMPLATE_ERROR;
  if (nb4InspectModelFile(temporary.c_str())) return STR_NB4_UX_TEMPLATE_INVALID;

  // The YAML reader owns dynamic screen storage. Save that separately from
  // packed ModelData so a parse/promotion failure restores the complete car.
  auto previous = std::make_unique<ModelSnapshot>();
  preModelLoad();
  LayoutFactory::deleteCustomScreens();
  LayoutFactory::deleteTopBarWidgets();
  const auto temporaryName = std::string(filename) + ".creating";
  const char* error = readModelYaml(temporaryName.c_str(), (uint8_t*)&g_model,
                                    sizeof(g_model), MODELS_PATH);
  if (!error && f_rename(temporary.c_str(), destination.c_str()) != FR_OK)
    error = STR_NB4_UX_TEMPLATE_ERROR;
  if (error) {
    previous->restore();
    postModelLoad(false);
    LayoutFactory::loadCustomScreens();
    return STR_NB4_UX_TEMPLATE_ERROR;
  }
  nb4AcceptNewCarModel();
  strAppend(g_eeGeneral.currModelFilename, filename, LEN_MODEL_FILENAME);
  auto cell = modelslist.addModel(filename, false);
  modelslist.setCurrentModel(cell);
  modelslist.updateCurrentModelCell();
  nb4MigrateHome(destination.c_str());
  postModelLoad(false);
  LayoutFactory::loadCustomScreens();
  storageDirty(EE_GENERAL | EE_MODEL);
  modelslabels.setDirty();
  storageCheck(false);
  return nullptr;
}

const char* nb4SavePersonalTemplate(const char* name, bool overwrite) {
  if (!nb4TemplateNameValid(name) || nb4ModelBlocked()) return STR_NB4_UX_TEMPLATE_INVALID;
  nb4FlushSettings();
  if (writeModel()) return STR_NB4_UX_TEMPLATE_ERROR;
  if (sdCheckAndCreateDirectory(TEMPLATES_PATH) ||
      sdCheckAndCreateDirectory(PERS_TEMPL_PATH)) return STR_NB4_UX_TEMPLATE_ERROR;
  const std::string target = std::string(PERS_TEMPL_PATH) + "/" + name + YAML_EXT;
  FILINFO info;
  const auto exists = f_stat(target.c_str(), &info);
  if (exists == FR_OK && !overwrite) return STR_FILE_EXISTS;
  if (exists != FR_OK && exists != FR_NO_FILE) return STR_NB4_UX_TEMPLATE_ERROR;
  const auto temporary = target + ".tmp";
  const auto source = std::string(MODELS_PATH) + "/" + g_eeGeneral.currModelFilename;
  if (sdCopyFile(source.c_str(), temporary.c_str())) return STR_NB4_UX_TEMPLATE_ERROR;
  const auto previous = target + ".previous";
  if (exists == FR_OK) {
    const auto old = f_stat(previous.c_str(), &info);
    if (old == FR_OK && f_unlink(previous.c_str()) != FR_OK) return STR_NB4_UX_TEMPLATE_ERROR;
    if (old != FR_OK && old != FR_NO_FILE) return STR_NB4_UX_TEMPLATE_ERROR;
    if (f_rename(target.c_str(), previous.c_str()) != FR_OK) return STR_NB4_UX_TEMPLATE_ERROR;
  }
  if (f_rename(temporary.c_str(), target.c_str()) != FR_OK) {
    if (exists == FR_OK) f_rename(previous.c_str(), target.c_str());
    return STR_NB4_UX_TEMPLATE_ERROR;
  }
  return nullptr;
}

namespace {
class TemplatesPage : public BaseDialog {
 public:
  explicit TemplatesPage(std::string folder = TEMPLATES_PATH) :
      BaseDialog(STR_NB4_TEMPLATES, true, lv_disp_get_hor_res(nullptr) - 8,
                 lv_disp_get_ver_res(nullptr) - 12), folder(std::move(folder)) {
    useSectionHeader(); form->padAll(PAD_MEDIUM); build();
    setHelpHandler([] { nb4OpenHelp("settings/models/templates", true); });
  }
  void checkEvents() override {
    BaseDialog::checkEvents();
    if (refresh && !deleted()) { refresh = false; form->clear(); build(); }
  }
 private:
  std::string folder;
  bool refresh = false;
  void text(const char* value) {
    auto label = new StaticText(form, {0, 0, LV_PCT(100), 0}, value, COLOR_THEME_PRIMARY3_INDEX);
    lv_label_set_long_mode(label->getLvObj(), LV_LABEL_LONG_WRAP);
  }
  void button(const std::string& title, std::function<void()> action) {
    auto b = new TextButton(form, {0, 0, LV_PCT(100), 44}, title.c_str(),
                            [action] { action(); return 0; });
    b->setWrap();
  }
  void save(std::string name, bool overwrite) {
    const auto error = nb4SavePersonalTemplate(name.c_str(), overwrite);
    if (error == STR_FILE_EXISTS) {
      new ConfirmDialog(STR_FILE_EXISTS, STR_ASK_OVERWRITE,
                        [this, name] { save(name, true); });
    } else { showError(error); refresh = true; }
  }
  void details(const std::string& file) {
    const std::string path = folder + "/" + file;
    new TemplateDetails(path, personalFolder(folder), [this] { refresh = true; });
  }
  class TemplateDetails : public BaseDialog {
   public:
    TemplateDetails(std::string path, bool personal, std::function<void()> changed) :
        BaseDialog(STR_NB4_TEMPLATES, true, lv_disp_get_hor_res(nullptr) - 8,
                   lv_disp_get_ver_res(nullptr) - 12) {
      useSectionHeader(); form->padAll(PAD_MEDIUM);
      setHelpHandler([] { nb4OpenHelp("settings/models/templates", true); });
      const auto name = path.substr(path.find_last_of('/') + 1);
      new StaticText(form, {0,0,LV_PCT(100),0}, name.c_str(), COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));
      char info[601] = {};
      FIL file;
      const auto description = path.substr(0, path.size() - 4) + TEXT_EXT;
      if (f_open(&file, description.c_str(), FA_READ) == FR_OK) {
        UINT read = 0; f_read(&file, info, sizeof(info) - 1, &read); f_close(&file);
      }
      auto note = new StaticText(form, {0,0,LV_PCT(100),0},
                                 info[0] ? info : STR_NO_INFORMATION);
      lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP);
      new TextButton(form, {0,0,LV_PCT(100),44}, STR_NB4_UX_CREATE_CAR, [this, path] {
        new ConfirmDialog(STR_NB4_UX_CREATE_CAR, STR_NB4_UX_HELP_TEMPLATES, [this, path] {
          const auto error = nb4CreateCarFromTemplate(path.c_str());
          showError(error);
          if (!error) deleteLater();
        });
        return 0;
      });
      if (personal) new TextButton(form, {0,0,LV_PCT(100),44}, STR_NB4_UX_DELETE_TEMPLATE,
        [this, path, changed] {
          new ConfirmDialog(STR_NB4_UX_DELETE_TEMPLATE, path.c_str(), [this, path, changed] {
            if (f_unlink(path.c_str()) != FR_OK) showError(STR_NB4_UX_TEMPLATE_ERROR);
            else { changed(); deleteLater(); }
          });
          return 0;
        });
    }
  };
  void build() {
    text(STR_NB4_UX_HELP_TEMPLATES);
    if (folder == TEMPLATES_PATH && !nb4ModelBlocked())
      button(STR_NB4_UX_SAVE_TEMPLATE, [this] {
        new LabelDialog(g_model.header.name, LEN_MODEL_NAME, STR_NAME,
                        [this](std::string name) { save(name, false); });
      });
    DIR directory;
    const auto error = f_opendir(&directory, folder.c_str());
    if (error != FR_OK) {
      text(error == FR_NO_PATH ? STR_NO_TEMPLATES : STR_NB4_UX_TEMPLATE_ERROR);
      return;
    }
    std::vector<std::pair<std::string, bool>> entries;
    FILINFO info;
    FRESULT result;
    while ((result = f_readdir(&directory, &info)) == FR_OK && info.fname[0]) {
      if (!safePart(info.fname) || (info.fattrib & (AM_HID | AM_SYS))) continue;
      const bool dir = info.fattrib & AM_DIR;
      const auto extension = getFileExtension(info.fname);
      if (dir || (extension && !strcasecmp(extension, YAML_EXT)))
        entries.emplace_back(info.fname, dir);
    }
    f_closedir(&directory);
    if (result != FR_OK) text(STR_NB4_UX_TEMPLATE_ERROR);
    std::sort(entries.begin(), entries.end());
    if (entries.empty()) text(STR_NO_TEMPLATES);
    for (const auto& entry : entries) {
      const auto name = entry.first;
      if (entry.second) button(name + " >", [this, name] { new TemplatesPage(folder + "/" + name); });
      else button(name.substr(0, name.size() - 4), [this, name] { details(name); });
    }
  }
};
}
void nb4OpenTemplates() { new TemplatesPage(); }
#endif
