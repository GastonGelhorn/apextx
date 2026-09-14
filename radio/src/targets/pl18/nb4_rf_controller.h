/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include "hal/module_port.h"
#include "nb4_rf_profile.h"

namespace nb4 {

enum class Nb4RfFault : uint8_t {
  None,
  UnqualifiedProfile,
  InvalidElectricalSequence,
  UartInitFailed,
  UartError,
};

class Nb4RfController final {
 public:
  static bool prepare();
  static etx_module_state_t* startTransport(uint8_t module,
                                            const etx_serial_init& baseParams);
  static void stopTransport(etx_module_state_t* state);
  static void setEnabled(bool enabled);
  static void notifyUartErrorFromIsr();
  static bool servicePendingFault();
  static void trip(Nb4RfFault fault);
  static void shutdown();
  static Nb4RfFault getFault();
};

}  // namespace nb4
