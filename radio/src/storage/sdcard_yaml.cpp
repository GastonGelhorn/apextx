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

#include "hal/adc_driver.h"
#include "myeeprom.h"
#include "edgetx.h"
#include "edgetx_helpers.h"
#include "storage.h"
#include "sdcard_common.h"
#include "sdcard_yaml.h"
#include "modelslist.h"
#include "nb4_car_state.h"
#include "nb4_model_compat.h"

#include "yaml/yaml_tree_walker.h"
#include "yaml/yaml_parser.h"
#include "yaml/yaml_datastructs.h"
#include "yaml/yaml_bits.h"
#if defined(RADIO_NB4_FAMILY)
#include <atomic>
#include "os/sleep.h"
#endif

const char * readYamlFile(const char* fullpath, const YamlParserCalls* calls, void* parser_ctx, ChecksumResult* checksum_result)
{
    FIL  file;
    UINT bytes_read;
    UINT total_bytes = 0;

    FRESULT result = f_open(&file, fullpath, FA_OPEN_EXISTING | FA_READ);
    if (result != FR_OK) {
        return SDCARD_ERROR(result);
    }

    YamlParser yp; //TODO: move to re-usable buffer
    yp.init(calls, parser_ctx);

    uint16_t calculated_checksum = 0xFFFF;
    uint16_t file_checksum = 0;

    bool first_block = true;
    char buffer[32];
    while ((result = f_read(&file, buffer, sizeof(buffer)-1, &bytes_read)) == FR_OK) {
      if (bytes_read == 0)  // EOF
        break;
      total_bytes += bytes_read;
      buffer[bytes_read] = '\0';

      uint16_t skip = 0;
      if(first_block) {
        // Get the 'checksum' value and skip from further YAML processing
        // The checksum must be first in the first buffer read from file
        first_block = false;
        const char *skipValue = "checksum: ";
        if(strncmp(buffer, skipValue, strlen(skipValue)) == 0) {
          skip = 10;
          char* startPos = buffer + strlen(skipValue);
          char* endPos = startPos;
          // Advance through the value
          while (endPos < buffer + bytes_read && *endPos != '\r' && *endPos != '\n') ++endPos;
          if (endPos == buffer + bytes_read) {
            f_close(&file);
            return SDCARD_ERROR(FR_INT_ERR);
          }
          // Skip trailing newline
          while(endPos < buffer + bytes_read && ((*endPos == '\r') || (*endPos == '\n'))) {
            *endPos = 0;
            endPos++;
          }

          file_checksum = atoi(startPos);
          skip = endPos - buffer;
        }
      }

      // Calculate checksum on read block only if we are called with a pointer to write the resulting checksum
      if (checksum_result != NULL) {
        calculated_checksum = crc16(0, (const uint8_t *)buffer + skip, bytes_read - skip, calculated_checksum);
      }

      if (f_eof(&file)) yp.set_eof();
      auto parsed = yp.parse(buffer + skip, bytes_read - skip);
      if (parsed == YamlParser::STRING_OVERFLOW) { result = FR_INVALID_PARAMETER; break; }
      if (parsed != YamlParser::CONTINUE_PARSING) break;
    }
    auto closeResult = f_close(&file);
    if (result != FR_OK) return SDCARD_ERROR(result);
    if (closeResult != FR_OK) return SDCARD_ERROR(closeResult);

    if (checksum_result != NULL) {
      // Special case to handle "old" files with no checksum field
      // 25 was arbitrarily chosen as the minimum realistic file size
      // - The issue is to allow old files to pass, while still detecting garbled files
      if ( (file_checksum == 0) && (total_bytes > 25) ) {
        *checksum_result = ChecksumResult::Success;
      } else {
        // Normal case - compare read and calculated checksum
        if (calculated_checksum == file_checksum) {
          *checksum_result = ChecksumResult::Success;
        } else {
          *checksum_result = ChecksumResult::Failed;
        }
      }
    }

    return NULL;
}

