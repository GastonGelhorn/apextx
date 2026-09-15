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

#include "afhds3.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_latency.h"
#endif
#include "afhds3_transport.h"
#include "afhds3_config.h"

#include "pulses.h"

#include "../debug.h"
#include "../definitions.h"

#include "telemetry/telemetry.h"
#include "telemetry/flysky_ibus2.h"
#include "mixer_scheduler.h"
#include "hal/module_driver.h"
#include "hal/module_port.h"

#if defined(RADIO_NB4)
#include "targets/pl18/nb4_rf_controller.h"
#endif

#define SET_DIRTY() storageDirty(EE_MODEL)

#define checkDirtyFlag(dirtyCmd) (cfg->others.dirtyFlag & ((uint32_t) 1 << dirtyCmd))

#define clearDirtyFlag(dirtyCmd) (cfg->others.dirtyFlag &= ~((uint32_t) 1 << dirtyCmd))

#define FAILSAFE_HOLD 1
#define FAILSAFE_CUSTOM 2

#define FAILSAFE_HOLD_VALUE         0x8000
#define FAILSAFE_NOPULSES_VALUE     0x8001

#define MAX_NO_OF_MODELS            20

#if defined(RADIO_NB4)
// Backoff before a failed NB4 handshake is retried, in 10 ms ticks.
#define NB4_FAIL_RETRY_DELAY        200
#endif

extern uint16_t  sns_RFCurrentPower;

//get channel value outside of afhds3 namespace
int32_t getChannelValue(uint8_t channel);
void processFlySkyAFHDS3Sensor(const uint8_t * packet, uint8_t type);
void processFlySkySensor(const uint8_t * packet, uint8_t type);

#if defined(RADIO_NB4)
#include "afhds3_nb4.h"
static const char* nb4RfText(const char* es, const char* en) { return g_eeGeneral.uiLanguage[0] == 'e' && g_eeGeneral.uiLanguage[1] == 's' ? es : en; }
#endif

namespace afhds3
{

static uint8_t _phyMode_channels[] = {
  18, // CLASSIC_FLCR1_18CH
  10, // CLASSIC_FLCR6_10CH
  18, // ROUTINE_FLCR1_18CH
  8,  // ROUTINE_FLCR6_8CH
  12, // ROUTINE_LORA_12CH
};

// enum COMMAND_DIRECTION
// {
//   RADIO_TO_MODULE = 0,
//   MODULE_TO_RADIO = 1
// };

// enum DATA_TYPE
// {
//   READY_DT,  // 8 bytes 0x01 Not ready 0x02 Ready
//   STATE_DT,  // See MODULE_STATE
//   MODE_DT,
//   MOD_CONFIG_DT,
//   CHANNELS_DT,
//   TELEMETRY_DT,
//   MODULE_POWER_DT,
//   MODULE_VERSION_DT,
//   EMPTY_DT,
// };

//Product number
#define PN_FRM301       ( (0x1234<<16) | 0x0003 )
#define PN_FRM004       ( (0x1234<<16) | 0x0007 )
#define PN_FRM005       ( (0x1234<<16) | 0x0009 )

#define PN_FTR10 	      ( (0x1234<<16) | 0x0002 )
#define PN_FGR4  	      ( (0x1234<<16) | 0x0004 )

#define PN_FTR4 	      ( (0x1234<<16) | 0x0006 )
#define PN_FGR4S 	      ( (0x1234<<16) | 0x0006 )
#define PN_FGR4P 	      ( (0x1234<<16) | 0x0006 )

#define PN_FTR16S       ( (0x0001<<16) | 0x0103 )
//#define PN_MINIZ        ( (0x0001<<16) | 0x0104 )

#define PN_FTR8B        ( (0x0001<<16) | 0x0105 )
#define PN_FTR12B       ( (0x0001<<16) | 0x0106 )
#define PN_FGR8B        ( (0x0001<<16) | 0x0107 )
#define PN_FGR12B       ( (0x0001<<16) | 0x0108 )
#define PN_GMR          ( (0x0001<<16) | 0x0109 )
#define PN_TMR          ( (0x0001<<16) | 0x010A )
#define PN_INR4_GYB     ( (0x0001<<16) | 0x010B )
#define PN_INR6_HS      ( (0x0001<<16) | 0x010C )
#define PN_FTR4B        ( (0x0001<<16) | 0x010D )
#define PN_FGR4B        ( (0x0001<<16) | 0x010E )
#define PN_FBR12        ( (0x0001<<16) | 0x010F )
#define PN_INR6_FC      ( (0x0001<<16) | 0x0110 )
#define PN_TR8B         ( (0x0001<<16) | 0x0111 )
#define PN_FBR8         ( (0x0001<<16) | 0x0112 )
#define PN_FBR4         ( (0x0001<<16) | 0x0113 )

// enum used by command response -> translate to ModuleState
enum MODULE_READY_E {
  MODULE_STATUS_UNKNOWN = 0x00,
  MODULE_STATUS_NOT_READY = 0x01,
  MODULE_STATUS_READY = 0x02
};

enum ModuleState {
  STATE_NOT_READY = 0x00,     // virtual, module not ready
  STATE_HW_ERROR = 0x01,
  STATE_BINDING = 0x02,
  STATE_SYNC_RUNNING = 0x03,  // sync state, ready to sync settings, receiver not connected
  STATE_SYNC_DONE = 0x04,     // sync state, ready to sync settings, receiver connected
  STATE_STANDBY = 0x05,       // standby state, ready to update modelID, and switch to RUN mode
  STATE_UPDATING_WAIT = 0x06,
  STATE_UPDATING_MOD = 0x07,
  STATE_UPDATING_RX = 0x08,
  STATE_UPDATING_RX_FAILED = 0x09,
  STATE_RF_TESTING = 0x0a,
  STATE_READY = 0x0b,         // virtual, module ready awaiting to query module state
  STATE_HW_TEST = 0xff,
};

// used for set command
enum MODULE_MODE_E {
  STANDBY = 0x01,
  BIND = 0x02,  // after bind module will enter run mode
  RUN = 0x03,
  RX_UPDATE = 0x04,  // after successful update module will enter standby mode,
                     // otherwise hw error will be raised
  MODULE_MODE_UNKNOWN = 0xFF
};

enum CMD_RESULT {
  FAILURE = 0x01,
  SUCCESS = 0x02,
};

enum CHANNELS_DATA_MODE {
  CHANNELS = 0x01,
  FAIL_SAFE = 0x02,
};

PACK(struct ChannelsData {
  uint8_t mode;
  uint8_t channelsNumber;
  int16_t data[AFHDS3_MAX_CHANNELS];
});

union ChannelsData_u {
  ChannelsData data;
  uint8_t buffer[sizeof(ChannelsData)];
};

PACK(struct TelemetryData {
  uint8_t sensorType;
  uint8_t length;
  uint8_t type;
  uint8_t semsorID;
  uint8_t data[8];
});

enum MODULE_POWER_SOURCE {
  INTERNAL = 0x01,
  EXTERNAL = 0x02,
};

enum DeviceAddress {
  TRANSMITTER = 0x01,
  FRM303 = 0x04,
  IRM301 = 0x05,
};

PACK(struct ModuleVersion
{
  uint16_t companyNumber;
  uint32_t txID;
  uint32_t rxID;
  uint32_t productNumber;
  uint32_t hardwareVersion;
  uint32_t bootloaderVersion;
  uint32_t firmwareVersion;
  uint32_t rfVersion;
});

PACK(struct ReceiverVersion
{
uint32_t ProductNumber;
uint16_t MainboardVersion;
uint16_t RFModuleVersion;
uint16_t BootloaderVersion;
uint16_t FirmwareVersion;
uint16_t RFLibraryVersion;
} );

PACK(struct CommandResult_s {
  uint16_t command;
  uint8_t result;
  uint8_t respLen;
});

union AfhdsFrameData {
  uint8_t value;
  // Config_s Config;
  ChannelsData Channels;
  TelemetryData Telemetry;
  ModuleVersion Version;
  CommandResult_s CommandResult;
};

static constexpr uint16_t rfpowerTable[7] = {14*4, 17*4, 20*4, 25*4, 27*4, 30*4, 33*4 };

#define FRM302_STATUS 0x56

uint8_t receiver_type( unsigned long productnumber );
class ProtoState
{
  public:
    /**
    * Initialize class for operation
    * @param moduleIndex index of module one of INTERNAL_MODULE, EXTERNAL_MODULE
    * @param resetFrameCount flag if current frame count should be reseted
    */
   void init(uint8_t moduleIndex, void* buffer, etx_module_state_t* mod_st,
             uint8_t fAddr);

   /**
    * Fills DMA buffers with frame to be send depending on actual state
    */
   void setupFrame();

   /**
    * Sends prepared buffers
    */
   void sendFrame()
   {
#if defined(RADIO_NB4)
     if (hardFaulted) return;
#endif
     trsp.sendBuffer();
#if defined(RADIO_NB4_FAMILY)
     // The module port owns the bytes from here on.
     nb4LatencySent();
#endif
   }

   /**
    * Gets actual module status into provided buffer
    * @param statusText target buffer for status
    */
   void getStatusString(char* statusText) const;

   bool isConnected();

   /**
    * Sends stop command to prevent any further module operations
    */
   void stop();

