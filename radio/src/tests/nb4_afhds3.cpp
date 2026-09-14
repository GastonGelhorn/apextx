/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "gtests.h"

#if defined(AFHDS3)

#include "pulses/afhds3_transport.h"

using namespace afhds3;

#if defined(RADIO_NB4_FAMILY)
TEST(Nb4Afhds3, UsbClassicUsesEightAxesAndNoNonexistentButtonChannels)
{
  SYSTEM_RESET(); MODEL_RESET();
  g_model.usbJoystickExtMode = 0;
  for (unsigned i = 0; i < MAX_OUTPUT_CHANNELS; ++i) channelOutputs[i] = i * 256 - 1024;
  setupUSBJoystick();
  auto report = usbReport();
  ASSERT_NE(report.ptr, nullptr);
  ASSERT_EQ(report.size, 19);
  EXPECT_EQ(report.ptr[0] | report.ptr[1] | report.ptr[2], 0);
  for (unsigned i = 0; i < 8; ++i)
    EXPECT_EQ(report.ptr[3 + i * 2] | (report.ptr[4 + i * 2] << 8), i * 256);
  static_assert(USBJ_MAX_JOYSTICK_CHANNELS == MAX_OUTPUT_CHANNELS, "USB cannot read removed channels");
}
#endif

#if defined(RADIO_NB4)
TEST(Nb4Afhds3, CarDefaultsAndChannelSelectionNeverExceedEight)
{
  g_model.moduleData[0].type = MODULE_TYPE_FLYSKY_AFHDS3;
  g_model.moduleData[0].channelsStart = 0;
  g_model.moduleData[0].channelsCount = defaultModuleChannels_M8(0);
  EXPECT_EQ(g_model.moduleData[0].channelsCount, -6);
  EXPECT_EQ(sentModuleChannels(0), 2);
  EXPECT_EQ(minModuleChannels(0), 2);
  EXPECT_EQ(maxModuleChannels(0), 8);
  for (int channels = 2; channels <= 8; ++channels) {
    g_model.moduleData[0].channelsCount = channels - 8;
    EXPECT_EQ(sentModuleChannels(0), channels);
  }
  g_model.moduleData[0].channelsCount = 0;
  g_model.moduleData[0].channelsStart = 5;
  EXPECT_EQ(sentModuleChannels(0), 8);
  g_model.moduleData[0].channelsStart = 0;

  g_model.moduleData[0].channelsCount = -8;
  EXPECT_EQ(sentModuleChannels(0), 2);
  g_model.moduleData[0].channelsCount = 4;
  EXPECT_EQ(sentModuleChannels(0), 8);
}
#endif

namespace {

constexpr uint8_t ADDR = 0x14;

uint8_t expectedCrc(const uint8_t* frame, uint8_t len)
{
  uint8_t crc = 0;
  for (uint8_t i = 1; i < len; i++) crc += frame[i];
  return crc ^ 0xff;
}

}  // namespace

TEST(Nb4Afhds3, FrameWithAddressIsTheEdgeTxOne)
{
  uint8_t buffer[64];
  FrameTransport trsp;
  trsp.init(buffer, ADDR);

  uint8_t payload[] = {0x11, 0x22};
  trsp.putFrame(COMMAND::MODULE_READY, FRAME_TYPE::REQUEST_GET_DATA, payload, 2, 7);

  uint32_t size = trsp.getFrameSize();
  ASSERT_GE(size, 8u);
  EXPECT_EQ(buffer[0], 0xC0);              // START
  EXPECT_EQ(buffer[1], ADDR);
  EXPECT_EQ(buffer[2], 7);                 // Index
  EXPECT_EQ(buffer[3], (uint8_t)FRAME_TYPE::REQUEST_GET_DATA);
  EXPECT_EQ(buffer[4], (uint8_t)COMMAND::MODULE_READY);
  EXPECT_EQ(buffer[5], 0x11);
  EXPECT_EQ(buffer[6], 0x22);
  EXPECT_EQ(buffer[size - 1], 0xC0);       // END
  EXPECT_EQ(buffer[size - 2], expectedCrc(buffer, size - 2));
}

TEST(Nb4Afhds3, FrameWithoutAddressIsOneByteShorterAndNothingElseMoves)
{
  uint8_t withAddr[64];
  uint8_t without[64];

  uint8_t payload[] = {0x11, 0x22};

  FrameTransport a;
  a.init(withAddr, ADDR);
  a.putFrame(COMMAND::MODULE_READY, FRAME_TYPE::REQUEST_GET_DATA, payload, 2, 7);
  uint32_t sizeA = a.getFrameSize();

  FrameTransport b;
  b.init(without, ADDR, true);
  b.putFrame(COMMAND::MODULE_READY, FRAME_TYPE::REQUEST_GET_DATA, payload, 2, 7);
  uint32_t sizeB = b.getFrameSize();

  EXPECT_EQ(sizeB, sizeA - 1);
  EXPECT_EQ(without[0], 0xC0);
  EXPECT_EQ(without[1], 7);                // The index occupies the address position
  EXPECT_EQ(without[2], (uint8_t)FRAME_TYPE::REQUEST_GET_DATA);
  EXPECT_EQ(without[3], (uint8_t)COMMAND::MODULE_READY);
  EXPECT_EQ(without[sizeB - 1], 0xC0);

  EXPECT_EQ(without[sizeB - 2], expectedCrc(without, sizeB - 2));
  EXPECT_NE(without[sizeB - 2], withAddr[sizeA - 2]);
}

TEST(Nb4Afhds3, EscapingHidesTheTwoReservedBytes)
{
  uint8_t buffer[64];
  FrameTransport trsp;
  trsp.init(buffer, ADDR);

  uint8_t payload[] = {0xC0, 0xDB};
  trsp.putFrame(COMMAND::MODULE_READY, FRAME_TYPE::REQUEST_GET_DATA, payload, 2, 1);

  uint32_t size = trsp.getFrameSize();
  bool foundEscEnd = false, foundEscEsc = false;
  for (uint32_t i = 1; i + 1 < size; i++) {
    if (buffer[i] == 0xDB && buffer[i + 1] == 0xDC) foundEscEnd = true;
    if (buffer[i] == 0xDB && buffer[i + 1] == 0xDD) foundEscEsc = true;
  }
  EXPECT_TRUE(foundEscEnd);
  EXPECT_TRUE(foundEscEsc);

  // No unescaped C0 byte may appear in the payload.
  for (uint32_t i = 1; i + 1 < size; i++) EXPECT_NE(buffer[i], 0xC0);
}