//
// SDCARD storage interface
//

static const char * attemptLoad(const char *filename, ChecksumResult* checksum_status)
{
  YamlTreeWalker tree;
  tree.reset(get_radiodata_nodes(), (uint8_t*)&g_eeGeneral);
  return readYamlFile(filename, YamlTreeWalker::get_parser_calls(), &tree, checksum_status);
}

#if defined(RADIO_NB4_FAMILY)
static constexpr auto radioPrevious = RADIO_PATH "/radio.yml.previous";

static const char* replaceYamlFile(const char* path, const char* temporary)
{
    const std::string previous = std::string(path) + ".previous";
    FILINFO info;
    auto result = f_stat(path, &info);
    if (result == FR_OK) {
      result = f_unlink(previous.c_str());
      if (result != FR_OK && result != FR_NO_FILE) return SDCARD_ERROR(result);
      result = f_rename(path, previous.c_str());
      if (result != FR_OK) return SDCARD_ERROR(result);
    } else if (result != FR_NO_FILE) {
      return SDCARD_ERROR(result);
    }
    result = f_rename(temporary, path);
    if (result != FR_OK) {
      f_rename(previous.c_str(), path);
      return SDCARD_ERROR(result);
    }
    return nullptr;
}

namespace {
enum SaveState : uint8_t { SaveIdle, SavePending, SaveWriting, SaveDone, SaveFailed };
struct SettingsSave {
  std::atomic<uint8_t> state{SaveIdle};
  std::string bytes, path, temporary;
};
SettingsSave settingsSaves[3]; // radio, model and labels; owned by state transitions
constexpr uint8_t settingsMasks[] = {EE_GENERAL, EE_MODEL, EE_LABELS};

bool collectSettings(void* ctx, const char* text, size_t length)
{
  auto& bytes = *static_cast<std::string*>(ctx);
  if (bytes.size() + length > 128 * 1024) return false;
  bytes.append(text, length);
  return true;
}
}

void nb4PollSettings()
{
  for (unsigned i = 0; i < 3; ++i) {
    auto& save = settingsSaves[i];
    const auto state = save.state.load(std::memory_order_acquire);
    if (state != SaveDone && state != SaveFailed) continue;
    if (state == SaveFailed) storageDirty(settingsMasks[i]);
    save.bytes.clear();
    save.state.store(SaveIdle, std::memory_order_release);
  }
}

uint8_t nb4QueueSettings(uint8_t mask)
{
  nb4PollSettings();
  uint8_t queued = 0;
  for (unsigned i = 0; i < 3; ++i) {
    if (!(mask & settingsMasks[i]) || (i == 1 && nb4ModelBlocked())) continue;
    auto& save = settingsSaves[i];
    if (save.state.load(std::memory_order_acquire) != SaveIdle) continue;
    save.bytes.clear();
    if (i == 2) {
      save.bytes = modelslist.serialize();
      save.path = LABELSLIST_YAML_PATH;
      save.temporary = save.path + ".tmp";
      queued |= settingsMasks[i];
      save.state.store(SavePending, std::memory_order_release);
      continue;
    }
    YamlTreeWalker tree;
    tree.reset(i ? get_modeldata_nodes() : get_radiodata_nodes(),
               i ? (uint8_t*)&g_model : (uint8_t*)&g_eeGeneral);
    if (!tree.generate(collectSettings, &save.bytes)) continue;
    if (!i) {
      const auto crc = crc16(0, (const uint8_t*)save.bytes.data(), save.bytes.size(), 0xffff);
      char header[24]; snprintf(header, sizeof(header), "checksum: %u\r\n", crc);
      save.bytes.insert(0, header);
      save.path = RADIO_SETTINGS_YAML_PATH;
      save.temporary = RADIO_SETTINGS_TMPFILE_YAML_PATH;
      g_eeGeneral.manuallyEdited = false;
    } else {
      save.path = std::string(MODELS_PATH) + "/" + g_eeGeneral.currModelFilename;
      save.temporary = save.path + ".tmp";
      modelslist.updateCurrentModelCell();
      mask |= EE_LABELS;
    }
    queued |= settingsMasks[i];
    save.state.store(SavePending, std::memory_order_release);
  }
  return queued;
}

