/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#if defined(RADIO_NB4_FAMILY)

#include <stdint.h>

enum class Nb4RouteState : uint8_t {
  Available,

  NotBuiltYet,
};

struct Nb4Route {
  const char* path;      // Exact ASCII key
  const char* labelEs;
  const char* labelEn;
  Nb4RouteState state;

  const char* reasonEs;
  const char* reasonEn;
  void (*open)();              // nullptr when NotBuiltYet

  bool (*available)();   // nullptr means always available

  uint8_t tab;
};

enum class Nb4RouteAccess : uint8_t {
  ModelData,
  RadioOnly,
  Recovery,    // Recovery and diagnostics are always available
};

Nb4RouteAccess nb4RouteAccessOf(const char* path);

bool nb4RouteIsOpenable(const Nb4Route& route);

struct Nb4Section2 {
  const char* id;
  const char* labelEs;
  const char* labelEn;
  uint8_t icon;
};

const Nb4Route* nb4Routes(unsigned* count);
const Nb4Section2* nb4Sections(unsigned* count);

void (*nb4SingleDestinationOf(const char* sectionId))();

bool nb4OpenRoute(const char* path);

unsigned nb4RoutesOfSection(const char* sectionId, const Nb4Route** out,
                            unsigned max);

void nb4OpenQuickAccessModal();

struct Nb4QuickEntry { const char* path; const char* labelEs; const char* labelEn; uint8_t icon; };
const Nb4QuickEntry* nb4QuickAccessDefaults(unsigned* count);

void nb4OpenSettingsModal();

#endif  // RADIO_NB4_FAMILY