   Config_u* getConfig() { return &cfg; }

   void applyConfigFromModel();

   bool fifoFull() { return trsp.fifoFull(); }
#if defined(RADIO_NB4) && defined(SIMU)
   void getDiagnostics(Nb4TransportDiagnostics& result) const {
     result = trsp.getDiagnostics();
     result.receiverStored = nb4Config.bound;
     result.receiverLearned = nb4ReceivedConfig;
     result.twoWay = moduleData && moduleData->afhds3.telemetry;
   }
#endif
#if defined(RADIO_NB4)
   BindPhase getBindPhase();
   Nb4ReceiverTelemetry receiverTelemetry;
#endif
   uint16_t RFCurrentPower;

  protected:

    void resetConfig(uint8_t version);

  private:
    //friendship declaration - use for passing telemetry
    friend void processTelemetryData(void* ctx, uint8_t data, uint8_t* buffer, uint8_t* len);

    void processTelemetryData(uint8_t data, uint8_t* buffer, uint8_t* len);

    void parseData(uint8_t* rxBuffer, uint8_t rxBufferCount);

    void setState(ModuleState state);

    bool syncSettings();

    bool sensorCalibration();

  //  void requestInfoAndRun(bool send = false);

    uint8_t setFailSafe(int16_t* target, uint8_t rfchannelcount=AFHDS3_MAX_CHANNELS);

    inline int16_t convert(int channelValue);

    void sendChannelsData();

    void clearFrameData();

    bool hasTelemetry();

    Transport trsp;

#if defined(RADIO_NB4)
    bool hardFaulted;
    enum class Nb4Stage : uint8_t { Ready, Standby, Pages, Bind, Run, Active, Failed };
    Nb4Stage nb4Stage = Nb4Stage::Ready;
    Nb4RfConfig nb4Config;
    uint8_t nb4Page = 0;
    uint8_t nb4Pending = 0;
    uint8_t nb4ReadyTries = 0;
    uint32_t nb4AppliedModelKey = 0;
    bool nb4Binding = false;
    bool nb4BindMode = false;
    bool nb4ReceivedConfig = false;
    tmr10ms_t nb4LastPoll = 0;
    tmr10ms_t nb4BindStarted = 0;
    tmr10ms_t nb4FailedAt = 0;
    const char* nb4Error = nullptr;
    void setupNb4Frame();
    bool parseNb4Data(const AfhdsFrame* frame, uint8_t length);
    void parseNb4Telemetry(const uint8_t* data, unsigned size);
    void nb4Request(COMMAND command, uint8_t* payload = nullptr, uint8_t size = 0,
                    FRAME_TYPE type = REQUEST_GET_DATA);
    void nb4Fail(const char* message);

#endif

    /**
     * Index of the module
     */
    uint8_t module_index;

    /**
     * Reported state of the HF module
     */
    ModuleState state;

    //bool modelIDSet;
    //bool modelcfgGet;
    uint8_t modelID;
    bool rx_state; //false:disconnect; true:connect

    /**
     * Command count used for counting actual number of commands sent in run mode
     */
    uint32_t cmdCount;

    /**
     * Command index of command to be send when cmdCount reached necessary value
     */
    uint32_t cmdIndex;

    /**
     * Pointer to module config - it is making operations easier and faster
     */
    ModuleData* moduleData;

    /**
     * Actual module configuration - must be requested from module
     */
    Config_u cfg;