void nb4WritePendingSettings()
{
  for (auto& save : settingsSaves) {
    uint8_t pending = SavePending;
    if (!save.state.compare_exchange_strong(pending, SaveWriting)) continue;
    FIL file;
    auto result = f_open(&file, save.temporary.c_str(), FA_CREATE_ALWAYS | FA_WRITE);
    bool success = false;
    if (result == FR_OK) {
      UINT written = 0;
      result = f_write(&file, save.bytes.data(), save.bytes.size(), &written);
      const auto closeResult = f_close(&file);
      success = result == FR_OK && written == save.bytes.size() && closeResult == FR_OK;
      if (success) success = !replaceYamlFile(save.path.c_str(), save.temporary.c_str());
    }
    save.state.store(success ? SaveDone : SaveFailed, std::memory_order_release);
  }
}

bool nb4SettingsPending()
{
  for (auto& save : settingsSaves) {
    const auto state = save.state.load();
    if (state == SavePending || state == SaveWriting) return true;
  }
  return false;
}

void nb4FlushSettings()
{
  do {
    nb4WritePendingSettings();
    if (nb4SettingsPending()) sleep_ms(1);
  } while (nb4SettingsPending());
  nb4PollSettings();
}
#endif

const char * loadRadioSettingsYaml(bool checks)
{
    // YAML reader
    TRACE("YAML radio settings reader");

    ChecksumResult checksum_status;
#if defined(RADIO_NB4_FAMILY)
    FILINFO previousInfo;
    if (f_stat(RADIO_SETTINGS_YAML_PATH, &previousInfo) == FR_NO_FILE &&
        f_stat(radioPrevious, &previousInfo) == FR_OK) {
      auto result = f_rename(radioPrevious, RADIO_SETTINGS_YAML_PATH);
      if (result != FR_OK) return SDCARD_ERROR(result);
    }
    // Missing visual fields identify an old file even if defaults were loaded.
    g_eeGeneral.nb4UiVersion = 0;
#endif
    const char* p = attemptLoad(RADIO_SETTINGS_YAML_PATH, &checksum_status);

    if(!checks)
      return p;

    if((p != NULL) || (checksum_status != ChecksumResult::Success) ) {
      // Read failed or checksum check failed
      FRESULT result = FR_OK;
      TRACE("radio settings: Reading failed");
      if ( (p == NULL) && g_eeGeneral.manuallyEdited) {
        // Read sussessfull, checksum failed, manuallyEdited set
        TRACE("File has been manually edited - ignoring checksum mismatch");
        g_eeGeneral.manuallyEdited = 0;
        storageDirty(EE_GENERAL);   // Trigger a save on sucessfull recovery
      } else {
        TRACE("File is corrupted, attempting alternative file");
        f_unlink(RADIO_SETTINGS_ERRORFILE_YAML_PATH);
        result = f_rename(RADIO_SETTINGS_YAML_PATH, RADIO_SETTINGS_ERRORFILE_YAML_PATH); // Save corrupted file for later analysis
        p = attemptLoad(RADIO_SETTINGS_TMPFILE_YAML_PATH, &checksum_status);
#if defined(RADIO_NB4_FAMILY)
        if (p || checksum_status != ChecksumResult::Success) {
          p = attemptLoad(radioPrevious, &checksum_status);
          if (!p && checksum_status == ChecksumResult::Success) {
            auto copyError = sdCopyFile(radioPrevious, RADIO_SETTINGS_TMPFILE_YAML_PATH);
            if (copyError) return copyError;
          }
        }
        if (!p && checksum_status != ChecksumResult::Success) p = SDCARD_ERROR(FR_INT_ERR);
#endif
        if (p == NULL && (checksum_status == ChecksumResult::Success)) {
            f_unlink(RADIO_SETTINGS_YAML_PATH);
            result = f_rename(RADIO_SETTINGS_TMPFILE_YAML_PATH, RADIO_SETTINGS_YAML_PATH);  // Rename previously saved file to active file
            if (result != FR_OK) {
              ALERT(STR_STORAGE_WARNING, STR_RADIO_DATA_UNRECOVERABLE, AU_BAD_RADIODATA);
              return SDCARD_ERROR(result);
            }
        }
        TRACE("Unable to recover radio data");
        ALERT(STR_STORAGE_WARNING, p == NULL ? STR_RADIO_DATA_RECOVERED : STR_RADIO_DATA_UNRECOVERABLE, AU_BAD_RADIODATA);
      }
    }
    #if defined(RADIO_NB4_FAMILY)
    if (!p && g_eeGeneral.nb4UiVersion == 0) {
      // Retain a byte-for-byte copy before the first writer sees new fields.
      // The primary model screen is never rewritten by this migration.
      constexpr auto backup = RADIO_PATH "/radio-pre-car-ui.yml";
      FILINFO info;
      const char* error = nullptr;
      if (f_stat(backup, &info) != FR_OK) {
        constexpr auto temporary = RADIO_PATH "/radio-pre-car-ui.tmp";
        error = sdCopyFile(RADIO_SETTINGS_YAML_PATH, temporary);
        if (!error) {
          auto result = f_rename(temporary, backup);
          if (result != FR_OK) error = SDCARD_ERROR(result);
        }
      }
      if (error) {
        g_eeGeneral.nb4Home = NB4_HOME_PREVIOUS;
        TRACE("NB4 visual migration deferred: %s", error);
      } else {
        // Preserve a selected custom theme; only old default names migrate.
        char theme[SELECTED_THEME_NAME_LEN];
        memcpy(theme, g_eeGeneral.selectedTheme, sizeof(theme));
        nb4VisualDefaults();
        if (theme[0] && strcmp(theme, "EdgeTX Default") && strcmp(theme, "Default"))
          memcpy(g_eeGeneral.selectedTheme, theme, sizeof(theme));
        storageDirty(EE_GENERAL);
      }
    }
    if (!p && g_eeGeneral.nb4UiVersion < NB4_UI_VERSION) {
      constexpr auto backup = RADIO_PATH "/radio-pre-racing-ui-v2.yml";
      constexpr auto temporary = RADIO_PATH "/radio-pre-racing-ui-v2.tmp";
      FILINFO info;
      const char* error = nullptr;
      if (f_stat(backup, &info) != FR_OK) {
        error = sdCopyFile(RADIO_SETTINGS_YAML_PATH, temporary);
        if (!error) {
          auto result = f_rename(temporary, backup);
          if (result != FR_OK) error = SDCARD_ERROR(result);
        }
      }
      if (!error) {
        g_eeGeneral.nb4Home = NB4_HOME_INSTRUMENTS;
        g_eeGeneral.nb4UiVersion = NB4_UI_VERSION;
        storageDirty(EE_GENERAL);
      } else TRACE("ApexTX visual migration deferred: %s", error);
    }
    // NB4 models have one stable surface convention: CH1 starts from the
    // steering wheel and CH2 from the trigger.  The generic EdgeTX channel
    // order remains untouched; only an old radio-wide preference is normalised
    // at the NB4 compatibility boundary before another model can inherit it.
    if (!p && g_eeGeneral.templateSetup != 0) {
      g_eeGeneral.templateSetup = 0;
      storageDirty(EE_GENERAL);
    }
    #endif
    return p;
}