TEST(Nb4Afhds3, ReceiverAcceptsAGoodFrameAndRejectsABadChecksum)
{
  uint8_t tx[64];
  FrameTransport sender;
  sender.init(tx, ADDR);
  uint8_t payload[] = {0x33};
  sender.putFrame(COMMAND::MODULE_READY, FRAME_TYPE::RESPONSE_DATA, payload, 1, 3);
  uint32_t size = sender.getFrameSize();

  uint8_t rx[64];
  uint8_t count = 0;
  FrameTransport parser;
  parser.init(tx, ADDR);

  bool complete = false;
  for (uint32_t i = 0; i < size; i++) {
    complete = parser.processTelemetryData(tx[i], rx, count, sizeof(rx));
  }
  EXPECT_TRUE(complete);

  AfhdsFrame* f = (AfhdsFrame*)rx;
  EXPECT_EQ(f->address, ADDR);
  EXPECT_EQ(f->frameNumber, 3);
  EXPECT_EQ(f->command, (uint8_t)COMMAND::MODULE_READY);

  tx[4] ^= 0x01;
  count = 0;
  complete = false;
  for (uint32_t i = 0; i < size; i++) {
    complete = parser.processTelemetryData(tx[i], rx, count, sizeof(rx));
  }
  EXPECT_FALSE(complete);
}

TEST(Nb4Afhds3, WithoutAddressTheReceiverFillsTheGapAfterChecking)
{
  uint8_t tx[64];
  FrameTransport sender;
  sender.init(tx, ADDR, true);
  uint8_t payload[] = {0x44};
  sender.putFrame(COMMAND::MODULE_READY, FRAME_TYPE::RESPONSE_DATA, payload, 1, 5);
  uint32_t size = sender.getFrameSize();

  uint8_t rx[64];
  uint8_t count = 0;
  FrameTransport parser;
  parser.init(tx, ADDR, true);

  bool complete = false;
  for (uint32_t i = 0; i < size; i++) {
    complete = parser.processTelemetryData(tx[i], rx, count, sizeof(rx));
  }
  ASSERT_TRUE(complete);

  AfhdsFrame* f = (AfhdsFrame*)rx;
  EXPECT_EQ(f->address, ADDR);
  EXPECT_EQ(f->frameNumber, 5);
  EXPECT_EQ(f->command, (uint8_t)COMMAND::MODULE_READY);
  EXPECT_EQ(count, size + 1);
}

#endif  // AFHDS3

#if defined(RADIO_NB4) && defined(AFHDS3) && (defined(NB4_RF_PROFILE_RECOVERED_LAB))
#include "pulses/afhds3.h"
#include "pulses/afhds3_nb4.h"
#include "targets/pl18/nb4_rf_controller.h"
#include "storage/yaml/yaml_datastructs.h"
#include "storage/yaml/yaml_parser.h"
#include "storage/yaml/yaml_tree_walker.h"
#include "nb4_car_state.h"
#include <vector>

namespace {
class Nb4Afhds3Bind : public ::testing::Test {
 protected:
  void* context = nullptr;
  tmr10ms_t previousTime = 0;
  uint8_t rxBuffer[TELEMETRY_RX_PACKET_SIZE]{};
  uint8_t rxCount = 0;
  std::vector<uint8_t> last;

  void SetUp() override {
    previousTime = g_tmr10ms;
    SYSTEM_RESET(); MODEL_RESET();
    telemetryReset();
    nb4::Nb4RfController::shutdown();
    modulePortInit();
    g_model.moduleData[0].type = MODULE_TYPE_FLYSKY_AFHDS3;
    g_model.moduleData[0].channelsCount = -6;
    g_model.moduleData[0].afhds3.phyMode = CLASSIC_FLCR1_18CH;
    g_model.moduleData[0].afhds3.emi = LNK_ES_CE;
    g_model.moduleData[0].afhds3.telemetry = true;
    context = ProtoDriver.init(0);
    ASSERT_NE(context, nullptr);
    setModuleMode(0, MODULE_MODE_NORMAL);
  }
  void TearDown() override {
    if (context) ProtoDriver.deinit(context);
    nb4::Nb4RfController::shutdown();
    setModuleMode(0, MODULE_MODE_NORMAL);
    g_tmr10ms = previousTime;
    getConfig(0)->others.lastUpdated = previousTime;
  }
  AfhdsFrame next() {
    ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
    FrameTransport parser;
    uint8_t unused[MODULE_BUFFER_SIZE]{}, decoded[240]{}, count = 0;
    parser.init(unused, ADDR, true, sizeof(unused));
    const auto wire = pulsesGetModuleBuffer(0);
    for (unsigned i = 0; i < MODULE_BUFFER_SIZE; ++i) {
      if (parser.processTelemetryData(wire[i], decoded, count, sizeof(decoded))) {
        last.assign(decoded, decoded + count);
        return *reinterpret_cast<AfhdsFrame*>(decoded);
      }
    }
    ADD_FAILURE();
    return {};
  }
  void receive(COMMAND command, const std::vector<uint8_t>& payload,
               uint8_t index, FRAME_TYPE type = RESPONSE_DATA) {
    uint8_t wire[MODULE_BUFFER_SIZE]{};
    FrameTransport sender;
    sender.init(wire, ADDR, true, sizeof(wire));
    sender.putFrame(command, type, const_cast<uint8_t*>(payload.data()), payload.size(), index);
    ASSERT_GT(sender.getFrameSize(), 0u);
    for (unsigned i = 0; i < sender.getFrameSize(); ++i)
      ProtoDriver.processData(context, wire[i], rxBuffer, &rxCount);
  }
  void ack(const AfhdsFrame& frame, uint8_t value) {
    receive((COMMAND)frame.command, {value}, frame.frameNumber);
  }
  void start(bool bind = true) {
    setModuleMode(0, bind ? MODULE_MODE_BIND : MODULE_MODE_NORMAL);
    EXPECT_EQ(getBindPhase(0), BindPhase::Preparing);
    auto f = next(); ASSERT_EQ(f.command, MODULE_READY); ack(f, 2);
    f = next(); ASSERT_EQ(f.command, MODULE_MODE); ASSERT_EQ(f.value, 1); ack(f, 2);
  }
  void pages(bool bind = true) {
    for (unsigned i = 0; i < 4; ++i) {
      auto f = next(); ASSERT_EQ(f.command, MODULE_SET_CONFIG);
      ASSERT_EQ(f.frameType, REQUEST_SET_EXPECT_DATA);
      ASSERT_EQ(f.value, bind ? 1 : 2); ASSERT_EQ(last[6], i);
      const unsigned sizes[] = {2, Nb4RfConfig::enhanced() ? 78u : 76u, 152, 170};
      ASSERT_EQ(last.size(), sizes[i] + 7);
      // 0xf8 on reset is captured hardware traffic. Intermediate masks need
      // not increase; stock requires the complete low nibble after the batch.
      ack(f, i == 3 ? 0xff : 0xf8);
    }
  }
  void bindCommit() {
    start(); pages();
    auto f = next(); ASSERT_EQ(f.command, MODULE_APPLY_CONFIG);
    ASSERT_EQ(last.size(), 7u); ack(f, 15);
  }
  void learned(uint8_t identity = 0xc0, FRAME_TYPE type = REQUEST_SET_EXPECT_ACK) {
    std::vector<uint8_t> payload(168);
    std::copy(nb4DefaultReceiver, nb4DefaultReceiver + 167, payload.begin() + 1);
    payload[0] = 15; payload[0x41] = identity; payload[0x42] = 0xdb;
    receive(MODULE_APPLY_CONFIG, payload, 81, type);
  }
  void restart() {
    ProtoDriver.deinit(context);
    context = ProtoDriver.init(0); rxCount = 0;
  }
};
}

