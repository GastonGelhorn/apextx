/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#if defined(RADIO_NB4_FAMILY)

#include <stdint.h>

#include "nb4_i18n.h"

enum class Nb4RouteState : uint8_t {
  Available,

  NotBuiltYet,
};

struct Nb4Route {
  const char* path;      // Exact ASCII key
  Nb4Str label;
  Nb4RouteState state;

  Nb4Str reason;         // nullptr when the route needs no explanation
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
  Nb4Str label;
  uint8_t icon;
};

const Nb4Route* nb4Routes(unsigned* count);
const Nb4Section2* nb4Sections(unsigned* count);

void (*nb4SingleDestinationOf(const char* sectionId))();

bool nb4OpenRoute(const char* path);
const Nb4Route* nb4RouteByPath(const char* path);

// What a full-screen warning can tell the user beyond its own one-line
// message: why it appeared, what to do about it, and the setting that fixes it.
struct Nb4Alert {
  const char* path;   // nullptr when no setting can be opened for this warning
  Nb4Str label;       // Short name for the "Go to" button, null without a path
  Nb4Str advice;      // Why it happened and what to do
};
// nullptr when the warning is not one of the ones we can explain.
const Nb4Alert* nb4AlertFor(const char* title, const char* message);
// Whether the warning's setting exists and can be opened on this model now.
bool nb4AlertCanOpen(const Nb4Alert& alert);

// Alerts run in a nested UI loop of their own, so a route chosen from one is
// kept here and opened by the next regular frame of the main loop.
void nb4DeferRoute(const char* path);
bool nb4RunDeferredRoute();

unsigned nb4RoutesOfSection(const char* sectionId, const Nb4Route** out,
                            unsigned max);

void nb4OpenQuickAccessModal();

struct Nb4QuickEntry { const char* path; Nb4Str label; uint8_t icon; };
const Nb4QuickEntry* nb4QuickAccessDefaults(unsigned* count);

void nb4OpenSettingsModal();

#endif  // RADIO_NB4_FAMILY