const char * loadRadioSettings()
{
    FILINFO fno;

    if ( (f_stat(RADIO_SETTINGS_YAML_PATH, &fno) != FR_OK) && ((f_stat(RADIO_SETTINGS_TMPFILE_YAML_PATH, &fno) != FR_OK))
#if defined(RADIO_NB4_FAMILY)
         && f_stat(radioPrevious, &fno) != FR_OK
#endif
       ) {
      // If neither the radio configuraion YAML file or the temporary file generated on write exist, this must be a first run with YAML support.
      // - thus requiring a conversion from binary to YAML.
      return "no radio settings";
    }

#if defined(DEFAULT_INTERNAL_MODULE)
    g_eeGeneral.internalModule = DEFAULT_INTERNAL_MODULE;
#endif

    adcCalibDefaults();
    generalDefaultSwitches();
#if defined(COLORLCD)
    g_eeGeneral.defaultKeyShortcuts();
#endif

    const char* error = loadRadioSettingsYaml(true);
    if (!error) {
      g_eeGeneral.chkSum = evalChkSum();
    }
    postRadioSettingsLoad();

    return error;
}

struct yaml_checksummer_ctx {
    FRESULT result;
    uint16_t checksum;
    bool checksum_invalid;
};

static bool yaml_checksummer(void* opaque, const char* str, size_t len)
{
    yaml_checksummer_ctx* ctx = (yaml_checksummer_ctx*)opaque;

    ctx->checksum = crc16(0, (const uint8_t *) str, len, ctx->checksum);
    return true;
}