TEST_F(Nb4Afhds3Bind, ReceiverHomeUsesBuiltInSensorInstancesWithoutDiscovery) {
  bindCommit(); learned();
  allowNewSensors = false;
  // The telemetry record this firmware parses: supply in centivolts, quality
  // as a percentage, and RSSI and noise as separate quarter-dB values whose
  // sign is inverted on the wire.
  receive(TELEMETRY_DATA, {0x22, 5, 0, 0x80, 0x5c, 3, 4, 0xfe, 0x81, 100, 0},
          90, REQUEST_SET_NO_RESP);
  auto home = nb4ReadCarState();
  EXPECT_TRUE(TELEMETRY_STREAMING());
  EXPECT_EQ(home.receiver.value, 8600);
  EXPECT_EQ(home.receiver.validity, Nb4Validity::Valid);
  EXPECT_EQ(home.link.value, 100);
  EXPECT_EQ(home.link.unit, Nb4Unit::Percent);
  EXPECT_EQ(getRxStatLabels()->max, 100);
  for (const auto& sensor : g_model.telemetrySensors) EXPECT_EQ(sensor.id, 0);

  // A traction battery and an unrelated built-in voltage must not replace RX.
  receive(TELEMETRY_DATA, {0x22, 5, 3, 0x80, 0x48, 3}, 92, REQUEST_SET_NO_RESP);
  receive(TELEMETRY_DATA, {0x22, 5, 0, 0x85, 0x90, 1}, 93, REQUEST_SET_NO_RESP);
  EXPECT_EQ(nb4ReadCarState().receiver.value, 8600);
  g_tmr10ms += TELEMETRY_TIMEOUT10ms;
  receive(TELEMETRY_DATA, {0x22, 4, 0xfe, 0x81, 87}, 94, REQUEST_SET_NO_RESP);
  EXPECT_EQ(nb4ReadCarState().receiver.validity, Nb4Validity::Stale);
  EXPECT_EQ(nb4ReadCarState().link.validity, Nb4Validity::Valid);
  restart();
  EXPECT_EQ(nb4ReadCarState().receiver.validity, Nb4Validity::Absent);
}

TEST_F(Nb4Afhds3Bind, ReceiverLossIsNotKeptAliveByModuleAndDoesNotWaitForLowSignalRepeat) {
  bindCommit(); learned();
  setModuleMode(0, MODULE_MODE_NORMAL);
  g_model.disableTelemetryWarning = false;
  g_model.rfAlarms.critical = 40;
  g_tmr10ms += 2000;
  receive(TELEMETRY_DATA,
          {0x22, 5, 0, 0x80, 0x5c, 3, 4, 0xfe, 0x81, 0},
          95, REQUEST_SET_NO_RESP);
  EXPECT_TRUE(TELEMETRY_STREAMING());
  EXPECT_EQ(nb4ReadCarState().receiver.value, 8600);
  telemetryWakeup();
  EXPECT_EQ(telemetryState, TELEMETRY_OK);
  for (unsigned i = 0; i < TELEMETRY_TIMEOUT10ms; ++i) {
    ++g_tmr10ms;
    // The module can keep sending a supply-like record after the receiver is
    // gone. It must neither preserve the link nor appear as an RX voltage.
    receive(TELEMETRY_DATA,
            {0x22, 5, 0, 0x80, 0xe6, 0, 5, 0xfc, 0x82, 0xd2, 0},
            96, REQUEST_SET_NO_RESP);
    telemetryInterrupt10ms();
  }
  telemetryWakeup();
  EXPECT_EQ(telemetryState, TELEMETRY_KO);
  EXPECT_EQ(nb4ReadCarState().link.validity, Nb4Validity::Absent);
  EXPECT_EQ(nb4ReadCarState().receiver.validity, Nb4Validity::Absent);
  receive(TELEMETRY_DATA, {0x22, 4, 0xfe, 0x81, 90}, 97, REQUEST_SET_NO_RESP);
  telemetryWakeup();
  EXPECT_EQ(telemetryState, TELEMETRY_OK);
}