    /**
     * Actual module version - must be requested from module
     */
    ModuleVersion version;
    ReceiverVersion rx_version;
};

static const char* const moduleStateText[] =
{
  "Not ready",
  "HW Error",
  "Binding",
  "Disconnected",
  "Connected",
  "Standby",
  "Waiting for update",
  "Updating",
  "Updating RX",
  "Updating RX failed",
  "Testing",
  "Ready",
  "HW test"
};

static const COMMAND periodicRequestCommands[] =
{
  COMMAND::MODULE_STATE,
  // COMMAND::MODULE_GET_CONFIG,
  COMMAND::VIRTUAL_FAILSAFE // One way failsafe
};

static const uint16_t AFHDS3_POWER[] = {56, 68, 80, 96, 108, 120, 132};

//Static collection of afhds3 object instances by module
static ProtoState protoState[MAX_MODULES];
bool containsData(FRAME_TYPE frameType);

void getStatusString(uint8_t module, char* buffer)
{
  return protoState[module].getStatusString(buffer);
}

bool isConnected(uint8_t module)
{
  return protoState[module].isConnected();
}

#if defined(RADIO_NB4)
BindPhase getBindPhase(uint8_t module)
{
  return module < MAX_MODULES ? protoState[module].getBindPhase() : BindPhase::Failed;
}

BindPhase ProtoState::getBindPhase()
{
  // Opening the dialog precedes the mixer consuming the new bind mode. A
  // connection or error from the previous operation must not finish this one.
  if (getModuleMode(module_index) != MODULE_MODE_BIND || !nb4Binding)
    return BindPhase::Preparing;
  if (nb4Error || hardFaulted || nb4::Nb4RfController::getFault() != nb4::Nb4RfFault::None)
    return BindPhase::Failed;
  if (isConnected()) return BindPhase::Connected;
  if (nb4ReceivedConfig)
    return moduleData->afhds3.telemetry ? BindPhase::Confirming : BindPhase::ManualFinish;
  return nb4Stage == Nb4Stage::Active ? BindPhase::Searching : BindPhase::Preparing;
}

#if defined(SIMU)
void getDiagnostics(uint8_t module, Nb4TransportDiagnostics& result)
{
  result = {};
  if (module < MAX_MODULES) protoState[module].getDiagnostics(result);
}
#endif

Nb4ReceiverTelemetry getReceiverTelemetry(uint8_t module)
{
  return module < MAX_MODULES ? protoState[module].receiverTelemetry : Nb4ReceiverTelemetry{};
}

void resetReceiverTelemetry()
{
  for (auto& protocol : protoState) protocol.receiverTelemetry = {};
}

#endif

//friends function that can access telemetry parsing method
void processTelemetryData(void* ctx, uint8_t data, uint8_t* buffer, uint8_t* len)
{
  auto mod_st = (etx_module_state_t*)ctx;
  auto p_state = (ProtoState*)mod_st->user_data;
  if (p_state) p_state->processTelemetryData(data, buffer, len);
}

void ProtoState::getStatusString(char* buffer) const
{
#if defined(RADIO_NB4)
  switch (nb4::Nb4RfController::getFault()) {
    case nb4::Nb4RfFault::UartError:
      strcpy(buffer, nb4RfText("Error de UART RF", "RF UART error")); return;
    case nb4::Nb4RfFault::UartInitFailed:
      strcpy(buffer, nb4RfText("No se pudo iniciar UART RF", "RF UART initialization failed")); return;
    case nb4::Nb4RfFault::UnqualifiedProfile:
      strcpy(buffer, nb4RfText("Perfil RF no disponible", "RF profile unavailable")); return;
    case nb4::Nb4RfFault::InvalidElectricalSequence:
      strcpy(buffer, nb4RfText("Error de activación RF", "RF activation error")); return;
    default: break;
  }
  if (nb4Error) { strcpy(buffer, nb4Error); return; }
  if (nb4Stage == Nb4Stage::Pages) {
    snprintf(buffer, 64, nb4RfText("Configurando RF %u/4", "Configuring RF %u/4"), nb4Page + 1);
    return;
  }
  if (nb4Binding && !nb4ReceivedConfig && state == STATE_SYNC_DONE) {
    strcpy(buffer, nb4RfText("Guardando receptor", "Saving receiver")); return;
  }
  if (nb4Binding && nb4ReceivedConfig && !moduleData->afhds3.telemetry) {
    strcpy(buffer, nb4RfText("Receptor guardado. Finaliza.", "Receiver saved. Finish.")); return;
  }
  if (nb4Stage == Nb4Stage::Active && !nb4Binding && !nb4Config.bound) {
    strcpy(buffer, nb4RfText("Sin receptor enlazado", "No bound receiver")); return;
  }
#endif

#if defined(RADIO_NB4_FAMILY)
  if (g_eeGeneral.uiLanguage[0] == 'e' && g_eeGeneral.uiLanguage[1] == 's') {
    static const char* const spanish[] = {"Sin preparar", "Error hardware", "Vinculando", "Desconectado", "Conectado", "En espera",
      "Espera de firmware", "Actualizando", "Actualizando RX", "Fallo al actualizar RX", "Probando", "Preparado"};
    strcpy(buffer, state <= ModuleState::STATE_READY ? spanish[state] : "Desconocido"); return;
  }
#endif
  strcpy(buffer, state <= ModuleState::STATE_READY ? moduleStateText[state]
                                                   : "Unknown");
}

void ProtoState::processTelemetryData(uint8_t byte, uint8_t* buffer, uint8_t* len)
{
  uint8_t maxSize = TELEMETRY_RX_PACKET_SIZE;
  if (!trsp.processTelemetryData(byte, buffer, *len, maxSize))
    return;

  parseData(buffer, *len);
  *len = 0;
}

bool ProtoState::isConnected()
{
#if defined(RADIO_NB4)
  if (nb4Error || !nb4Config.bound || nb4Stage != Nb4Stage::Active ||
      (nb4Binding && (!nb4ReceivedConfig || !moduleData->afhds3.telemetry))) return false;
#endif
  return this->state == ModuleState::STATE_SYNC_DONE;
}

bool ProtoState::hasTelemetry()
{
  if (cfg.version == 0)
    return cfg.v0.IsTwoWay;
  else
    return cfg.v1.IsTwoWay;
}

uint8_t ibus_type[SES_NPT_NB_MAX_PORTS] = {SES_NPT_IBUS1_IN};
void setIbusType(uint8_t* ibus_type_buf)
{
  for(uint8_t i = 0; i< SES_NPT_NB_MAX_PORTS; i++) {
   if (ibus_type_buf[i] == afhds3::SES_NPT_IBUS2 || ibus_type_buf[i] == afhds3::SES_NPT_IBUS2_HUB_PORT) {
    ibus_type[i] = afhds3::SES_NPT_IBUS2;
   } else {
    ibus_type[i] = afhds3::SES_NPT_IBUS1_IN;
   }
  }
}

void ProtoState::setupFrame()
{
#if defined(RADIO_NB4)
  if (nb4::Nb4RfController::servicePendingFault()) {
    hardFaulted = true;
    mixerSchedulerSetPeriod(module_index, 0);
    return;
  }
#endif
#if defined(RADIO_NB4)
  setupNb4Frame();
  return;
#endif
  bool trsp_error = false;
  if (trsp.handleRetransmissions(trsp_error)) return;

  if (trsp_error) {
    // A missing response restarts the AFHDS3 handshake on the same transport.
    // It is normal while the receiver is off and must not permanently disable
    // RF or require a cold boot. Hardware UART/DMA errors are handled separately.
    this->state = ModuleState::STATE_NOT_READY;
    clearFrameData();
  }

  if (this->state == ModuleState::STATE_NOT_READY) {
    trsp.putFrame(COMMAND::MODULE_READY, FRAME_TYPE::REQUEST_GET_DATA);
    return;
  }

  // Process backlog, not check states
  if (trsp.processQueue()) return;

  ::ModuleSettingsMode moduleMode = getModuleMode(module_index);

  if (moduleMode == ::ModuleSettingsMode::MODULE_MODE_BIND) {
    if (state != STATE_BINDING) {
      applyConfigFromModel();

      trsp.putFrame(COMMAND::MODULE_SET_CONFIG,
                     FRAME_TYPE::REQUEST_SET_EXPECT_DATA, cfg.buffer,
                     cfg.version == 0 ? sizeof(cfg.v0) : sizeof(cfg.v1));

      trsp.enqueue(COMMAND::MODULE_MODE, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, true,
                   (uint8_t)MODULE_MODE_E::BIND);
      return;
    }
  }
  else if (moduleMode == ::ModuleSettingsMode::MODULE_MODE_RANGECHECK) {
    TRACE("AFHDS3 [RANGE CHECK] not supported");
  }
  else if (moduleMode == ::ModuleSettingsMode::MODULE_MODE_NORMAL) {

    // If module is ready but not started
    if (this->state == ModuleState::STATE_READY) {
      trsp.putFrame(MODULE_STATE, FRAME_TYPE::REQUEST_GET_DATA);
      return;
    }

    // ModelID change detection, if changed => STATE_STANDBY => Set ModelID => RUN mode => STATE_SYNC_XXX
    uint8_t newModelID = g_model.header.modelId[module_index] % MAX_NO_OF_MODELS;  // Rotate the model ID when exceed no. of stored models
    if (modelID != newModelID)
    {
      if (this->state != ModuleState::STATE_STANDBY) {
        auto mode = (uint8_t)MODULE_MODE_E::STANDBY;
        trsp.putFrame(COMMAND::MODULE_MODE, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, &mode, 1);
        return;
      } else {
        modelID = newModelID;
        trsp.putFrame(COMMAND::MODEL_ID, FRAME_TYPE::REQUEST_SET_EXPECT_DATA,
                       &modelID, 1);
        return;
      }
    }

    // If standby or exit bind =>  RUN mode => STATE_SYNC_XXX
    if (this->state == ModuleState::STATE_STANDBY || this->state == ModuleState::STATE_BINDING) {
      cmdCount = 0;
//      requestInfoAndRun(true);
      auto mode = (uint8_t)MODULE_MODE_E::RUN;
      trsp.putFrame(COMMAND::MODULE_MODE, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, &mode, 1);
      return;
    }

    // Exit bind
/*    if (this->state == STATE_BINDING) {
      TRACE("AFHDS3 [EXIT BIND]");
//      modelcfgGet = true;
      auto mode = (uint8_t)MODULE_MODE_E::RUN;
      trsp.putFrame(COMMAND::MODULE_MODE, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, &mode, 1);
      trsp.enqueue(COMMAND::MODULE_GET_CONFIG, FRAME_TYPE::REQUEST_GET_DATA);
      return;
    } */
  }

/*  if (modelcfgGet) {
    trsp.enqueue(COMMAND::MODULE_GET_CONFIG, FRAME_TYPE::REQUEST_GET_DATA);
    return;
  }*/

  if (cmdCount++ >= 150) {

    cmdCount = 0;
    if (cmdIndex >= sizeof(periodicRequestCommands)) {
      cmdIndex = 0;
    }
    COMMAND cmd = periodicRequestCommands[cmdIndex++];

    if (cmd == COMMAND::VIRTUAL_FAILSAFE) {
      Config_u* cfg = this->getConfig();
      uint8_t len =_phyMode_channels[cfg->v0.PhyMode];
#if defined(RADIO_NB4)
      len = min<uint8_t>(len, sentModuleChannels(module_index));
#endif
      if (!hasTelemetry()) {
          uint16_t failSafe[AFHDS3_MAX_CHANNELS + 1] = {
          ((AFHDS3_MAX_CHANNELS << 8) | CHANNELS_DATA_MODE::FAIL_SAFE), 0};
#if defined(RADIO_NB4)
          failSafe[0] = (len << 8) | CHANNELS_DATA_MODE::FAIL_SAFE;
#endif
          setFailSafe((int16_t*)(&failSafe[1]), len);
          trsp.putFrame(COMMAND::CHANNELS_FAILSAFE_DATA,
                   FRAME_TYPE::REQUEST_SET_NO_RESP, (uint8_t*)failSafe,
#if defined(RADIO_NB4)
                   len * 2 + 2);
#else
                   AFHDS3_MAX_CHANNELS * 2 + 2);
#endif
      }
      else if( isConnected() ){
          uint8_t data[AFHDS3_MAX_CHANNELS*2 + 3] = { (uint8_t)(RX_CMD_FAILSAFE_VALUE&0xFF), (uint8_t)((RX_CMD_FAILSAFE_VALUE>>8)&0xFF), (uint8_t)(2*len)};
          int16_t failSafe[18];
          setFailSafe(&failSafe[0], len);
          std::memcpy( &data[3], failSafe, 2*len );
          trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, 2*len+3);
      }
    } else {
      trsp.putFrame(cmd, FRAME_TYPE::REQUEST_GET_DATA);
    }
    return;
  }

  // Sync settings when dirty flag is set
  auto *cfg = this->getConfig();
  if (checkDirtyFlag(DC_RX_CMD_TX_PWR))
  {
    uint8_t data[] = { (uint8_t)(RX_CMD_TX_PWR&0xFF), (uint8_t)((RX_CMD_TX_PWR>>8)&0xFF), 2,
                       (uint8_t)(AFHDS3_POWER[moduleData->afhds3.rfPower]&0xFF),  (uint8_t)((AFHDS3_POWER[moduleData->afhds3.rfPower]>>8)&0xFF)};
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    clearDirtyFlag(DC_RX_CMD_TX_PWR);
    RFCurrentPower = (AFHDS3_POWER[moduleData->afhds3.rfPower]&0xFF);
    return;
  }
  else if( EXTERNAL_MODULE == module_index )
  {
    if( !RFCurrentPower )
    {
      RFCurrentPower = sns_RFCurrentPower;
    }
  }

  if (isConnected()) {
    // Sync config, with commands
    if (syncSettings()) { return; }

    if (sensorCalibration()) { return; }

    // Send channels data
    sendChannelsData();
  } else {
    //default frame - request state
    trsp.putFrame(MODULE_STATE, FRAME_TYPE::REQUEST_GET_DATA);
  }
}

#if defined(RADIO_NB4)
void ProtoState::nb4Request(COMMAND command, uint8_t* payload, uint8_t size,
                            FRAME_TYPE type)
{
  nb4Pending = command;
  trsp.putFrame(command, type, payload, size);
}

void ProtoState::nb4Fail(const char* message)
{
  nb4Error = message;
  nb4Stage = Nb4Stage::Failed;
  nb4Pending = 0xff; // Send standby on the mixer task, not from the RX task.
  cfg.others.isConnected = false;
  cfg.others.lastUpdated = get_tmr10ms();
  nb4FailedAt = get_tmr10ms();
  trsp.clear();
}