bool YamlFileChecksum(const YamlNode* root_node, uint8_t* data, uint16_t* checksum)
{
    YamlTreeWalker tree;
    tree.reset(root_node, data);

    yaml_checksummer_ctx ctx;
    ctx.result = FR_OK;
    ctx.checksum = 0xFFFF;
    ctx.checksum_invalid = false;

    if (!tree.generate(yaml_checksummer, &ctx)) {
        if (ctx.result != FR_OK) {
          ctx.checksum_invalid = true;
          return false;
        }
    }

    if(checksum != NULL) {
      *checksum = ctx.checksum;
    }

    return true;
}


struct yaml_writer_ctx {
    FIL*    file;
    FRESULT result;
};

static bool yaml_writer(void* opaque, const char* str, size_t len)
{
    UINT bytes_written;
    yaml_writer_ctx* ctx = (yaml_writer_ctx*)opaque;

#if defined(DEBUG_YAML)
    TRACE_NOCRLF("%.*s",len,str);
#endif

    ctx->result = f_write(ctx->file, str, len, &bytes_written);
    return (ctx->result == FR_OK) && (bytes_written == len);
}

const char* writeFileYaml(const char* path, const YamlNode* root_node, uint8_t* data, uint16_t checksum)
{
    FIL file;

    FRESULT result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        return SDCARD_ERROR(result);
    }
    YamlTreeWalker tree;
    tree.reset(root_node, data);

    yaml_writer_ctx ctx;
    ctx.file = &file;
    ctx.result = FR_OK;

    // Try to add CRC
    if (checksum != 0) {
      const char* p_out = yaml_unsigned2str((int)checksum);
      if (!p_out || !yaml_writer(&ctx, YAMLFILE_CHECKSUM_TAG_NAME, strlen(YAMLFILE_CHECKSUM_TAG_NAME)) ||
          !yaml_writer(&ctx, ": ", 2) || !yaml_writer(&ctx, p_out, strlen(p_out)) ||
          !yaml_writer(&ctx, "\r\n", 2)) {
        f_close(&file);
        return SDCARD_ERROR(ctx.result == FR_OK ? FR_INVALID_PARAMETER : ctx.result);
      }
    }


    if (!tree.generate(yaml_writer, &ctx)) {
        f_close(&file);
        return SDCARD_ERROR(ctx.result == FR_OK ? FR_INVALID_PARAMETER : ctx.result);
    }

    result = f_close(&file);
    return result == FR_OK ? nullptr : SDCARD_ERROR(result);
}