TEST_F(Nb4Afhds3Bind, MalformedOrForeignTelemetryCannotCreateReceiverReadings) {
  bindCommit(); learned();
  for (const std::vector<uint8_t>& payload : std::vector<std::vector<uint8_t>>{
      {0x22}, {0x22, 0}, {0x22, 3, 0xfe, 0x81},
      {0x22, 5, 0, 0x80, 8}, {0x22, 4, 0xff, 0x81, 90},
      {0x22, 4, 0xfe, 0x81, 255}, {0x22, 5, 0, 0x80, 255, 255},
      {0x22, 4, 0xfe, 1, 90}, {0x22, 4, 0xfe, 0x80, 90},
      {0x22, 5, 0, 0x81, 8, 2}, {0x24, 4, 0xfe, 0x81, 90}}) {
    receive(TELEMETRY_DATA, payload, 98, REQUEST_SET_NO_RESP);
    EXPECT_FALSE(TELEMETRY_STREAMING());
    EXPECT_FALSE(getReceiverTelemetry(0).qualityAvailable);
    EXPECT_FALSE(getReceiverTelemetry(0).voltageAvailable);
  }
}

TEST_F(Nb4Afhds3Bind, ReceiverRssiAndQualityKeepDifferentUnitsInSensorDiscovery) {
  bindCommit(); learned(); allowNewSensors = true;
  receive(TELEMETRY_DATA, {0x22, 5, 0xfc, 0x82, 0x2c, 1, 4, 0xfe, 0x81, 83},
          99, REQUEST_SET_NO_RESP);
  bool rssi = false, quality = false;
  for (unsigned i = 0; i < MAX_TELEMETRY_SENSORS; ++i) {
    const auto& s = g_model.telemetrySensors[i];
    if (s.id == 0xfc) {
      rssi = true; EXPECT_EQ(s.unit, UNIT_DBM); EXPECT_EQ(telemetryItems[i].value, -75);
    }
    if (s.id == 0xfe) {
      quality = true; EXPECT_EQ(s.unit, UNIT_PERCENT); EXPECT_EQ(telemetryItems[i].value, 83);
    }
  }
  EXPECT_TRUE(rssi); EXPECT_TRUE(quality);
  allowNewSensors = false;
}

#if defined(AUDIO)
TEST(Nb4Afhds3, ReceiverLossHasAnAudibleFallbackWithoutVoiceFilesAndRespectsMute) {
  extern uint32_t simuTestNonSilentAudioSamples;
  SYSTEM_RESET();
  audioQueue.stopAll();
  const auto oldVolume = currentSpeakerVolume;
  currentSpeakerVolume = VOLUME_LEVEL_MAX;
  simuTestNonSilentAudioSamples = 0;
  g_eeGeneral.beepMode = e_mode_all;
  audioEvent(AU_TELEMETRY_LOST);
  audioQueue.wakeup();
  EXPECT_GT(simuTestNonSilentAudioSamples, 0u);
  audioQueue.stopAll();
  simuTestNonSilentAudioSamples = 0;
  g_eeGeneral.beepMode = e_mode_quiet;
  audioEvent(AU_TELEMETRY_LOST);
  audioQueue.wakeup();
  EXPECT_EQ(simuTestNonSilentAudioSamples, 0u);
  g_eeGeneral.beepMode = e_mode_all;
  currentSpeakerVolume = oldVolume;
}
#endif

TEST_F(Nb4Afhds3Bind, OfficialPagesCommitAndLearnedIdentityAreRequired) {
  bindCommit();
  EXPECT_EQ(getBindPhase(0), BindPhase::Searching);
  receive(MODULE_STATE, {4}, 70, REQUEST_SET_EXPECT_ACK);
  EXPECT_FALSE(afhds3::isConnected(0));
  EXPECT_EQ(getBindPhase(0), BindPhase::Searching);
  EXPECT_EQ(g_model.nb4RfSettings[1], 0);
  learned();
  EXPECT_TRUE(afhds3::isConnected(0));
  EXPECT_EQ(getBindPhase(0), BindPhase::Connected);
  EXPECT_EQ(g_model.nb4RfSettings[1], 1);
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x40], 0xc0);
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x41], 0xdb);
  EXPECT_EQ(getModuleMode(0), MODULE_MODE_BIND); // Dialog, not STATE, owns close.
}

TEST_F(Nb4Afhds3Bind, LearnedNotificationMayPrecedeConnection) {
  bindCommit(); learned(0x44);
  EXPECT_FALSE(afhds3::isConnected(0));
  EXPECT_EQ(getBindPhase(0), BindPhase::Confirming);
  receive(MODULE_STATE, {4}, 90, REQUEST_SET_NO_RESP);
  EXPECT_TRUE(afhds3::isConnected(0));
  EXPECT_EQ(getBindPhase(0), BindPhase::Connected);
  setModuleMode(0, MODULE_MODE_NORMAL);
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  setModuleMode(0, MODULE_MODE_BIND);
  EXPECT_EQ(getBindPhase(0), BindPhase::Preparing);
}

TEST_F(Nb4Afhds3Bind, CompletedBindExitsToOfficialNormalModeNotHardwareTest) {
  bindCommit(); learned(0x44);
  receive(MODULE_STATE, {4}, 90, REQUEST_SET_NO_RESP);
  ASSERT_TRUE(afhds3::isConnected(0));
  setModuleMode(0, MODULE_MODE_NORMAL); start(false); pages(false);
  const auto mode = next();
  ASSERT_EQ(mode.command, MODULE_MODE);
  // Operation 7 sends mode 2. Mode 3 belongs to operation 10, whose
  // completion is internal state 12 (wire 0xff), exactly the state observed
  // after closing the bind dialog.
  EXPECT_EQ(mode.value, 2);
  ack(mode, 2);
  receive(MODULE_STATE, {uint8_t(mode.value == 2 ? 4 : 0xff)}, 91, REQUEST_SET_NO_RESP);
  EXPECT_TRUE(afhds3::isConnected(0));
  const auto channels = next(); EXPECT_EQ(channels.command, CHANNELS_FAILSAFE_DATA);
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x40], 0x44);
}

TEST_F(Nb4Afhds3Bind, HardwareTestStateClearsAnEarlierConnection) {
  bindCommit(); learned();
  receive(MODULE_STATE, {4}, 90, REQUEST_SET_NO_RESP);
  ASSERT_TRUE(afhds3::isConnected(0));
  receive(MODULE_STATE, {0xff}, 91, REQUEST_SET_NO_RESP);
  EXPECT_FALSE(afhds3::isConnected(0));
  char status[64]; getStatusString(0, status);
  EXPECT_NE(std::string(status).find("prueba"), std::string::npos);
}

