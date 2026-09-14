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

#include "afhds3_transport.h"

#if defined(RADIO_NB4)
#include "targets/pl18/nb4_rf_profile.h"
#endif
#include "edgetx_helpers.h"
#include "debug.h"

#include "board.h"
#include "dataconstants.h"
#if defined(RADIO_NB4)
#include "myeeprom.h"
#endif
#include "mixer_scheduler.h"

// timer is 2 MHz
#if defined(AFHDS3_SLOW)
  // 1000000/57600 = 17,36 us
  #define BITLEN_AFHDS (35)
#else
  // 1000000/115200 = 8,68 us
  #define BITLEN_AFHDS (17)
#endif

#define MAX_RETRIES_AFHDS3 5

namespace afhds3
{

enum AfhdsSpecialChars {
  END = 0xC0,  // Frame end
  START = END,
  ESC_END = 0xDC,  // Escaped frame end - in case END occurs in fame then ESC
                   // ESC_END must be used
  ESC = 0xDB,      // Escaping character
  ESC_ESC = 0xDD,  // Escaping character in case ESC occurs in fame then ESC
                   // ESC_ESC  must be used
};

void FrameTransport::init(void* buffer, uint8_t fAddr, bool noAddress, uint16_t capacity)
{
  this->capacity = capacity;
  frameAddress = fAddr;
  omitAddress = noAddress ? 1 : 0;
  trsp_buffer = (uint8_t*)buffer;
  clear();
}

void FrameTransport::clear()
{
  // reset send buffer
  data_ptr = trsp_buffer;
  overflow = false;

  // reset parser
  esc_state = 0;
}

void FrameTransport::putByte(uint8_t b)
{
  if (data_ptr - trsp_buffer < capacity) *(data_ptr++) = b;
  else overflow = true;
}

void FrameTransport::putBytes(uint8_t* data, int length)
{
  for (int i = 0; i < length; i++) {
    uint8_t byte = data[i];
    crc += byte;
    if (END == byte) {
      putByte(ESC);
      putByte(ESC_END);
    }
    else if (ESC == byte) {
      putByte(ESC);
      putByte(ESC_ESC);
    }
    else {
      putByte(byte);
    }
  }
}

void FrameTransport::putFrame(COMMAND command, FRAME_TYPE frameType,
                              uint8_t* data, uint8_t dataLength,
                              uint8_t frameIndex)
{
  // header
  data_ptr = trsp_buffer;
  overflow = false;

  crc = 0;
  putByte(START);

  if (omitAddress) {
    uint8_t buffer[] = {frameIndex, frameType, command};
    putBytes(buffer, 3);
  } else {
    uint8_t buffer[] = {frameAddress, frameIndex, frameType, command};
    putBytes(buffer, 4);
  }

  // payload
  if (dataLength > 0) {
    putBytes(data, dataLength);
  }

  // footer
  uint8_t crcValue = crc ^ 0xff;
  putBytes(&crcValue, 1);
  putByte(END);
}

uint32_t FrameTransport::getFrameSize()
{
  return overflow ? 0 : data_ptr - trsp_buffer;
}

static bool _checkCRC(const uint8_t* data, uint8_t size)
{
  uint8_t crc = 0;
  //skip start byte
  for (uint8_t i = 1; i < size; i++) {
    crc += data[i];
  }
  return (crc ^ 0xff) == data[size];
}

bool FrameTransport::processTelemetryData(uint8_t byte, uint8_t* rxBuffer,
                                          uint8_t& rxBufferCount,
                                          uint8_t maxSize)
{
  if (rxBufferCount == 0 && byte != START) {
    this->esc_state = 0;
    return false;
  }

  if (byte == ESC) {
    this->esc_state = rxBufferCount;
    return false;
  }

  if (rxBufferCount > 1 && byte == END) {
    if (rxBufferCount >= maxSize || rxBufferCount < (omitAddress ? 5 : 6)) {
      rxBufferCount = 0;
      return false;
    }
    rxBuffer[rxBufferCount++] = byte;

    if (!_checkCRC(rxBuffer, rxBufferCount - 2)) {
      TRACE("AFHDS3 [INVALID CRC]");
      rxBufferCount = 0;
      return false;
    }

    if (omitAddress) {
      if (rxBufferCount >= maxSize) {
        TRACE("AFHDS3 [BUFFER OVERFLOW]");
        rxBufferCount = 0;
        return false;
      }
      memmove(&rxBuffer[2], &rxBuffer[1], rxBufferCount - 1);
      rxBuffer[1] = frameAddress;
      rxBufferCount++;
    }

    return true;
  }

  if (this->esc_state && byte == ESC_END) {
    byte = END;
  }
  else if (esc_state && byte == ESC_ESC) {
    byte = ESC;
  }
  //reset esc index
  this->esc_state = 0;

  if (rxBufferCount >= maxSize) {
    TRACE("AFHDS3 [BUFFER OVERFLOW]");
    rxBufferCount = 0;
  }
  rxBuffer[rxBufferCount++] = byte;
  return false;
}

void CommandFifo::clearCommandFifo()
{
  memclear(commandFifo, sizeof(commandFifo));
  setIndex = getIndex = 0;
}

Frame* CommandFifo::getCommand()
{
  if (isEmpty()) return nullptr;
  return &commandFifo[getIndex];
}

void CommandFifo::enqueueACK(COMMAND command, uint8_t frameNumber)
{
  uint32_t next = nextIndex(setIndex);
  if (next != getIndex) {
    commandFifo[setIndex].command = command;
    commandFifo[setIndex].frameType = FRAME_TYPE::RESPONSE_ACK;
    commandFifo[setIndex].payload = 0;
    commandFifo[setIndex].payloadSize = 0;
    commandFifo[setIndex].frameNumber = frameNumber;
    commandFifo[setIndex].useFrameNumber = true;
    setIndex = next;
  }
}

void CommandFifo::enqueue(COMMAND command, FRAME_TYPE frameType, bool useData,
                          uint8_t byteContent)
{
  uint32_t next = nextIndex(setIndex);
  if (next != getIndex) {
    commandFifo[setIndex].command = command;
    commandFifo[setIndex].frameType = frameType;
    commandFifo[setIndex].payload = byteContent;
    commandFifo[setIndex].payloadSize = useData ? 1 : 0;
    commandFifo[setIndex].frameNumber = 0;
    commandFifo[setIndex].useFrameNumber = false;
    setIndex = next;
  }
}

void CommandFifo::enqueueResponse(COMMAND command, uint8_t frameNumber, uint8_t value,
                                 bool withValue)
{
  uint32_t next = nextIndex(setIndex);
  if (next == getIndex) return;
  commandFifo[setIndex] = {command, RESPONSE_DATA, value, frameNumber, true,
                         uint8_t(withValue ? 1 : 0)};
  setIndex = next;
}

void Transport::init(void* buffer, etx_module_state_t* mod_st, uint8_t fAddr)
{
#if defined(RADIO_NB4)
  const auto& profile = nb4::nb4RfSelectedFraming();
  trsp.init(buffer, fAddr,
            profile.framing == nb4::Nb4RfFraming::AddresslessSlip);
  maxResponseRetries = nb4::nb4RfResponseRetries(profile);
#else
  trsp.init(buffer, fAddr);
  maxResponseRetries = MAX_RETRIES_AFHDS3;
#endif
  this->mod_st = mod_st;
#if defined(RADIO_NB4) && defined(SIMU)
  diagnostics = {};
#endif
  frameIndex = 0;
  clear();
}

void Transport::clear()
{
  // reset frame
  trsp.clear();

  // reset command layer
  fifo.clearCommandFifo();
  acknowledgements.clearCommandFifo();
#if defined(RADIO_NB4)
  responsePending = false;
#endif

  ++frameIndex; // Do not reuse the previous request number after a timeout.
  repeatCount = 0;

  // reset internal state
  operationState = State::UNKNOWN;
}

void Transport::putFrame(COMMAND command, FRAME_TYPE frameType, uint8_t* data,
                          uint8_t dataLength)
{
  operationState = State::SENDING_COMMAND;
  repeatCount = 0;

  pendingIndex = frameIndex;
  pendingCommand = command;
  pendingType = frameType;
  trsp.putFrame(command, frameType, data, dataLength, frameIndex);
  frameIndex++;

  switch (frameType) {
    case FRAME_TYPE::REQUEST_GET_DATA:
    case FRAME_TYPE::REQUEST_SET_EXPECT_ACK:
    case FRAME_TYPE::REQUEST_SET_EXPECT_DATA:
      operationState = State::AWAITING_RESPONSE;
      break;
    default:
      operationState = State::IDLE;
  }
}

void Transport::enqueue(COMMAND command, FRAME_TYPE frameType, bool useData,
                        uint8_t byteContent)
{
  fifo.enqueue(command, frameType, useData, byteContent);
}

void Transport::sendBuffer()
{
#if !defined(SIMU)
  auto drv = modulePortGetSerialDrv(mod_st->tx);
  auto ctx = modulePortGetCtx(mod_st->tx);
  if (!drv->txCompleted(ctx)) {
#if defined(RADIO_NB4) && defined(SIMU)
    ++diagnostics.txBusy;
#endif
    return;
  }
#endif
  if (auto ack = acknowledgements.getCommand()) {
    FrameTransport frame;
    frame.init(ackBuffer, trsp.frameAddress, trsp.omitAddress, sizeof(ackBuffer));
    frame.putFrame(ack->command, ack->frameType, &ack->payload,
                    ack->payloadSize, ack->frameNumber);
#if !defined(SIMU)
    drv->sendBuffer(ctx, ackBuffer, frame.getFrameSize());
#endif
#if defined(RADIO_NB4) && defined(SIMU)
    recordTx(ackBuffer, frame.getFrameSize());
#endif
    acknowledgements.skip();
    return;
  }
#if defined(RADIO_NB4)
  if (responsePending) {
    // Build only after the previous TX completes. The RX task fills a separate
    // mailbox, so a retransmitted request cannot alter an in-flight DMA frame.
    FrameTransport frame;
    frame.init(responseBuffer, trsp.frameAddress, trsp.omitAddress, sizeof(responseBuffer));
    frame.putFrame(responseCommand, RESPONSE_DATA, responsePayload,
                   responseLength, responseIndex);
    responsePending = false;
#if !defined(SIMU)
    drv->sendBuffer(ctx, responseBuffer, frame.getFrameSize());
#endif
#if defined(SIMU)
    recordTx(responseBuffer, frame.getFrameSize());
#endif
    return;
  }
#endif
  if (!trsp.getFrameSize()) return;
#if !defined(SIMU)
  drv->sendBuffer(ctx, (uint8_t*)trsp.trsp_buffer, trsp.getFrameSize());
#endif
#if defined(RADIO_NB4) && defined(SIMU)
  recordTx(trsp.trsp_buffer, trsp.getFrameSize());
#endif
}

#if defined(RADIO_NB4)
bool Transport::queueResponse(COMMAND command, uint8_t index,
                              const uint8_t* payload, uint8_t size)
{
  if (responsePending || size > sizeof(responsePayload)) return false;
  memcpy(responsePayload, payload, size);
  responseCommand = command;
  responseIndex = index;
  responseLength = size;
  responsePending = true;
  return true;
}

#if defined(SIMU)
void Transport::recordTx(const uint8_t* buffer, uint32_t size)
{
  ++diagnostics.txFrames;
  diagnostics.lastTxSize = size < sizeof(diagnostics.lastTx) ? size : sizeof(diagnostics.lastTx);
  memcpy(diagnostics.lastTx, buffer, diagnostics.lastTxSize);
}
#endif
#endif

bool Transport::processQueue()
{
  // check waiting commands
  auto f = fifo.getCommand();
  if (!f) return false;

  if (f->useFrameNumber) {
    trsp.putFrame(f->command, f->frameType, &f->payload, f->payloadSize, f->frameNumber);
  } else {
    putFrame(f->command, f->frameType, &f->payload, f->payloadSize);
  }
  fifo.skip();

  return true;
}

bool Transport::handleRetransmissions(bool& error)
{
  if (operationState == State::AWAITING_RESPONSE) {
    if (repeatCount++ < maxResponseRetries) {
      error = false;
      return true; // re-send
    }

    error = true;
    return false;
  }

  if (operationState == State::UNKNOWN) {
    error = true;
    return false;
  }

  error = false;
  repeatCount = 0;

  return false;
}

bool Transport::handleReply(uint8_t* buffer, uint8_t len)
{
  if (len < 7) return true;

  AfhdsFrame* responseFrame = reinterpret_cast<AfhdsFrame*>(buffer);
#if defined(RADIO_NB4)
  if (responseFrame->frameType == REQUEST_GET_DATA &&
      responseFrame->command == MODULE_READY && len == 7) {
    // A MODULE_READY reply carries 2 when RF is enabled. The peer can query
    // us before answering our own query.
    // Queue the response with the peer's sequence; preserve our pending query.
    acknowledgements.enqueueResponse(MODULE_READY, responseFrame->frameNumber, 2);
    return true;
  }
  if (responseFrame->frameType == REQUEST_SET_EXPECT_DATA &&
      (responseFrame->command == MODULE_STATE ||
       responseFrame->command == MODULE_APPLY_CONFIG ||
       responseFrame->command == TELEMETRY_DATA ||
       responseFrame->command == COMMAND_RESULT)) {
    // SET notifications of both types 2 and 3 are accepted. These
    // command-table entries have no response builder: type 2 needs an
    // empty DATA response, with the peer's sequence, instead of an ACK.
    acknowledgements.enqueueResponse((COMMAND)responseFrame->command,
                                     responseFrame->frameNumber, 0, false);
  }
#endif
  if (responseFrame->frameType == FRAME_TYPE::REQUEST_SET_EXPECT_ACK) {

    // check if such request is not queued
    auto f = acknowledgements.getCommand();
    if (f && f->frameType == FRAME_TYPE::RESPONSE_ACK &&
        f->frameNumber == responseFrame->frameNumber &&
        f->command == responseFrame->command) {

      // absorb retransmission
      TRACE("ACK for frame %02X already queued", responseFrame->frameNumber);
      return true;
    }


    // The mixer sends ACKs when TX is idle. A notification must neither
    // truncate in-flight DMA nor overwrite the request retained for retry.
    acknowledgements.enqueueACK((COMMAND)responseFrame->command,
                                 responseFrame->frameNumber);
  } else if (responseFrame->frameType == FRAME_TYPE::RESPONSE_DATA ||
             responseFrame->frameType == FRAME_TYPE::RESPONSE_ACK) {
    if (operationState != State::AWAITING_RESPONSE ||
        responseFrame->frameNumber != pendingIndex ||
        responseFrame->command != pendingCommand) {
#if defined(RADIO_NB4) && defined(SIMU)
      ++diagnostics.unmatched;
#endif
      return true;
    }
    if (responseFrame->frameType == RESPONSE_DATA || pendingType == REQUEST_SET_EXPECT_ACK)
      operationState = State::IDLE;
  }

  return false;
}

bool Transport::processTelemetryData(uint8_t byte, uint8_t* rxBuffer,
                                     uint8_t& rxBufferCount, uint8_t maxSize)
{
#if defined(RADIO_NB4) && defined(SIMU)
  ++diagnostics.rxBytes;
  if (diagnostics.lastRxSize < sizeof(diagnostics.lastRx)) {
    diagnostics.lastRx[diagnostics.lastRxSize++] = byte;
  } else {
    memmove(diagnostics.lastRx, diagnostics.lastRx + 1, sizeof(diagnostics.lastRx) - 1);
    diagnostics.lastRx[sizeof(diagnostics.lastRx) - 1] = byte;
  }
#endif
  bool has_frame =
      trsp.processTelemetryData(byte, rxBuffer, rxBufferCount, maxSize);
#if defined(RADIO_NB4) && defined(SIMU)
  if (has_frame) {
    const auto* frame = reinterpret_cast<const AfhdsFrame*>(rxBuffer);
    ++diagnostics.rxFrames;
    diagnostics.command = frame->command; diagnostics.type = frame->frameType;
    diagnostics.sequence = frame->frameNumber;
    diagnostics.value = rxBufferCount > 7 ? frame->value : 0;
    if (frame->command == MODULE_APPLY_CONFIG &&
        (frame->frameType == REQUEST_SET_EXPECT_DATA ||
         frame->frameType == REQUEST_SET_EXPECT_ACK ||
         frame->frameType == REQUEST_SET_NO_RESP)) {
      ++diagnostics.bindRequests;
      diagnostics.bindRequestType = frame->frameType;
      diagnostics.bindRequestSize = rxBufferCount - 7;
      diagnostics.bindRequestSequence = frame->frameNumber;
    }
  }
#endif
  if (has_frame && handleReply(rxBuffer, rxBufferCount)) {
    rxBufferCount = 0;
    return false;
  }

  return has_frame;
}
};  // namespace afhds3