void ProtoState::setupNb4Frame()
{
  const auto now = get_tmr10ms();
  const bool bind = getModuleMode(module_index) == MODULE_MODE_BIND;
  if (cfg.version != (Nb4RfConfig::enhanced() ? 1 : 0)) {
    nb4Config.load(cfg);
    applyConfigFromModel();
    clearFrameData();
    nb4Stage = Nb4Stage::Ready;
    nb4Pending = nb4Page = nb4ReadyTries = 0;
    nb4ReceivedConfig = false;
    nb4Error = nullptr;
  }
  if (bind != nb4BindMode) {
    // Also handles cancellation during a page/retry. Old replies can no longer
    // advance the new operation because Transport checks command and sequence.
    nb4BindMode = nb4Binding = bind;
    nb4ReceivedConfig = false;
    nb4Error = nullptr;
    nb4Pending = nb4Page = nb4ReadyTries = 0;
    nb4BindStarted = now;
    if (bind) receiverTelemetry = {};
    clearFrameData();
    nb4Config.load(cfg);
    applyConfigFromModel();
    nb4Stage = Nb4Stage::Ready;
    state = STATE_NOT_READY;
  }

  // Two-way binding needs a confirmed connection, not just a saved identity.
  // One-way has no connection feedback and deliberately waits for manual exit.
  if (nb4Binding && !isConnected() &&
      (!nb4ReceivedConfig || moduleData->afhds3.telemetry) &&
      nb4Stage != Nb4Stage::Failed && (tmr10ms_t)(now - nb4BindStarted) > 3000)
    nb4Fail(nb4ReceivedConfig ?
              nb4RfText("Receptor sin confirmar", "Receiver not confirmed") :
              nb4RfText("Tiempo de enlace agotado", "Bind timed out"));

  if (nb4Stage == Nb4Stage::Failed) {
    if (nb4Pending == 0xff) {
      uint8_t mode = STANDBY;
      trsp.putFrame(MODULE_MODE, REQUEST_SET_NO_RESP, &mode, 1);
      nb4Pending = 0;
      return;
    }
    // Retry the handshake instead of latching the failure. The internal module
    // can take longer than the ready poll budget to boot, which happens after
    // a firmware write in particular, and a saved receiver identity must then
    // reconnect on its own rather than waiting for the user to open and cancel
    // the bind dialog. Binding is excluded: a timed-out bind must stay failed
    // so it is never silently restarted behind the user.
    if (!nb4Binding &&
        (tmr10ms_t)(now - nb4FailedAt) > NB4_FAIL_RETRY_DELAY) {
      nb4Stage = Nb4Stage::Ready;
      nb4Pending = nb4Page = nb4ReadyTries = 0;
      nb4ReceivedConfig = false;
      nb4Error = nullptr;
      nb4Config.load(cfg);
      applyConfigFromModel();
      clearFrameData();
      state = STATE_NOT_READY;
      return;
    }
    trsp.skipFrame();
    return;
  }

  bool error = false;
  if (trsp.waiting() && trsp.handleRetransmissions(error)) return;
  if (error) {
    if (nb4Stage == Nb4Stage::Ready && ++nb4ReadyTries < 100) {
      // Give the internal module time to boot before declaring a UART timeout.
      trsp.clear();
      nb4Pending = 0;
    }
    // A state poll timing out while driving must not stop channel streaming.
    // Configuration failures, however, must never be reported as a bind.
    else if (nb4Stage == Nb4Stage::Active &&
             (nb4Pending == MODULE_STATE || nb4Pending == SEND_COMMAND)) {
      trsp.clear();
      nb4Pending = 0;
      state = STATE_SYNC_RUNNING;
      cfg.others.isConnected = false;
    } else {
      nb4Fail(nb4RfText("El módulo RF no responde", "RF module not responding"));
      return;
    }
  }

  uint8_t mode;
  switch (nb4Stage) {
    case Nb4Stage::Ready:
      nb4Request(MODULE_READY);
      return;
    case Nb4Stage::Standby:
      mode = STANDBY;
      nb4Request(MODULE_MODE, &mode, 1, REQUEST_SET_EXPECT_DATA);
      return;
    case Nb4Stage::Pages: {
      uint8_t page[170];
      auto length = nb4Config.page(nb4Page, nb4Binding, cfg, page);
      nb4Request(MODULE_SET_CONFIG, page, length, REQUEST_SET_EXPECT_DATA);
      return;
    }
    case Nb4Stage::Bind:
      // Normal-bind operation: four 0x04 pages with operation=1, followed
      // by an empty 0x05 (not legacy MODE=2).
      nb4Request(MODULE_APPLY_CONFIG, nullptr, 0, REQUEST_SET_EXPECT_DATA);
      return;
    case Nb4Stage::Run:
      mode = nb4NormalMode;
      nb4Request(MODULE_MODE, &mode, 1, REQUEST_SET_EXPECT_DATA);
      return;
    default: break;
  }

  constexpr uint32_t outputOptions = (1u << DC_RX_CMD_GET_VERSION) - 1;
  if (!nb4Binding && ((cfg.others.dirtyFlag & outputOptions) ||
                     nb4AppliedModelKey != Nb4RfConfig::modelKey())) {
    // Persist actual output options and apply the official update pages.
    applyConfigFromModel();
    nb4AppliedModelKey = Nb4RfConfig::modelKey();
    nb4Config.save(cfg);
    cfg.others.dirtyFlag &= ~outputOptions;
    if (nb4Config.bound) {
      nb4Stage = Nb4Stage::Standby;
      nb4Page = 0;
      uint8_t standby = STANDBY;
      nb4Request(MODULE_MODE, &standby, 1, REQUEST_SET_EXPECT_DATA);
      return;
    }
  }

  if (isConnected() && sensorCalibration()) {
    nb4Pending = SEND_COMMAND;
    return;
  }

  if ((tmr10ms_t)(now - nb4LastPoll) >= 20) {
    nb4LastPoll = now;
    nb4Request(MODULE_STATE);
  } else if (nb4Config.bound && !nb4Binding) {
    if (++cmdCount >= 500) {
      cmdCount = 0;
      int16_t failsafe[AFHDS3_MAX_CHANNELS + 1]{};
      const auto channels = setFailSafe(failsafe + 1, sentModuleChannels(module_index));
      failsafe[0] = (channels << 8) | CHANNELS_DATA_MODE::FAIL_SAFE;
      trsp.putFrame(CHANNELS_FAILSAFE_DATA, REQUEST_SET_NO_RESP,
                    (uint8_t*)failsafe, 2 + channels * 2);
    } else sendChannelsData();
  } else trsp.skipFrame();
}

bool ProtoState::parseNb4Data(const AfhdsFrame* frame, uint8_t length)
{
  if (frame->frameType == REQUEST_GET_DATA && frame->command == MODULE_SET_CONFIG) {
    // The peer requests a page with one byte. Reply with page index +
    // contents, omitting the SET operation byte.
    // This is independent of our pending handshake/configuration request.
    if (length == 8 && frame->value < 4) {
      uint8_t page[170];
      const auto size = nb4Config.page(frame->value, false, cfg, page);
      trsp.queueResponse(MODULE_SET_CONFIG, frame->frameNumber, page + 1, size - 1);
    }
    return true;
  }
  // Parser inserts address for addressless SLIP; exclude header, checksum, END.
  if (length < 7 || !containsData((FRAME_TYPE)frame->frameType)) return true;
  const unsigned size = length - 7;
  const uint8_t* data = &frame->value;
  const bool reply = frame->frameType == RESPONSE_DATA;
  const bool notification = frame->frameType == REQUEST_SET_EXPECT_DATA ||
                            frame->frameType == REQUEST_SET_EXPECT_ACK ||
                            frame->frameType == REQUEST_SET_NO_RESP;

  if (frame->command == SEND_COMMAND && reply) {
    nb4Pending = 0;
    return true; // Application result follows separately as command 0x0d.
  }
  if (frame->command == COMMAND_RESULT && notification)
    return size < 4 || data[3] > size - 4;

  if (frame->command == MODULE_APPLY_CONFIG && notification) {
    // A short 0x05 reply is only a page mask. Only the full bind notification
    // contains the learned identity. Ignore late notifications after cancel.
    if (getModuleMode(module_index) == MODULE_MODE_BIND && nb4Binding &&
        (nb4Stage == Nb4Stage::Bind || nb4Stage == Nb4Stage::Active) &&
        nb4Config.acceptReceiver(data, size, cfg)) {
      nb4ReceivedConfig = true;
      cfg.others.isConnected = isConnected();
      cfg.others.lastUpdated = get_tmr10ms();
    }
    return true;
  }
  if (frame->command == MODULE_STATE && size == 1 && (reply || notification)) {
    if (data[0] == STATE_HW_TEST) {
      state = STATE_HW_TEST;
      nb4Fail(nb4RfText("Modo de prueba RF inesperado", "Unexpected RF test mode"));
      return true;
    }
    if (data[0] <= STATE_READY) state = (ModuleState)data[0];
    if (reply) nb4Pending = 0;
    cfg.others.isConnected = isConnected();
    cfg.others.lastUpdated = get_tmr10ms();
    return true;
  }

  if (reply && frame->command == nb4Pending) {
    nb4Pending = 0;
    if (size != 1) {
      nb4Fail(nb4RfText("Respuesta RF incompleta", "Incomplete RF response"));
      return true;
    }
    switch (frame->command) {
      case MODULE_READY:
        if (data[0] != MODULE_STATUS_READY) {
          if (++nb4ReadyTries >= 100)
            nb4Fail(nb4RfText("Módulo RF no preparado", "RF module not ready"));
        } else {
          state = STATE_READY;
          nb4Stage = Nb4Stage::Standby;
          applyConfigFromModel();
        }
        break;
      case MODULE_MODE:
        if (data[0] != SUCCESS) {
          nb4Fail(nb4RfText("Modo RF rechazado", "RF mode rejected"));
        } else if (nb4Stage == Nb4Stage::Standby) {
          state = STATE_STANDBY;
          nb4Page = 0;
          nb4AppliedModelKey = Nb4RfConfig::modelKey();
          nb4Stage = nb4Binding || nb4Config.bound ? Nb4Stage::Pages : Nb4Stage::Active;
        } else if (nb4Stage == Nb4Stage::Run) {
          state = STATE_SYNC_RUNNING;
          nb4Stage = Nb4Stage::Active;
          if (cfg.version) setIbusType(cfg.v1.NewPortTypes);
          else memset(ibus_type, SES_NPT_IBUS1_IN, sizeof(ibus_type));
        }
        break;
      case MODULE_SET_CONFIG: {
        // Each status is recorded, and the complete low nibble is tested
        // only after the batch. Page 0 resets the batch: the module returns
        // 0xf8, not a cumulative mask of 1.
        if (++nb4Page == 4) {
          if ((data[0] & 0x0f) != 0x0f) {
            nb4Fail(nb4RfText("Configuración RF rechazada", "RF configuration rejected"));
          } else {
            nb4Stage = nb4Binding ? Nb4Stage::Bind : Nb4Stage::Run;
            cfg.others.dirtyFlag = 0;
          }
        }
        break;
      }
      case MODULE_APPLY_CONFIG:
        if ((data[0] & 0x0f) != 0x0f) {
          nb4Fail(nb4RfText("Enlace RF rechazado", "RF bind rejected"));
        } else {
          nb4Stage = Nb4Stage::Active;
          if (state != STATE_SYNC_DONE) state = STATE_BINDING;
        }
        break;
      default: break;
    }
    return true;
  }
  if (frame->command == TELEMETRY_DATA && size >= 1 && notification)
    parseNb4Telemetry(data, size);
  // Unsupported legacy commands must not interpret arbitrary NB4 bytes as cfg.
  return true;
}