TEST_F(Nb4Afhds3Bind, Observed169ByteReceiverIsSavedWithoutCopyingReservedTail) {
  bindCommit();
  std::vector<uint8_t> payload(169);
  std::copy(nb4DefaultReceiver, nb4DefaultReceiver + 168, payload.begin() + 1);
  payload[0] = 15; payload[0x41] = 0x44; payload[0x42] = 0xdb;
  payload.back() = 0xa5;
  const uint8_t reserved = nb4DefaultReceiver[167];
  // Reject the truncated record immediately below stock's minimum length.
  receive(MODULE_APPLY_CONFIG, std::vector<uint8_t>(payload.begin(), payload.begin() + 167),
          7, REQUEST_SET_EXPECT_ACK);
  EXPECT_EQ(g_model.nb4RfSettings[1], 0);
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  // Observed hardware metadata: type 03, payload 169, sequence 08.
  // Receiver identity here is synthetic; the final reserved byte is ignored
  // by the module, which accepts length >= 168.
  receive(MODULE_APPLY_CONFIG, payload, 8, REQUEST_SET_EXPECT_ACK);
  EXPECT_EQ(g_model.nb4RfSettings[1], 1);
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x40], 0x44);
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x41], 0xdb);
  EXPECT_EQ(g_model.nb4RfSettings[2 + 167], reserved);
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  Nb4TransportDiagnostics d; getDiagnostics(0, d);
  EXPECT_EQ(d.bindRequestType, REQUEST_SET_EXPECT_ACK);
  EXPECT_EQ(d.bindRequestSize, 169);
  const uint8_t expected[] = {0xc0, 0x08, 0x20, 0x05, 0xd2, 0xc0};
  ASSERT_EQ(d.lastTxSize, sizeof(expected));
  EXPECT_EQ(memcmp(d.lastTx, expected, sizeof(expected)), 0);
  EXPECT_FALSE(afhds3::isConnected(0));
  g_tmr10ms += 3001;
  next(); char status[64]; getStatusString(0, status);
  EXPECT_NE(std::string(status).find("sin confirmar"), std::string::npos);
  EXPECT_EQ(getBindPhase(0), BindPhase::Failed);
  EXPECT_FALSE(afhds3::isConnected(0));
  // A timeout must not destroy the learned identity or report it connected.
  EXPECT_EQ(g_model.nb4RfSettings[1], 1);
  setModuleMode(0, MODULE_MODE_NORMAL);
  restart(); start(false); pages(false);
  auto run = next(); ASSERT_EQ(run.command, MODULE_MODE); ASSERT_EQ(run.value, 2);
  ack(run, 2);
  receive(MODULE_STATE, {4}, 90, REQUEST_SET_NO_RESP);
  EXPECT_TRUE(afhds3::isConnected(0));
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x40], 0x44);
}

TEST_F(Nb4Afhds3Bind, LearnedSetRequestIsConfirmedAndSurvivesRestart) {
  bindCommit();
  learned(0x44, REQUEST_SET_EXPECT_DATA);
  EXPECT_EQ(g_model.nb4RfSettings[1], 1);
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x40], 0x44);
  EXPECT_FALSE(afhds3::isConnected(0));
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  Nb4TransportDiagnostics d; getDiagnostics(0, d);
  // Command 5 has no response builder, so SET-with-data receives an empty
  // DATA response.
  const uint8_t expected[] = {0xc0, 0x51, 0x10, 0x05, 0x99, 0xc0};
  ASSERT_EQ(d.lastTxSize, sizeof(expected));
  EXPECT_EQ(memcmp(d.lastTx, expected, sizeof(expected)), 0);
  learned(0x44, REQUEST_SET_EXPECT_DATA); // Peer may retry if our reply is lost.
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  getDiagnostics(0, d);
  ASSERT_EQ(d.lastTxSize, sizeof(expected));
  EXPECT_EQ(memcmp(d.lastTx, expected, sizeof(expected)), 0);
  setModuleMode(0, MODULE_MODE_NORMAL);
  restart(); start(false); pages(false);
  auto run = next(); ASSERT_EQ(run.command, MODULE_MODE); ASSERT_EQ(run.value, 2);
  ack(run, 2);
  receive(MODULE_STATE, {4}, 90, REQUEST_SET_NO_RESP);
  EXPECT_TRUE(afhds3::isConnected(0));
  EXPECT_EQ(g_model.nb4RfSettings[2 + 0x40], 0x44);
}

TEST_F(Nb4Afhds3Bind, EmptyPeerResponsePreservesOurPendingRequestAndRejectsShortIdentity) {
  bindCommit(); g_tmr10ms += 21;
  auto query = next(); ASSERT_EQ(query.command, MODULE_STATE);
  receive(MODULE_APPLY_CONFIG, {15}, 0x51, REQUEST_SET_EXPECT_DATA);
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  Nb4TransportDiagnostics d; getDiagnostics(0, d);
  const uint8_t expected[] = {0xc0, 0x51, 0x10, 0x05, 0x99, 0xc0};
  ASSERT_EQ(d.lastTxSize, sizeof(expected));
  EXPECT_EQ(memcmp(d.lastTx, expected, sizeof(expected)), 0);
  auto retry = next(); EXPECT_EQ(retry.frameNumber, query.frameNumber);
  EXPECT_EQ(retry.command, MODULE_STATE);
  ack(query, 4);
  EXPECT_EQ(g_model.nb4RfSettings[1], 0);
  EXPECT_FALSE(afhds3::isConnected(0));
}

TEST_F(Nb4Afhds3Bind, WrongSequenceWrongCommandAndAckCannotAdvanceAPage) {
  start(); auto f = next(); const auto original = last;
  receive(MODULE_SET_CONFIG, {1}, f.frameNumber + 1); next(); EXPECT_EQ(last, original);
  receive(MODULE_MODE, {2}, f.frameNumber); next(); EXPECT_EQ(last, original);
  receive(MODULE_SET_CONFIG, {}, f.frameNumber, RESPONSE_ACK); next(); EXPECT_EQ(last, original);
  receive(MODULE_STATE, {2}, 98, REQUEST_SET_EXPECT_ACK); next(); EXPECT_EQ(last, original);
  ack(f, 1); f = next(); EXPECT_EQ(f.command, MODULE_SET_CONFIG); EXPECT_EQ(last[6], 1);
}