const char * writeGeneralSettings()
{
#if defined(RADIO_NB4_FAMILY)
    nb4FlushSettings();
#endif
    TRACE("YAML radio settings writer");
    uint16_t file_checksum = 0;

    YamlFileChecksum(get_radiodata_nodes(), (uint8_t*)&g_eeGeneral, &file_checksum);
    g_eeGeneral.manuallyEdited = false;

    const char *p = writeFileYaml(RADIO_SETTINGS_TMPFILE_YAML_PATH, get_radiodata_nodes(),
                         (uint8_t*)&g_eeGeneral, file_checksum);
    TRACE("generalSettings written with checksum %u", file_checksum);

    if (p != NULL) {
        return p;
    }
#if defined(RADIO_NB4_FAMILY)
    return replaceYamlFile(RADIO_SETTINGS_YAML_PATH, RADIO_SETTINGS_TMPFILE_YAML_PATH);
#else
    f_unlink(RADIO_SETTINGS_YAML_PATH);

    FRESULT result = f_rename(RADIO_SETTINGS_TMPFILE_YAML_PATH, RADIO_SETTINGS_YAML_PATH);
    if(result != FR_OK)
        return SDCARD_ERROR(result);

    return nullptr;
#endif
}


const char * readModelYaml(const char * filename, uint8_t * buffer, uint32_t size, const char* pathName)
{
    // YAML reader
    TRACE("YAML model reader");

    bool init_model = true;
    const YamlNode* data_nodes = nullptr;
    if (size == sizeof(g_model)) {
        data_nodes = get_modeldata_nodes();
    }
    else if (size == sizeof(PartialModel)) {
        data_nodes = get_partialmodel_nodes();
        init_model = false;
    }
    else {
        TRACE("cannot find YAML data nodes for object size (size=%d)", size);
        return "YAML size error";
    }

    char path[256];
    getModelPath(path, filename, pathName);

    YamlTreeWalker tree;
    tree.reset(data_nodes, buffer);

    // wipe memory before reading YAML
    memset(buffer,0,size);

    if (init_model) {
#if defined(FUNCTION_SWITCHES)
      extern void initCustomSwitches();
      initCustomSwitches();
#endif
#if defined(COLORLCD)
      g_model.resetScreenData();
#endif
      auto md = reinterpret_cast<ModelData*>(buffer);
#if defined(FLIGHT_MODES) && defined(GVARS)
      // reset GVars to default values
      // Note: taken from edgetx.cpp::modelDefault()
      //TODO: new func in gvars
      for (int p=1; p<MAX_FLIGHT_MODES; p++) {
        for (int i=0; i<MAX_GVARS; i++) {
          md->flightModeData[p].gvars[i] = GVAR_MAX+1;
        }
      }
#endif
      // is that necessary ???
      // md->swashR.collectiveWeight = 100;
      // md->swashR.aileronWeight    = 100;
      // md->swashR.elevatorWeight   = 100;

      md->rfAlarms.warning = 45;
      md->rfAlarms.critical = 42;
    }

    return readYamlFile(path, YamlTreeWalker::get_parser_calls(), &tree, NULL);
}

static const char _wrongExtentionError[] = "wrong file extension";

const char* readModel(const char* filename, uint8_t* buffer, uint32_t size, const char* pathName)
{
  const char* ext = strrchr(filename, '.');
  if (!ext || strncmp(ext, YAML_EXT, 4) != 0) {
    return _wrongExtentionError;
  }

  return readModelYaml(filename, buffer, size, pathName);
}

const char * writeModelYaml(const char* filename)
{
#if defined(RADIO_NB4_FAMILY)
    nb4FlushSettings();
#endif
#if defined(RADIO_NB4_FAMILY)
    if (nb4ModelBlocked()) return nb4ModelCompatibilityIssue();
#endif
    TRACE("YAML model writer");
    char path[256];
    getModelPath(path, filename);
#if defined(RADIO_NB4_FAMILY)
    const std::string temporary = std::string(path) + ".tmp";
    auto error = writeFileYaml(temporary.c_str(), get_modeldata_nodes(), (uint8_t*)&g_model, 0);
    if (error) return error;
    return replaceYamlFile(path, temporary.c_str());
#else
    return writeFileYaml(path, get_modeldata_nodes(), (uint8_t*)&g_model,0 );
#endif
}