void ProtoState::parseNb4Telemetry(const uint8_t* data, unsigned size)
{
  // The NB4 module uses both 0x22 and 0x23 telemetry containers. Receiver
  // identity is encoded by the built-in sensor instance, not by the container:
  // 0=RX supply, 1=link quality, 2=RSSI, 3=noise and 4=SNR. Observed
  // traffic places the first four records in 0x22 packets.
  if ((data[0] != 0x22 && data[0] != 0x23) ||
      !moduleData->afhds3.telemetry || !nb4Config.bound)
    return;

  bool ibus2 = false;
  for (auto port : ibus_type)
    ibus2 |= port == SES_NPT_IBUS2 || port == SES_NPT_IBUS2_HUB_PORT;
  for (unsigned offset = 1; offset < size;) {
    const auto* record = data + offset;
    const unsigned length = record[0];
    if (length < 4 || length > size - offset) break;
    const uint8_t type = record[1], instance = record[2];
    if (type == 0xff) break;
    const unsigned bytes = length - 3;
    const bool builtin = (instance & 0x80) != 0;
    const uint8_t builtinIndex = instance & 0x7f;
    if (builtin && (type == 0 || type == 0xfe || type == 0xfc || type == 0xfb || type == 0xfa)) {
      if (bytes == 1 || bytes == 2) {
        const unsigned value = record[3] | (bytes == 2 ? record[4] << 8 : 0);
        if (type == 0 && builtinIndex == 0 && bytes == 2 && value != 0xffff) {
          receiverTelemetry.voltageMv = value * 10;
          receiverTelemetry.voltageTime = get_tmr10ms();
          receiverTelemetry.voltageAvailable = true;
          setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, 0x1000, 0, 0x80,
                            value, UNIT_VOLTS, 2);
        } else if (type == 0xfe && builtinIndex == 1 && value <= 100) {
          receiverTelemetry.quality = value;
          receiverTelemetry.qualityTime = get_tmr10ms();
          receiverTelemetry.qualityAvailable = true;
          telemetryData.rssi.set(value);
          // Zero quality is still a received packet, not a missing receiver.
          telemetryStreaming = TELEMETRY_TIMEOUT10ms;
          setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, type, 0, 0x80,
                            value, UNIT_PERCENT, 0);
        } else if (((type == 0xfc && builtinIndex == 2) ||
                    (type == 0xfb && builtinIndex == 3) ||
                    (type == 0xfa && builtinIndex == 4)) &&
                   bytes == 2 && value != 0xffff) {
          const int signedValue = type == 0xfa ? int(value) : -int(value);
          const int rounded = (signedValue + (signedValue >= 0 ? 2 : -2)) / 4;
          setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, type, 0, 0x80,
                            rounded, type == 0xfa ? UNIT_DB : UNIT_DBM, 0);
        }
      }
    } else if (type != 0xfe && (bytes == 1 || bytes == 2 || bytes == 4 ||
                               (type == 0x56 && bytes >= 7))) {
      // Existing decoders expect a 16-bit sensor type in place of length.
      uint8_t packet[TELEMETRY_RX_PACKET_SIZE];
      memcpy(packet, record, length);
      packet[0] = 0;
      if (ibus2) ::processFlySkyIbus2AFHDS3Sensor(packet, bytes);
      else ::processFlySkyAFHDS3Sensor(packet, bytes);
    }
    offset += length;
  }
}
#endif

uint8_t get_current_rfpower_level( uint8_t module )
{
  int16_t diff_min = protoState[module].RFCurrentPower-rfpowerTable[0];
  uint8_t power_level = 0;
  for( uint8_t i=1; i<7; i++ )
  {
    int16_t diff = protoState[module].RFCurrentPower-rfpowerTable[i];

    if( abs(diff) < abs(diff_min))
    {
      diff_min = diff;
      power_level = i;
    }
  }
  return power_level;
}

void ProtoState::init(uint8_t moduleIndex, void* buffer,
                      etx_module_state_t* mod_st, uint8_t fAddr)
{
  module_index = moduleIndex;
  trsp.init(buffer, mod_st, fAddr);
#if defined(RADIO_NB4)
  hardFaulted = false;
  receiverTelemetry = {};
  nb4Stage = Nb4Stage::Ready;
  nb4Pending = nb4Page = nb4ReadyTries = 0;
  nb4AppliedModelKey = Nb4RfConfig::modelKey();
  nb4Binding = nb4BindMode = nb4ReceivedConfig = false;
  nb4Error = nullptr;
  nb4LastPoll = nb4BindStarted = get_tmr10ms();
  resetConfig(0);
  nb4Config.load(cfg);
  rx_state = false;
  RFCurrentPower = Nb4RfConfig::get16(nb4Config.receiver + 0x8b);

#endif

  //clear local vars because it is member of union
  moduleData = &g_model.moduleData[module_index];
  state = ModuleState::STATE_NOT_READY;
  modelID = 0xff;  // Valid range 0-19
//  modelIDSet = false;
  clearFrameData();
}

void ProtoState::clearFrameData()
{
  trsp.clear();

  cmdCount = 0;
  cmdIndex = 0;
}

bool containsData(enum FRAME_TYPE frameType)
{
  return (frameType == FRAME_TYPE::RESPONSE_DATA ||
      frameType == FRAME_TYPE::REQUEST_SET_EXPECT_DATA ||
      frameType == FRAME_TYPE::REQUEST_SET_EXPECT_ACK ||
      frameType == FRAME_TYPE::REQUEST_SET_EXPECT_DATA ||
      frameType == FRAME_TYPE::REQUEST_SET_NO_RESP);
}

void ProtoState::setState(ModuleState state)
{
  if (state == this->state) {
    return;
  }

  uint8_t oldState = this->state;
  this->state = state;
  if (oldState == ModuleState::STATE_BINDING) {
    setModuleMode(module_index, ::ModuleSettingsMode::MODULE_MODE_NORMAL);
  }
  if (state == ModuleState::STATE_NOT_READY) {
    trsp.clear();
  }
  else if (state == ModuleState::STATE_SYNC_RUNNING || state == ModuleState::STATE_SYNC_DONE)
  {
    // Get config when switched to STATE_SYNC_XXX
    trsp.enqueue(COMMAND::MODULE_GET_CONFIG, FRAME_TYPE::REQUEST_GET_DATA);

    // Change connected state and refresh UI
    cfg.others.isConnected = isConnected();
    cfg.others.lastUpdated = get_tmr10ms();
    cfg.others.dirtyFlag = 0U;
  }
}

/*
void ProtoState::requestInfoAndRun(bool send)
{
  // set model ID
  // trsp.enqueue(COMMAND::MODEL_ID, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, true,
  //              g_model.header.modelId[module_index]);

  // RUN
  trsp.enqueue(COMMAND::MODULE_MODE, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, true,
               (uint8_t)MODULE_MODE_E::RUN);

  if (send) { trsp.processQueue(); }
}*/

