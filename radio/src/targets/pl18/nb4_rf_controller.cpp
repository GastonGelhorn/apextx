/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "nb4_rf_controller.h"

#if defined(RADIO_NB4)

#include "edgetx.h"

#define NB4_RF_MARKER __attribute__((used))

#if defined(NB4_RF_TRANSPORT_USART3)
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_linked_usart3 = 3;
#elif defined(NB4_RF_TRANSPORT_USART6)
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_linked_usart6 = 6;
#else
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_linked_unqualified = 0;
#endif

#if defined(NB4_RF_FRAMING_ADDRESSED_SLIP)
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_framing_addressed_slip = 1;
#elif defined(NB4_RF_FRAMING_ADDRESSLESS_SLIP)
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_framing_addressless_slip = 2;
#else
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_framing_unqualified = 0;
#endif

#if defined(APEXTX_PUBLIC_RELEASE_BUILD)
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_image_public = 1;
#else
extern "C" NB4_RF_MARKER volatile const uint8_t nb4_rf_image_laboratory = 0;
#endif

#if !defined(SIMU)
#include "delays_driver.h"
#include "hal/gpio.h"
#include "stm32_gpio.h"
#include "stm32_hal_ll.h"
#endif

namespace nb4 {
namespace {

Nb4RfFault fault = Nb4RfFault::None;
bool prepared = false;
bool enabled = false;
bool bootApplied = false;
bool shutdownApplied = false;
volatile bool uartErrorPending = false;
etx_module_state_t* activeTransport = nullptr;

void onUartError()
{
  Nb4RfController::notifyUartErrorFromIsr();
}

#if !defined(SIMU)
gpio_t sharedLine(Nb4RfSharedLine line)
{
  return line == Nb4RfSharedLine::Pd11 ? GPIO_PIN(GPIOD, 11)
                                       : GPIO_PIN(GPIOI, 8);
}

void configureLine(void*, Nb4RfSharedLine line, Nb4RfPinMode mode)
{
  gpio_mode_t gpioMode = GPIO_IN;
  switch (mode) {
    case Nb4RfPinMode::Input: gpioMode = GPIO_IN; break;
    case Nb4RfPinMode::InputPullUp: gpioMode = GPIO_IN_PU; break;
    case Nb4RfPinMode::InputPullDown: gpioMode = GPIO_IN_PD; break;
    case Nb4RfPinMode::OutputPushPull: gpioMode = GPIO_OUT; break;
    case Nb4RfPinMode::OutputOpenDrain: gpioMode = GPIO_OD; break;
  }
  gpio_init(sharedLine(line), gpioMode, GPIO_PIN_SPEED_LOW);
}

void writeLine(void*, Nb4RfSharedLine line, Nb4RfLevel level)
{
  if (level == Nb4RfLevel::High)
    gpio_set(sharedLine(line));
  else if (level == Nb4RfLevel::Low)
    gpio_clear(sharedLine(line));
}

void waitUs(void*, uint32_t duration)
{
  delay_us(duration);
}
#else
void configureLine(void*, Nb4RfSharedLine, Nb4RfPinMode) {}
void writeLine(void*, Nb4RfSharedLine, Nb4RfLevel) {}
void waitUs(void*, uint32_t) {}
#endif

const Nb4RfHal hardware = {nullptr, configureLine, writeLine, waitUs};

bool apply(const Nb4RfElectricalSequence& sequence)
{
  if (nb4RfApplyElectricalSequence(sequence, hardware)) return true;
  fault = Nb4RfFault::InvalidElectricalSequence;
  prepared = false;
  enabled = false;
  return false;
}

}  // namespace

bool Nb4RfController::prepare()
{
#if defined(NB4_RF_TRANSPORT_USART3)
  if (nb4_rf_linked_usart3 != 3) return false;
#elif defined(NB4_RF_TRANSPORT_USART6)
  if (nb4_rf_linked_usart6 != 6) return false;
#else
  if (nb4_rf_linked_unqualified != 0) return false;
#endif
  if (!nb4RfProfileCanTransmit()) {
    fault = Nb4RfFault::UnqualifiedProfile;
    prepared = false;
    enabled = false;
    return false;
  }
  fault = Nb4RfFault::None;
  if (shutdownApplied) {
    // A stopped or faulted module can be started again without rebooting the
    // whole radio. Re-running boot restores the recovered selector sequence.
    shutdownApplied = false;
    bootApplied = false;
  }
  if (!bootApplied) {
    if (!apply(kNb4QualifiedHardwareProfile.boot)) {
      trip(Nb4RfFault::InvalidElectricalSequence);
      return false;
    }
#if !defined(SIMU)
    // PE3 and PI15 are external UART inversion controls on this board: drive
    // normal polarity explicitly before starting the UART and enabling
    // module power.
    for (gpio_t pin : {GPIO_PIN(GPIOE, 3), GPIO_PIN(GPIOI, 15)}) {
      gpio_clear(pin);
      gpio_init(pin, GPIO_OUT, GPIO_PIN_SPEED_VERY_HIGH);
    }
#endif
    bootApplied = true;
  }
  prepared = true;
  return prepared;
}

etx_module_state_t* Nb4RfController::startTransport(
  uint8_t module, const etx_serial_init& baseParams)
{
  if (module != INTERNAL_MODULE || activeTransport || !prepare())
    return nullptr;

  const auto& transport = nb4RfSelectedTransport();
  const auto& framing = nb4RfSelectedFraming();
  if (transport.baudrate == 0 || framing.framing == Nb4RfFraming::Unknown ||
      framing.cadenceUs == 0) {
    trip(Nb4RfFault::UnqualifiedProfile);
    return nullptr;
  }

  etx_serial_init params(baseParams);
  params.baudrate = transport.baudrate;
  params.polarity = framing.uartInverted ? ETX_Pol_Inverted : ETX_Pol_Normal;
  activeTransport =
    modulePortInitSerial(module, ETX_MOD_PORT_UART, &params, false);
  if (!activeTransport) {
    trip(Nb4RfFault::UartInitFailed);
    return nullptr;
  }

#if !defined(SIMU) && defined(INTMODULE_USART)
  // Both UART pins need AF, push-pull, very high speed and a pull-up. The
  // shared STM32 serial driver clears PUPDR during init, so restore these
  // electrical settings afterwards on every start, including recovery from
  // UART faults.
  for (gpio_t pin : {INTMODULE_TX_GPIO, INTMODULE_RX_GPIO}) {
    auto port = gpio_get_port(pin);
    const auto mask = 1u << gpio_get_pin(pin);
    LL_GPIO_SetPinOutputType(port, mask, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(port, mask, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinSpeed(port, mask, LL_GPIO_SPEED_FREQ_VERY_HIGH);
  }
#endif

  auto serial = modulePortGetSerialDrv(activeTransport->rx);
  auto serialCtx = modulePortGetCtx(activeTransport->rx);
  if (!serial || !serial->setErrorCb || !serialCtx) {
    trip(Nb4RfFault::UartInitFailed);
    return nullptr;
  }
  uartErrorPending = false;
  serial->setErrorCb(serialCtx, onUartError);
  return activeTransport;
}

void Nb4RfController::stopTransport(etx_module_state_t* state)
{
  if (!state || state != activeTransport) return;
  modulePortDeInit(activeTransport);
  activeTransport = nullptr;
  uartErrorPending = false;
  prepared = false;
}

void Nb4RfController::setEnabled(bool requested)
{
  if (!requested && shutdownApplied) {
    enabled = false;
    return;
  }
  if (!nb4RfProfileCanTransmit() || (requested && !prepared)) {
    fault = Nb4RfFault::UnqualifiedProfile;
    enabled = false;
    return;
  }

  if (apply(requested ? kNb4QualifiedHardwareProfile.enable
                      : kNb4QualifiedHardwareProfile.disable)) {
    enabled = requested;
  } else {
    trip(Nb4RfFault::InvalidElectricalSequence);
  }
}

void Nb4RfController::notifyUartErrorFromIsr()
{
  // ISR contract: record only. De-init, GPIO transitions and profile delays
  // are deliberately deferred to the RF scheduler context.
  uartErrorPending = true;
}

bool Nb4RfController::servicePendingFault()
{
  if (!uartErrorPending) return false;
  uartErrorPending = false;
  trip(Nb4RfFault::UartError);
  return true;
}

void Nb4RfController::trip(Nb4RfFault reason)
{
  fault = reason;
  if (activeTransport) {
    modulePortDeInit(activeTransport);
    activeTransport = nullptr;
  }
  if (nb4RfProfileCanTransmit() && !shutdownApplied) {
    apply(kNb4QualifiedHardwareProfile.shutdown);
    shutdownApplied = true;
  }
  prepared = false;
  enabled = false;
  bootApplied = false;
  uartErrorPending = false;
}

void Nb4RfController::shutdown()
{
  if (activeTransport) {
    modulePortDeInit(activeTransport);
    activeTransport = nullptr;
  }
  if (nb4RfProfileCanTransmit() && !shutdownApplied) {
    apply(kNb4QualifiedHardwareProfile.shutdown);
    shutdownApplied = true;
  }
  prepared = false;
  enabled = false;
  bootApplied = false;
  uartErrorPending = false;
}

Nb4RfFault Nb4RfController::getFault()
{
  return fault;
}

}  // namespace nb4

#endif  // RADIO_NB4