TEST_F(Nb4Afhds3Bind, RejectedPageStopsInsteadOfStartingBind) {
  start();
  for (unsigned i = 0; i < 3; ++i) {
    auto page = next(); ASSERT_EQ(page.command, MODULE_SET_CONFIG); ack(page, 0xf8);
  }
  auto f = next(); ASSERT_EQ(f.command, MODULE_SET_CONFIG); ack(f, 0xf8);
  f = next(); EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 1);
  EXPECT_FALSE(afhds3::isConnected(0));
  char status[64]; afhds3::getStatusString(0, status);
  EXPECT_NE(std::string(status).find("rechazada"), std::string::npos);
  EXPECT_EQ(getBindPhase(0), BindPhase::Failed);
}

TEST_F(Nb4Afhds3Bind, TruncatedReplyIsNotSuccess) {
  start(); auto f = next(); receive(MODULE_SET_CONFIG, {}, f.frameNumber);
  f = next(); EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 1);
  EXPECT_FALSE(afhds3::isConnected(0));
}

TEST_F(Nb4Afhds3Bind, CancelIgnoresLateReceiverAndRestoresStandby) {
  bindCommit(); setModuleMode(0, MODULE_MODE_NORMAL); learned();
  EXPECT_EQ(g_model.nb4RfSettings[1], 0);
  start(false);
  EXPECT_FALSE(afhds3::isConnected(0));
}

TEST_F(Nb4Afhds3Bind, CancelDuringConfigurationDoesNotAcceptOldPageReply) {
  start(); auto old = next(); setModuleMode(0, MODULE_MODE_NORMAL);
  auto f = next(); EXPECT_EQ(f.command, MODULE_READY);
  ack(old, 1); next(); EXPECT_EQ(last[4], MODULE_READY);
  ack(f, 2); f = next(); EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 1);
}

TEST_F(Nb4Afhds3Bind, OneWaySavesReceiverWithoutInventingConnection) {
  g_model.moduleData[0].afhds3.telemetry = false;
  bindCommit(); learned();
  EXPECT_FALSE(afhds3::isConnected(0));
  EXPECT_EQ(g_model.nb4RfSettings[1], 1);
  EXPECT_EQ(getBindPhase(0), BindPhase::ManualFinish);
  g_tmr10ms += 3001;
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  EXPECT_EQ(getBindPhase(0), BindPhase::ManualFinish);
  setModuleMode(0, MODULE_MODE_NORMAL); start(false); pages(false);
  auto f = next(); EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 2);
}

TEST_F(Nb4Afhds3Bind, SavedReceiverUsesUpdatePagesAndRunAfterRestart) {
  bindCommit(); learned(0x72);
  setModuleMode(0, MODULE_MODE_NORMAL); restart(); start(false);
  pages(false);
  EXPECT_EQ(last[7 + 0x40], 0x72);
  auto f = next(); ASSERT_EQ(f.command, MODULE_MODE); ASSERT_EQ(f.value, 2); ack(f, 2);
  f = next(); EXPECT_EQ(f.command, CHANNELS_FAILSAFE_DATA); EXPECT_EQ(f.value, 1);
  EXPECT_EQ(last[6], 2); EXPECT_EQ(last.size(), 13u);
}

TEST_F(Nb4Afhds3Bind, LearnedDataAndOutputsSurviveYamlAndStayWithTheirCar) {
  bindCommit(); learned(0x66);
  auto cfg = getConfig(0); cfg->v0.PWMFrequency.Frequency = 73;
  Nb4RfConfig persisted; persisted.load(*cfg); cfg->v0.PWMFrequency.Frequency = 73; cfg->v0.SignalStrengthRCChannelNb = 1; persisted.save(*cfg);
  const auto before = g_model;
  YamlTreeWalker writer; std::string yaml;
  writer.reset(get_modeldata_nodes(), reinterpret_cast<uint8_t*>(&g_model));
  ASSERT_TRUE(writer.generate([](void* out, const char* str, size_t len) {
    static_cast<std::string*>(out)->append(str, len); return true;
  }, &yaml));
  memset(&g_model, 0, sizeof(g_model));
  YamlTreeWalker reader; reader.reset(get_modeldata_nodes(), reinterpret_cast<uint8_t*>(&g_model));
  YamlParser parser; parser.init(YamlTreeWalker::get_parser_calls(), &reader);
  ASSERT_EQ(parser.parse(yaml.c_str(), yaml.size()), YamlParser::CONTINUE_PARSING);
  EXPECT_EQ(memcmp(before.nb4RfSettings, g_model.nb4RfSettings, sizeof(g_model.nb4RfSettings)), 0);
  persisted.load(*cfg); EXPECT_TRUE(persisted.bound); EXPECT_EQ(cfg->v0.PWMFrequency.Frequency, 73); EXPECT_EQ(cfg->v0.SignalStrengthRCChannelNb, 1);
  MODEL_RESET(); persisted.load(*cfg); EXPECT_FALSE(persisted.bound); EXPECT_NE(persisted.receiver[0x40], 0x66);
}

TEST_F(Nb4Afhds3Bind, EnhancedFamilyBuildsItsOwnPortsAndReceiverTemplate) {
  g_model.moduleData[0].afhds3.phyMode = ROUTINE_FLCR1_18CH;
  start(); auto cfg = getConfig(0); cfg->v1.PWMFrequenciesV1.PWMFrequencies[0] = 333;
  cfg->v1.NewPortTypes[0] = SES_NPT_SBUS;
  auto f = next(); ack(f, 1); f = next();
  EXPECT_EQ(last.size(), 85u); EXPECT_EQ(last[7], 1);
  EXPECT_EQ(last[8] | (last[9] << 8), 333); EXPECT_EQ(last[5 + 71], SES_NPT_SBUS);
  ack(f, 3); f = next(); ack(f, 7); f = next();
  EXPECT_EQ(last[7 + 0x3a], 0x19); EXPECT_EQ(last[7 + 0x8b], 0x30);
}