void ProtoState::parseData(uint8_t* rxBuffer, uint8_t rxBufferCount)
{
  AfhdsFrame* responseFrame = reinterpret_cast<AfhdsFrame*>(rxBuffer);
#if defined(RADIO_NB4)
  if (parseNb4Data(responseFrame, rxBufferCount)) return;
#endif
  if (containsData((enum FRAME_TYPE) responseFrame->frameType)) {
    switch (responseFrame->command) {
      case COMMAND::MODULE_READY:
        if (responseFrame->value == MODULE_STATUS_READY) {
          setState(ModuleState::STATE_READY);
          // requestInfoAndRun();
        }
        else {
          setState(ModuleState::STATE_NOT_READY);
        }
        break;
      case COMMAND::MODULE_GET_CONFIG: {
        size_t len = min<size_t>(sizeof(cfg.buffer), rxBufferCount);
        std::memcpy((void*) cfg.buffer, &responseFrame->value, len);
        moduleData->afhds3.emi = cfg.v0.EMIStandard;
        moduleData->afhds3.telemetry = cfg.v0.IsTwoWay;
        moduleData->afhds3.phyMode = cfg.v0.PhyMode;
        cfg.others.ExternalBusType = cfg.v0.ExternalBusType;
        SET_DIRTY();
        cfg.others.lastUpdated = get_tmr10ms();
      } break;
      case COMMAND::MODULE_VERSION:
        std::memcpy((void*) &version, &responseFrame->value, sizeof(version));
        break;
      case COMMAND::MODULE_RFPOWER:
        {  uint8_t* value = &responseFrame->value;
          RFCurrentPower = (value[1]<<8) + value[0];
        }
        break;
      case COMMAND::MODULE_STATE:
        setState((ModuleState)responseFrame->value);
        if(STATE_SYNC_DONE == (ModuleState)responseFrame->value){
          if( !this->rx_state )
          {
              auto *cfg = this->getConfig();
              this->rx_state = true;
              DIRTY_CMD( cfg, DC_RX_CMD_GET_RX_VERSION );
              trsp.enqueue( COMMAND::MODULE_VERSION, FRAME_TYPE::REQUEST_GET_DATA );
              setIbusType(cfg->v1.NewPortTypes);
//            modelcfgGet = true;
//            cfg.others.isConnected = true;
//            cfg.others.lastUpdated = get_tmr10ms();
          }
        }
        else
        {
          this->rx_state = false;
//          cfg.others.isConnected = false;
//          cfg.others.lastUpdated = get_tmr10ms();
        }
        break;
      case COMMAND::MODULE_MODE:
        if (responseFrame->value != CMD_RESULT::SUCCESS) {
          setState(ModuleState::STATE_NOT_READY);
        }
        else if( !RFCurrentPower && INTERNAL_MODULE==module_index )
        {
          trsp.enqueue( COMMAND::MODULE_RFPOWER, FRAME_TYPE::REQUEST_GET_DATA );
        }
        break;
      case COMMAND::MODULE_SET_CONFIG:
        if (responseFrame->value != CMD_RESULT::SUCCESS) {
          setState(ModuleState::STATE_NOT_READY);
        }
        break;
      case COMMAND::MODEL_ID:
        if (responseFrame->value == CMD_RESULT::SUCCESS) {
        }
        break;
      case COMMAND::TELEMETRY_DATA:
        {
        uint8_t* telemetry = &responseFrame->value;

        if (telemetry[0] == 0x22) {
          telemetry++;
          auto* telemetryEnd = rxBuffer + rxBufferCount - 2;
          while (telemetry < telemetryEnd) {

            uint8_t len = telemetry[0];
            if (len < 4 || telemetry + len > telemetryEnd)
            {
              break;
            }
            telemetry[0] = 0;
            uint8_t ibus_version = SES_NPT_IBUS1_IN;
            for(uint8_t i = 0; i < SES_NPT_NB_MAX_PORTS; i++) {
              // If ibus2 is configured, ignore ibus1.
              if (ibus_type[i] == SES_NPT_IBUS2 || ibus_type[i] == SES_NPT_IBUS2_HUB_PORT) {
                ibus_version = SES_NPT_IBUS2;
                break;
              }
            }
            if (ibus_version == afhds3::SES_NPT_IBUS2) {
              ::processFlySkyIbus2AFHDS3Sensor(telemetry, len-3);
            } else {
              ::processFlySkyAFHDS3Sensor(telemetry, len-3);
            }
            telemetry += len;
          }
        }
      }
        break;
      case COMMAND::COMMAND_RESULT: {
        uint8_t *data = &responseFrame->value;
        uint16_t cmd_code = *data++;
        cmd_code |= (*data++)<<8;
        uint8_t result  = *data++;
        auto *cfg = this->getConfig();
        switch (cmd_code)
        {
          case RX_CMD_RSSI_CHANNEL_SETUP:
            if(RX_CMDRESULT::RXSUCCESS==result) {
              clearDirtyFlag(DC_RX_CMD_RSSI_CHANNEL_SETUP);
            } break;
          case RX_CMD_OUT_PWM_PPM_MODE:
            if(RX_CMDRESULT::RXSUCCESS==result) {
              clearDirtyFlag(DC_RX_CMD_OUT_PWM_PPM_MODE);
            } break;
          case RX_CMD_FREQUENCY_V0:
            if(RX_CMDRESULT::RXSUCCESS==result) {
              clearDirtyFlag(DC_RX_CMD_FREQUENCY_V0);
            } break;
          case RX_CMD_PORT_TYPE_V1:
            if(RX_CMDRESULT::RXSUCCESS==result) {
              clearDirtyFlag(DC_RX_CMD_PORT_TYPE_V1);
            } break;
          case RX_CMD_FREQUENCY_V1:
            if(RX_CMDRESULT::RXSUCCESS==result) {
              if (checkDirtyFlag(DC_RX_CMD_FREQUENCY_V1)) {
                clearDirtyFlag(DC_RX_CMD_FREQUENCY_V1);
              } else {
                clearDirtyFlag(DC_RX_CMD_FREQUENCY_V1_2);
              }
            } break;
          case RX_CMD_BUS_TYPE_V0:
            if(RX_CMDRESULT::RXSUCCESS==result) {
              clearDirtyFlag(DC_RX_CMD_BUS_TYPE_V0);
              clearDirtyFlag(DC_RX_CMD_BUS_TYPE_V0_2);
            } break;
          case RX_CMD_IBUS_DIRECTION:
            if(RX_CMDRESULT::RXSUCCESS==*data++) {
              clearDirtyFlag(DC_RX_CMD_BUS_DIRECTION);
              DIRTY_CMD(cfg, DC_RX_CMD_BUS_TYPE_V0_2);
            }break;
          case RX_CMD_GET_VERSION :
            if(RX_CMDRESULT::RXSUCCESS==result) {
              if(14==*data++)
              {
                std::memcpy((void*) &rx_version, data, sizeof(rx_version));
                clearDirtyFlag(DC_RX_CMD_GET_RX_VERSION);
              }
            }break;
          case RX_CMD_CODE_IBUS2_SET_PARAM:
            {
              uint8_t len = *data++;
              ::Ibus2ParamCheck(data, len);
            }
            break;
          case RX_CMD_CODE_IBUS2_GET_PARAM:
            {
              uint8_t len = *data++;
              ::Ibus2ParamCheck(data, len);
              // cfg->others.calibData[IBUS2_SENSOR_IBC] = getIbus2IbcState();
              // DIRTY_CMD(cfg, DC_RX_CMD_CLEAR_IBC);
            }
            break;
        default:
          break;
        }
      } break;
    }
  }

  if (responseFrame->frameType == FRAME_TYPE::REQUEST_GET_DATA ||
      responseFrame->frameType == FRAME_TYPE::REQUEST_SET_EXPECT_DATA) {
    TRACE("Command %02X NOT IMPLEMENTED!", responseFrame->command);
  }
}

inline bool isSbus(uint8_t mode)
{
  return (mode & 1);
}

inline bool isPWM(uint8_t mode)
{
  return !(mode & 2);
}

bool ProtoState::sensorCalibration() {
  auto *cfg = this->getConfig();

  static uint8_t data[30] = {0};
  uint8_t len = 0;

  uint8_t sensor_online = flyskyIbus2SensorOnLine();
  cfg->others.sensorOnLine = sensor_online;

  if (checkDirtyFlag(DC_RX_CMD_CALIB_GYRO)) {

    ::flySkyIbus2CalGpsGyro(data, &len);
    trsp.putFrame( COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, len);
    clearDirtyFlag(DC_RX_CMD_CALIB_GYRO);
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_CALIB_ALT)) {
    ::flySkyIbus2CalGpsAlt();
    clearDirtyFlag(DC_RX_CMD_CALIB_ALT);
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_CALIB_DIST) ){
    ::flySkyIbus2CalGpsDist();
    clearDirtyFlag(DC_RX_CMD_CALIB_DIST);
    return true;
  }

  static uint32_t ibc_update_tick = 0;
  static short last_ibc_v = cfg->others.calibData[IBUS2_SENSOR_IBC];
  if (last_ibc_v != cfg->others.calibData[IBUS2_SENSOR_IBC] ) {
    if (timersGetMsTick() - ibc_update_tick > 2000) { // 2-second check
      ibc_update_tick = timersGetMsTick();
      last_ibc_v = cfg->others.calibData[IBUS2_SENSOR_IBC];
      ::flySkyIbus2CalibIBC(data, &len, cfg->others.calibData[IBUS2_SENSOR_IBC]);
      trsp.putFrame( COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, len);
      return true;
    }
  }

  return false;
}

