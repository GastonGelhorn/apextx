/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <mutex>
// Shared by the simulator interface loop and the headless preview API.
extern std::recursive_mutex simuUiMutex;