TEST(Nb4Afhds3, OfficialLinkDirectionMatchesWirePagesAndPersistedReceiver) {
  SYSTEM_RESET(); MODEL_RESET();
  g_model.moduleData[0].type = MODULE_TYPE_FLYSKY_AFHDS3;
  g_model.moduleData[0].channelsCount = -6;
  // Two-way link is 1 on the RF standard field, and 3 on both the Classic
  // and Enhanced telemetry fields. The one-way serializer uses 0.
  for (uint8_t phy : {uint8_t(CLASSIC_FLCR1_18CH), uint8_t(ROUTINE_FLCR1_18CH)}) {
    for (bool twoWay : {false, true}) {
      memset(g_model.nb4RfSettings, 0, sizeof(g_model.nb4RfSettings));
      g_model.moduleData[0].afhds3.phyMode = phy;
      g_model.moduleData[0].afhds3.telemetry = twoWay;
      Config_u cfg{}; Nb4RfConfig config; config.load(cfg);
      uint8_t page[170]; ASSERT_EQ(config.page(3, true, cfg, page), 170);
      const uint8_t expected = twoWay ? 3 : 0;
      EXPECT_EQ(page[2 + 0x44], expected);
      std::vector<uint8_t> learned(169);
      std::copy(page + 2, page + 170, learned.begin() + 1);
      learned[0] = 15;
      ASSERT_TRUE(config.acceptReceiver(learned.data(), learned.size(), cfg));
      EXPECT_EQ(g_model.nb4RfSettings[2 + 0x44], expected);
      Nb4RfConfig restored; restored.load(cfg); ASSERT_TRUE(restored.bound);
      ASSERT_EQ(restored.page(3, false, cfg, page), 170);
      EXPECT_EQ(page[2 + 0x44], expected);
    }
  }
}

TEST_F(Nb4Afhds3Bind, ConfigPagesUseRealFailsafeAndChannelCount) {
  g_model.moduleData[0].channelsCount = -4;
  g_model.moduleData[0].failsafeMode = FAILSAFE_CUSTOM;
  g_model.failsafeChannels[0] = -321;
  g_model.failsafeChannels[1] = FAILSAFE_CHANNEL_HOLD;
  g_model.failsafeChannels[2] = FAILSAFE_CHANNEL_NOPULSE;
  start(); auto f = next(); ack(f, 1); f = next(); ack(f, 3); f = next();
  EXPECT_EQ(last[5 + 23], 4);
  EXPECT_EQ(int16_t(last[29] | last[30] << 8), -3210);
  EXPECT_EQ(last[31] | last[32] << 8, 0x8000);
  EXPECT_EQ(last[33] | last[34] << 8, 0x8001);
}

TEST(Nb4Afhds3, LongEscapedOfficialPageFitsAndUndersizedBufferIsProtected) {
  uint8_t payload[170]; memset(payload, 0xc0, sizeof(payload));
  uint8_t wire[MODULE_BUFFER_SIZE + 1]{}; wire[MODULE_BUFFER_SIZE] = 0xaa;
  FrameTransport transport; transport.init(wire, ADDR, true, MODULE_BUFFER_SIZE);
  transport.putFrame(MODULE_SET_CONFIG, REQUEST_SET_EXPECT_DATA, payload, sizeof(payload), 0xdb);
  EXPECT_GT(transport.getFrameSize(), 340u); EXPECT_EQ(wire[MODULE_BUFFER_SIZE], 0xaa);
  uint8_t decoded[240]{}, count = 0; bool complete = false;
  for (unsigned i = 0; i < transport.getFrameSize(); ++i)
    complete = transport.processTelemetryData(wire[i], decoded, count, sizeof(decoded)) || complete;
  ASSERT_TRUE(complete); EXPECT_EQ(count, 177); EXPECT_EQ(memcmp(decoded + 5, payload, 170), 0);
  memset(wire, 0xaa, sizeof(wire)); transport.init(wire, ADDR, true, 16);
  transport.putFrame(MODULE_SET_CONFIG, REQUEST_SET_EXPECT_DATA, payload, sizeof(payload), 1);
  EXPECT_EQ(transport.getFrameSize(), 0u); EXPECT_EQ(wire[16], 0xaa);
}

TEST(Nb4Afhds3, QueuedRequestsWaitForTheirOwnResponse) {
  uint8_t buffer[MODULE_BUFFER_SIZE]{}; Transport transport;
  transport.init(buffer, nullptr, ADDR);
  transport.putFrame(CHANNELS_FAILSAFE_DATA, REQUEST_SET_NO_RESP);
  transport.enqueue(MODULE_MODE, REQUEST_SET_EXPECT_DATA, true, 2);
  ASSERT_TRUE(transport.processQueue()); bool error = false;
  EXPECT_TRUE(transport.handleRetransmissions(error)); EXPECT_FALSE(error);
}

TEST_F(Nb4Afhds3Bind, ReceiverOffDoesNotStopLiveChannelUpdates) {
  bindCommit(); learned(); setModuleMode(0, MODULE_MODE_NORMAL);
  restart(); start(false); pages(false); auto f = next(); ack(f, 2);
  g_tmr10ms += 21; f = next(); ASSERT_EQ(f.command, MODULE_STATE);
  for (unsigned i = 0; i < 6; ++i) f = next();
  EXPECT_EQ(f.command, CHANNELS_FAILSAFE_DATA);
  EXPECT_FALSE(afhds3::isConnected(0));
}

TEST_F(Nb4Afhds3Bind, ModuleBootMayTakeLongerThanOneRequestTimeout) {
  auto f = next(); ASSERT_EQ(f.command, MODULE_READY);
  for (unsigned i = 0; i < 20; ++i) {
    f = next(); ASSERT_EQ(f.command, MODULE_READY);
  }
  ack(f, 2); f = next(); EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 1);
}

TEST_F(Nb4Afhds3Bind, BindTimeoutStopsAndDoesNotSaveAnUnknownReceiver) {
  bindCommit(); g_tmr10ms += 3001; auto f = next();
  EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 1);
  EXPECT_EQ(g_model.nb4RfSettings[1], 0);
  EXPECT_EQ(getBindPhase(0), BindPhase::Failed);
  learned(); EXPECT_EQ(g_model.nb4RfSettings[1], 0);
}