bool ProtoState::syncSettings()
{

  auto *cfg = this->getConfig();

  // Handles old receivers bug
  if ( checkDirtyFlag(DC_RX_CMD_GET_RX_VERSION) )
  {
    uint8_t data[] = { (uint8_t)(RX_CMD_GET_VERSION&0xFF), (uint8_t)((RX_CMD_GET_VERSION>>8)&0xFF), 0x00 };
    trsp.putFrame( COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data) );
    return true;
  }
  // Sync settings when dirty flag is set
  if (checkDirtyFlag(DC_RX_CMD_TX_PWR))
  {
    uint8_t data[] = { (uint8_t)(RX_CMD_TX_PWR&0xFF), (uint8_t)((RX_CMD_TX_PWR>>8)&0xFF), 2,
                       (uint8_t)(AFHDS3_POWER[moduleData->afhds3.rfPower]&0xFF),  (uint8_t)((AFHDS3_POWER[moduleData->afhds3.rfPower]>>8)&0xFF)};
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    clearDirtyFlag(DC_RX_CMD_TX_PWR);
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_RSSI_CHANNEL_SETUP))
  {
    uint8_t data[] = { (uint8_t)(RX_CMD_RSSI_CHANNEL_SETUP&0xFF), (uint8_t)((RX_CMD_RSSI_CHANNEL_SETUP>>8)&0xFF), 1, cfg->v1.SignalStrengthRCChannelNb };
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_OUT_PWM_PPM_MODE))
  {
    uint8_t data[] = { (uint8_t)(RX_CMD_OUT_PWM_PPM_MODE&0xFF), (uint8_t)((RX_CMD_OUT_PWM_PPM_MODE>>8)&0xFF), 1, cfg->v0.AnalogOutput };
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    return true;
  }
  if (checkDirtyFlag(DC_RX_CMD_FREQUENCY_V0))
  {
    uint16_t Frequency = ((cfg->v0.PWMFrequency.Synchronized<<15)| cfg->v0.PWMFrequency.Frequency);
    uint8_t data[] = { (uint8_t)(RX_CMD_FREQUENCY_V0&0xFF), (uint8_t)((RX_CMD_FREQUENCY_V0>>8)&0xFF), 2,
                        (uint8_t)(Frequency&0xFF), (uint8_t)((Frequency>>8)&0xFF) };
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_PORT_TYPE_V1))
  {
    uint8_t data[] = { (uint8_t)(RX_CMD_PORT_TYPE_V1&0xFF), (uint8_t)((RX_CMD_PORT_TYPE_V1>>8)&0xFF), 4, 0, 0, 0, 0 };
    setIbusType(cfg->v1.NewPortTypes);
    // If pure is upgraded from multiple ibus2 to ibus2 hub
    uint8_t tempPortTypes[SES_NPT_NB_MAX_PORTS] = {0};
    std::memcpy(tempPortTypes, cfg->v1.NewPortTypes, SES_NPT_NB_MAX_PORTS);

    uint8_t ibus2Count = 0;
    for (uint8_t i = 0; i < SES_NPT_NB_MAX_PORTS; i++) {
        if (tempPortTypes[i] == afhds3::SES_NPT_IBUS2) {
            ibus2Count++;
        }
    }
    if (ibus2Count >= 2) {
        for (uint8_t i = 0; i < SES_NPT_NB_MAX_PORTS; i++) {
            if (tempPortTypes[i] == afhds3::SES_NPT_IBUS2) {
                tempPortTypes[i] = afhds3::SES_NPT_IBUS2_HUB_PORT;
            }
        }
    }

    std::memcpy(&data[3], tempPortTypes, SES_NPT_NB_MAX_PORTS);
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_FREQUENCY_V1))
  {
    uint8_t data[32 + 3 + 3] = { (uint8_t)(RX_CMD_FREQUENCY_V1&0xFF), (uint8_t)((RX_CMD_FREQUENCY_V1>>8)&0xFF), 32+3};
    data[3] = 0;
    std::memcpy(&data[4], &cfg->v1.PWMFrequenciesV1.PWMFrequencies[0], 32);
    data[36] = cfg->v1.PWMFrequenciesV1.Synchronized & 0xff;
    data[37] = (cfg->v1.PWMFrequenciesV1.Synchronized>>8) & 0xff;
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    DIRTY_CMD(cfg, DC_RX_CMD_FREQUENCY_V1_2);
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_FREQUENCY_V1_2))
  {
    uint8_t data[32 + 3 + 3] = { (uint8_t)(RX_CMD_FREQUENCY_V1_2&0xFF), (uint8_t)((RX_CMD_FREQUENCY_V1_2>>8)&0xFF), 32+3};
    data[3] = 1;
    std::memcpy(&data[4], &cfg->v1.PWMFrequenciesV1.PWMFrequencies[16], 32);
    data[36] = (cfg->v1.PWMFrequenciesV1.Synchronized>>16) & 0xff;
    data[37] = (cfg->v1.PWMFrequenciesV1.Synchronized>>24) & 0xff;
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));
    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_BUS_TYPE_V0))
  {
    bool onlySupportIBUSOut = (1==receiver_type(rx_version.ProductNumber));

    if (onlySupportIBUSOut && cfg->others.ExternalBusType == EB_BT_IBUS1_IN)
    {
        cfg->others.ExternalBusType = EB_BT_IBUS1_OUT;
    }

    DIRTY_CMD(cfg, DC_RX_CMD_BUS_DIRECTION);
    clearDirtyFlag(DC_RX_CMD_BUS_TYPE_V0);
  }

  if (checkDirtyFlag(DC_RX_CMD_BUS_TYPE_V0_2))
  {
    bool onlySupportIBUSOut = (1==receiver_type(rx_version.ProductNumber));

    if (onlySupportIBUSOut && cfg->others.ExternalBusType == EB_BT_IBUS1_IN)
    {
        cfg->others.ExternalBusType = EB_BT_IBUS1_OUT;
    }

    uint8_t data[] = { (uint8_t)(RX_CMD_BUS_TYPE_V0&0xFF), (uint8_t)((RX_CMD_BUS_TYPE_V0>>8)&0xFF), 1,
                       cfg->others.ExternalBusType == EB_BT_SBUS1 ? EB_BT_SBUS1 : EB_BT_IBUS1};
    trsp.putFrame(COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data));

    return true;
  }

  if (checkDirtyFlag(DC_RX_CMD_BUS_DIRECTION))
  {
    static uint8_t bus_dir;

    if( cfg->others.ExternalBusType == EB_BT_IBUS1_OUT || cfg->others.ExternalBusType == EB_BT_SBUS1 )
        bus_dir = BUS_OUT;
    else
        bus_dir = BUS_IN;
    uint8_t data[4] = { (uint8_t)(RX_CMD_IBUS_DIRECTION&0xFF), (uint8_t)((RX_CMD_IBUS_DIRECTION>>8)&0xFF), 1, bus_dir };
    trsp.putFrame( COMMAND::SEND_COMMAND, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, data, sizeof(data) );
    return true;
  }

  // No need to sync
  return false;
}

void ProtoState::sendChannelsData()
{
#if defined(RADIO_NB4_FAMILY)
  // Whatever else this cycle queued, it is carrying control positions.
  nb4LatencyCarriesChannels();
#endif
#if defined(RADIO_NB4)
  uint8_t channels_start = 0;
  uint8_t channelsCount = sentModuleChannels(module_index);
#else
  uint8_t channels_start = moduleData->channelsStart;
  uint8_t channelsCount = 8 + moduleData->channelsCount;
#endif
  uint8_t channels_last = channels_start + channelsCount;

  int16_t buffer[AFHDS3_MAX_CHANNELS + 1] = {0};

  uint8_t* header = (uint8_t*)buffer;
  header[0] = CHANNELS_DATA_MODE::CHANNELS;

  uint8_t channels = _phyMode_channels[cfg.v0.PhyMode];
#if defined(RADIO_NB4)
  // The NB4 product contract is the active model range, not the maximum of the
  // selected AFHDS3 PHY. Do not advertise or pack silent channels above it.
  channels = min<uint8_t>(channelsCount, MAX_OUTPUT_CHANNELS - channels_start);
#endif
  header[1] = channels;

  for (uint8_t channel = channels_start, index = 1; channel < channels_last && channel < MAX_OUTPUT_CHANNELS && index <= AFHDS3_MAX_CHANNELS;
       channel++, index++) {
    int16_t channelValue = convert(::getChannelValue(channel));
    buffer[index] = channelValue;
  }

  trsp.putFrame(COMMAND::CHANNELS_FAILSAFE_DATA, FRAME_TYPE::REQUEST_SET_NO_RESP,
           (uint8_t*)buffer, (channels + 1) * 2);
}

void ProtoState::stop()
{
  auto mode = (uint8_t)MODULE_MODE_E::STANDBY;
  trsp.putFrame(COMMAND::MODULE_MODE, FRAME_TYPE::REQUEST_SET_EXPECT_DATA, &mode, 1);
}

void ProtoState::resetConfig(uint8_t version)
{
  memclear(&cfg, sizeof(cfg));
  cfg.version = version;

  if (cfg.version == 1) {
    cfg.v1.SignalStrengthRCChannelNb = 0xFF;
    cfg.v1.FailsafeTimeout = 500;
    for (int i = 0; i < SES_NB_MAX_CHANNELS; i++)
      cfg.v1.PWMFrequenciesV1.PWMFrequencies[i] = 50;
  } else {
    cfg.v0.SignalStrengthRCChannelNb = 0xFF;
    cfg.v0.FailsafeTimeout = 500;
    cfg.v0.PWMFrequency.Frequency = 50;
  }
}