#if !defined(STORAGE_MODELSLIST)
// EEPROM slot simulation based on file names:
// - /MODELS/model[00-99].yml

void getModelNumberStr(uint8_t idx, char* model_idx)
{
  memcpy(model_idx, MODEL_FILENAME_PREFIX, sizeof(MODEL_FILENAME_PREFIX));
  model_idx[sizeof(MODEL_FILENAME_PREFIX)-1] = '0' + idx / 10;
  model_idx[sizeof(MODEL_FILENAME_PREFIX)]   = '0' + idx % 10;
  model_idx[sizeof(MODEL_FILENAME_PREFIX)+1] = '\0';
}
#endif

const char * writeModel()
{
#if defined(STORAGE_MODELSLIST)
  return writeModelYaml(g_eeGeneral.currModelFilename);
#else
  char fname[MODELIDX_STRLEN + sizeof(YAML_EXT)];
  getModelNumberStr(g_eeGeneral.currModel, fname);
  strcat(fname, YAML_EXT);
  return writeModelYaml(fname);
#endif
}

#if !defined(STORAGE_MODELSLIST)
void loadModelHeader(uint8_t id, ModelHeader* header)
{
  PartialModel partial;
  memclear(&partial, sizeof(PartialModel));

  if (modelExists(id)) {
    char fname[MODELIDX_STRLEN + sizeof(YAML_EXT)];
    getModelNumberStr(id, fname);
    strcat(fname, YAML_EXT);
    readModelYaml(fname, reinterpret_cast<uint8_t*>(&partial), sizeof(partial));
    memcpy(header, &partial, sizeof(ModelHeader));
  }
}

const char * loadModel(uint8_t idx, bool alarms)
{
  char fname[MODELIDX_STRLEN + sizeof(YAML_EXT)];
  getModelNumberStr(idx, fname);
  strcat(fname, YAML_EXT);
  return loadModel(fname, alarms);
}

bool modelExists(uint8_t idx)
{
  char model_idx[MODELIDX_STRLEN];
  getModelNumberStr(idx, model_idx);
  GET_FILENAME(fname, MODELS_PATH, model_idx, YAML_EXT);

  FILINFO fno;
  return f_stat(fname, &fno) == FR_OK;
}

bool copyModel(uint8_t dst, uint8_t src)
{
  // TODO: overwrite possible?
  char model_idx_src[MODELIDX_STRLEN];
  char model_idx_dst[MODELIDX_STRLEN];
  getModelNumberStr(src, model_idx_src);
  getModelNumberStr(dst, model_idx_dst);

  GET_FILENAME(fname_src, MODELS_PATH, model_idx_src, YAML_EXT);
  GET_FILENAME(fname_dst, MODELS_PATH, model_idx_dst, YAML_EXT);

  if (sdCopyFile(fname_src, fname_dst) == nullptr) {
    // update headers
    memcpy(&modelHeaders[dst], &modelHeaders[src], sizeof(ModelHeader));
    return true;
  }

  return false;
}

static void swapModelHeaders(uint8_t id1, uint8_t id2)
{
  char tmp[sizeof(g_model.header)];
  memcpy(tmp, &modelHeaders[id1], sizeof(ModelHeader));
  memcpy(&modelHeaders[id1], &modelHeaders[id2], sizeof(ModelHeader));
  memcpy(&modelHeaders[id2], tmp, sizeof(ModelHeader));
}