TEST_F(Nb4Afhds3Bind, ChangingFamilyCannotReuseTheOtherFamiliesIdentity) {
  bindCommit(); learned(0x77); setModuleMode(0, MODULE_MODE_NORMAL);
  g_model.moduleData[0].afhds3.phyMode = ROUTINE_FLCR1_18CH;
  applyModelConfig(0);
  start(false); receive(MODULE_STATE, {4}, 75, REQUEST_SET_NO_RESP);
  EXPECT_FALSE(afhds3::isConnected(0));
  Nb4RfConfig model; Config_u cfg{}; model.load(cfg);
  EXPECT_FALSE(model.bound); EXPECT_EQ(model.receiver[0x40], 1);
}

TEST_F(Nb4Afhds3Bind, OldUnboundModelCannotReportConnectedFromStaleState) {
  start(false); receive(MODULE_STATE, {4}, 33, REQUEST_SET_EXPECT_ACK);
  EXPECT_FALSE(afhds3::isConnected(0));
}

TEST_F(Nb4Afhds3Bind, LiveFailsafeChangesReconfigureBeforeResumingChannels) {
  bindCommit(); learned(); setModuleMode(0, MODULE_MODE_NORMAL);
  restart(); start(false); pages(false); auto f = next(); ack(f, 2);
  g_model.moduleData[0].failsafeMode = FAILSAFE_CUSTOM;
  g_model.failsafeChannels[0] = 200;
  f = next(); EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 1);
  ack(f, 2); pages(false);
  f = next(); EXPECT_EQ(f.command, MODULE_MODE); EXPECT_EQ(f.value, 2);
}

TEST_F(Nb4Afhds3Bind, AnswersThePeersReadyQueryWithoutLosingItsOwnRequest) {
  auto query = next();
  receive(MODULE_READY, {}, 0x69, REQUEST_GET_DATA);
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  Nb4TransportDiagnostics d; getDiagnostics(0, d);
  const uint8_t expected[] = {0xc0, 0x69, 0x10, 0x01, 0x02, 0x83, 0xc0};
  ASSERT_EQ(d.lastTxSize, sizeof(expected));
  EXPECT_EQ(memcmp(d.lastTx, expected, sizeof(expected)), 0);
  EXPECT_FALSE(afhds3::isConnected(0));
  auto repeated = next(); EXPECT_EQ(repeated.frameNumber, query.frameNumber);
  ack(query, 2); auto mode = next(); EXPECT_EQ(mode.command, MODULE_MODE);
  EXPECT_EQ(mode.value, 1);
}

TEST_F(Nb4Afhds3Bind, AnswersCapturedConfigQueryWithoutReplacingPendingHandshake) {
  auto query = next();
  // Real module capture: c0 7d 01 04 01 7c c0, repeated until answered.
  receive(MODULE_SET_CONFIG, {1}, 0x7d, REQUEST_GET_DATA);
  receive(MODULE_SET_CONFIG, {3}, 0x7e, REQUEST_GET_DATA);
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  Nb4TransportDiagnostics d; getDiagnostics(0, d);
  const uint8_t prefix[] = {0xc0, 0x7d, 0x10, 0x04, 1, 0, 50, 0, 50, 0};
  ASSERT_GE(d.lastTxSize, sizeof(prefix));
  EXPECT_EQ(memcmp(d.lastTx, prefix, sizeof(prefix)), 0);
  EXPECT_FALSE(afhds3::isConnected(0));
  auto repeated = next(); EXPECT_EQ(repeated.frameNumber, query.frameNumber);
  ack(query, 2); EXPECT_EQ(next().command, MODULE_MODE);
}

TEST_F(Nb4Afhds3Bind, ConfigQueryRejectsInvalidPageAndRepliesToPageZeroWithOwnSequence) {
  auto query = next();
  receive(MODULE_SET_CONFIG, {4}, 0x60, REQUEST_GET_DATA);
  receive(MODULE_SET_CONFIG, {}, 0x61, REQUEST_GET_DATA);
  receive(MODULE_SET_CONFIG, {1, 2}, 0x62, REQUEST_GET_DATA);
  next();
  Nb4TransportDiagnostics d; getDiagnostics(0, d);
  EXPECT_EQ(d.lastTx[2], REQUEST_GET_DATA);
  receive(MODULE_SET_CONFIG, {0}, 0x63, REQUEST_GET_DATA);
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  getDiagnostics(0, d);
  const uint8_t expected[] = {0xc0, 0x63, 0x10, 0x04, 0x00, 0x88, 0xc0};
  ASSERT_EQ(d.lastTxSize, sizeof(expected));
  EXPECT_EQ(memcmp(d.lastTx, expected, sizeof(expected)), 0);
  ack(query, 2); EXPECT_EQ(next().command, MODULE_MODE);
}

TEST_F(Nb4Afhds3Bind, DiagnosticsDistinguishNoBytesCorruptBytesAndUnmatchedReplies) {
  auto query = next(); Nb4TransportDiagnostics d; getDiagnostics(0, d);
  EXPECT_EQ(d.rxBytes, 0u); EXPECT_EQ(d.rxFrames, 0u); EXPECT_GT(d.txFrames, 0u);
  for (uint8_t byte : {0xc0, 0x69, 0x10, 0x01, 0x02, 0x84, 0xc0})
    ProtoDriver.processData(context, byte, rxBuffer, &rxCount);
  getDiagnostics(0, d); EXPECT_EQ(d.rxBytes, 7u); EXPECT_EQ(d.rxFrames, 0u);
  receive(MODULE_READY, {2}, query.frameNumber + 1);
  getDiagnostics(0, d); EXPECT_EQ(d.rxFrames, 1u); EXPECT_EQ(d.unmatched, 1u);
  EXPECT_EQ(d.command, MODULE_READY); EXPECT_EQ(d.type, RESPONSE_DATA);
  ack(query, 2); getDiagnostics(0, d);
  EXPECT_EQ(d.rxFrames, 2u); EXPECT_EQ(d.unmatched, 1u);
}

TEST_F(Nb4Afhds3Bind, UartFaultIsVisibleAndReleasedContextDoesNotCrashOnNextTick) {
  next(); nb4::Nb4RfController::notifyUartErrorFromIsr();
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  char status[64]; getStatusString(0, status);
  EXPECT_STREQ(status, "Error de UART RF");
  ProtoDriver.sendPulses(context, nullptr, nullptr, 0);
  EXPECT_FALSE(ProtoDriver.txCompleted(context));
  ProtoDriver.processData(context, 0xc0, rxBuffer, &rxCount);
  restart(); start(); pages();
}
#endif