void ProtoState::applyConfigFromModel()
{
  uint8_t version = 0;
#if defined(SIMU)
  // TODO: work out why this is not initialised in some cases
  if (moduleData == nullptr) return;
#endif
  if (moduleData->afhds3.phyMode >= ROUTINE_FLCR1_18CH) {
    version = 1;
  }

  if (version != cfg.version) {
    resetConfig(version);
#if defined(RADIO_NB4)
    nb4Config.load(cfg);
    clearFrameData();
    nb4Stage = Nb4Stage::Ready;
    nb4Pending = nb4Page = nb4ReadyTries = 0;
    nb4ReceivedConfig = false;
    nb4Error = nullptr;
#endif
  }

  if (cfg.version == 1) {
    cfg.v1.EMIStandard = moduleData->afhds3.emi;
    cfg.v1.IsTwoWay = moduleData->afhds3.telemetry;
    cfg.v1.PhyMode = moduleData->afhds3.phyMode;

    // Failsafe
    setFailSafe(cfg.v1.FailSafe);
    if (moduleData->failsafeMode != FAILSAFE_NOPULSES) {
      cfg.v1.FailsafeOutputMode = true;
    } else {
      cfg.v1.FailsafeOutputMode = false;
    }
  } else {
    cfg.v0.EMIStandard = moduleData->afhds3.emi;
    cfg.v0.IsTwoWay = moduleData->afhds3.telemetry;
    cfg.v0.PhyMode = moduleData->afhds3.phyMode;
    cfg.v0.ExternalBusType = cfg.others.ExternalBusType==EB_BT_SBUS1 ? EB_BT_SBUS1 : EB_BT_IBUS1;
    // Failsafe
    setFailSafe(cfg.v0.FailSafe);
    if (moduleData->failsafeMode != FAILSAFE_NOPULSES) {
      cfg.v0.FailsafeOutputMode = true;
    } else {
      cfg.v0.FailsafeOutputMode = false;
    }
  }
}

uint8_t receiver_type( unsigned long productnumber )
{
  if(PN_FTR10 == productnumber || PN_FGR4 == productnumber || PN_FTR16S == productnumber)
  {
    return 1; // This type of RX can be set to IBUS-OUT/SBUS
  }
  else if(PN_FTR4 == productnumber)
  {
    return 2; // This type of RX can be set to IBUS-IN/IBUS-OUT/SBUS
  }
  else if( (PN_FTR8B&0xFFFF) <= productnumber)
  {
      return 3;   //V1 RX
  }
  else
  {
      return 0;
  }
}

inline int16_t ProtoState::convert(int channelValue)
{
  //pulseValue = limit<uint16_t>(0, 988 + ((channelValue + 1024) / 2), 0xfff);
  //988 - 750 = 238
  //238 * 20 = 4760
  //2250 - 2012 = 238
  //238 * 20 = 4760
  // 988   ---- 2012
  //-10240 ---- 10240
  //-1024  ---- 1024
  return ::limit<int16_t>(AFHDS3_FAILSAFE_MIN, channelValue * 10, AFHDS3_FAILSAFE_MAX);
}

uint8_t ProtoState::setFailSafe(int16_t* target, uint8_t rfchannelsCount )
{
  int16_t pulseValue = 0;
#if defined(RADIO_NB4)
  uint8_t channels_start = 0;
  uint8_t channelsCount = sentModuleChannels(module_index);
#else
  uint8_t channels_start = moduleData->channelsStart;
  uint8_t channelsCount = 8 + moduleData->channelsCount;
#endif
  uint8_t channels_last = channels_start + channelsCount;
  std::memset(target, 0, 2*rfchannelsCount );
  for (uint8_t channel = channels_start, i=0; i<rfchannelsCount && channel < channels_last && channel < MAX_OUTPUT_CHANNELS; channel++, i++) {
    if (moduleData->failsafeMode == FAILSAFE_CUSTOM) {
      if(FAILSAFE_CHANNEL_HOLD==g_model.failsafeChannels[channel]){
        pulseValue = FAILSAFE_HOLD_VALUE;
      }else if(FAILSAFE_CHANNEL_NOPULSE==g_model.failsafeChannels[channel]){
        pulseValue = FAILSAFE_NOPULSES_VALUE;
      }
      else{
        pulseValue = convert(g_model.failsafeChannels[channel]);
      }
    }
    else if (moduleData->failsafeMode == FAILSAFE_HOLD) {
      pulseValue = FAILSAFE_HOLD_VALUE;
    }
    else if (moduleData->failsafeMode == FAILSAFE_NOPULSES) {
      pulseValue = FAILSAFE_NOPULSES_VALUE;
    }
    else {
      pulseValue = FAILSAFE_NOPULSES_VALUE;
    }
    target[i] = pulseValue;
  }
#if defined(RADIO_NB4)
  return channelsCount;
#else
  // Return max channels because channel count cannot change after bind.
  return (uint8_t) (AFHDS3_MAX_CHANNELS);
#endif
}

Config_u* getConfig(uint8_t module)
{
  auto p_state = &protoState[module];
  return p_state->getConfig();
}

void applyModelConfig(uint8_t module)
{
  auto p_state = &protoState[module];
  p_state->applyConfigFromModel();
}

static const etx_serial_init _uartParams = {
  .baudrate = 0, //AFHDS3_UART_BAUDRATE,
  .encoding = ETX_Encoding_8N1,
  .direction = ETX_Dir_TX_RX,
  .polarity = ETX_Pol_Normal,
};

static void* initModule(uint8_t module)
{
  etx_module_state_t* mod_st = nullptr;
  etx_serial_init params(_uartParams);
#if defined(RADIO_NB4)
  const auto& framing = nb4::nb4RfSelectedFraming();
  uint16_t period = framing.cadenceUs;
  uint8_t fAddr = framing.frameAddress;
  mod_st = nb4::Nb4RfController::startTransport(module, params);
  if (!mod_st) return nullptr;
#else
  uint16_t period = AFHDS3_UART_COMMAND_TIMEOUT * 1000;
  uint8_t fAddr = (module == INTERNAL_MODULE ? DeviceAddress::IRM301
                                             : DeviceAddress::FRM303)
                      << 4 |
                  DeviceAddress::TRANSMITTER;

  params.baudrate = AFHDS3_UART_BAUDRATE;
  params.polarity =
    module == INTERNAL_MODULE ? ETX_Pol_Normal : ETX_Pol_Inverted;
  mod_st = modulePortInitSerial(module, ETX_MOD_PORT_UART, &params, false);

#if defined(CONFIGURABLE_MODULE_PORT)
  if (!mod_st && module == EXTERNAL_MODULE) {
    // Try Connect using aux serial mod
    params.polarity = ETX_Pol_Normal;
    mod_st = modulePortInitSerial(module, ETX_MOD_PORT_UART, &params, false);
  }
#endif

  if (!mod_st && module == EXTERNAL_MODULE) {
    // soft-serial fallback
    params.baudrate = AFHDS3_SOFTSERIAL_BAUDRATE;
    params.direction = ETX_Dir_TX;
    period = AFHDS3_SOFTSERIAL_COMMAND_TIMEOUT * 1000 /* us */;
    mod_st = modulePortInitSerial(module, ETX_MOD_PORT_SOFT_INV, &params, false);
    // TODO: telemetry RX ???
  }

  if (!mod_st) return nullptr;
#endif

  auto p_state = &protoState[module];
  p_state->init(module, pulsesGetModuleBuffer(module), mod_st, fAddr);
  mod_st->user_data = (void*)p_state;

  mixerSchedulerSetPeriod(module, period);

  return mod_st;
}

static void deinitModule(void* ctx)
{
  auto mod_st = (etx_module_state_t*)ctx;
#if defined(RADIO_NB4)
  if (auto p_state = (ProtoState*)mod_st->user_data) p_state->receiverTelemetry = {};
  nb4::Nb4RfController::stopTransport(mod_st);
#else
  modulePortDeInit(mod_st);
#endif
}

static void sendPulses(void* ctx, uint8_t* buffer, int16_t* channels,
                       uint8_t nChannels)
{
  (void)buffer;
  (void)channels;
  (void)nChannels;

  auto mod_st = (etx_module_state_t*)ctx;
  auto p_state = (ProtoState*)mod_st->user_data;
  if (!p_state) return; // UART fault may have released the serial context.
  p_state->setupFrame();
  p_state->sendFrame();
}

static bool txCompleted(void* ctx)
{
  auto mod_st = (etx_module_state_t*)ctx;
  auto p_state = (ProtoState*)mod_st->user_data;
  return p_state && !p_state->fifoFull();
}

etx_proto_driver_t ProtoDriver = {
    .protocol = PROTOCOL_CHANNELS_AFHDS3,
    .init = initModule,
    .deinit = deinitModule,
    .sendPulses = sendPulses,
    .processData = processTelemetryData,
    .processFrame = nullptr,
    .onConfigChange = nullptr,
    .txCompleted = txCompleted,
};

}  // namespace afhds3