void swapModels(uint8_t id1, uint8_t id2)
{
  char model_idx_1[MODELIDX_STRLEN];
  char model_idx_2[MODELIDX_STRLEN];
  getModelNumberStr(id1, model_idx_1);
  getModelNumberStr(id2, model_idx_2);

  GET_FILENAME(fname1, MODELS_PATH, model_idx_1, YAML_EXT);
  GET_FILENAME(fname1_tmp, MODELS_PATH, model_idx_1, ".tmp");
  GET_FILENAME(fname2, MODELS_PATH, model_idx_2, YAML_EXT);

  FILINFO fno;
  if (f_stat(fname2,&fno) != FR_OK) {
    if (f_stat(fname1,&fno) == FR_OK) {
      if (f_rename(fname1, fname2) == FR_OK)
        swapModelHeaders(id1,id2);
    }
    return;
  }

  if (f_stat(fname1,&fno) != FR_OK) {
    f_rename(fname2, fname1);
    return;
  }

  // just in case...
  f_unlink(fname1_tmp);

  if (f_rename(fname1, fname1_tmp) != FR_OK) {
    TRACE("Error renaming 1");
    return;
  }

  if (f_rename(fname2, fname1) != FR_OK) {
    TRACE("Error renaming 2");
    return;
  }

  if (f_rename(fname1_tmp, fname2) != FR_OK) {
    TRACE("Error renaming 1 tmp");
    return;
  }

  swapModelHeaders(id1,id2);
}

int8_t deleteModel(uint8_t idx)
{
  char model_idx[MODELIDX_STRLEN];
  getModelNumberStr(idx, model_idx);
  GET_FILENAME(fname, MODELS_PATH, model_idx, YAML_EXT);

  if (f_unlink(fname) != FR_OK) {
    return -1;
  }

  modelHeaders[idx].name[0] = '\0';
  return 0;
}

const char * backupModel(uint8_t idx)
{
  char * buf = reusableBuffer.modelsel.mainname;

  // check and create folder here
  const char * error = sdCheckAndCreateDirectory(BACKUP_PATH);
  if (error) {
    return error;
  }

  strncpy(buf, modelHeaders[idx].name, sizeof(g_model.header.name));
  buf[sizeof(g_model.header.name)] = '\0';

  int8_t i = sizeof(g_model.header.name)-1;
  uint8_t len = 0;
  while (i > 0) {
    if (!len && buf[i])
      len = i+1;
    if (len) {
      if (!buf[i])
        buf[i] = '_';
    }
    i--;
  }

  if (len == 0) {
    uint8_t num = idx + 1;
    char* s = strAppend(buf, STR_MODEL);
    strAppendUnsigned(s, num, 2);
    len = strlen(buf);
  }

#if defined(RTCLOCK)
  char * tmp = strAppendDate(&buf[len]);
  len = tmp - buf;
#endif

  strcpy(&buf[len], YAML_EXT);

#ifdef SIMU
  TRACE("SD-card backup filename=%s", buf);
#endif

  char model_idx[MODELIDX_STRLEN + sizeof(YAML_EXT)];
  getModelNumberStr(idx, model_idx);
  strcat(model_idx, YAML_EXT);

  return sdCopyFile(model_idx, MODELS_PATH, buf, BACKUP_PATH);
}

const char * restoreModel(uint8_t idx, char *model_name)
{
  char * buf = reusableBuffer.modelsel.mainname;
  strcpy(buf, model_name);
  strcpy(&buf[strlen(buf)], YAML_EXT);

  char model_idx[MODELIDX_STRLEN + sizeof(YAML_EXT)];
  getModelNumberStr(idx, model_idx);
  strcat(model_idx, YAML_EXT);

  const char* error = sdCopyFile(buf, BACKUP_PATH, model_idx, MODELS_PATH);
  if (!error) {
    loadModelHeader(idx, &modelHeaders[idx]);
  }

  return error;
}

#endif

bool storageReadRadioSettings(bool checks)
{
  if (!sdMounted()) sdInit();
  return loadRadioSettingsYaml(checks) == nullptr;
}
