/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#if defined(RADIO_NB4_FAMILY)

#include <stdint.h>
#include <string>

#include "nb4_i18n.h"

enum class Nb4RouteState : uint8_t {
  Available,

  NotBuiltYet,
};

enum class Nb4RouteAccess : uint8_t {
  ModelData,
  RadioOnly,
  Recovery,
};

struct Nb4Route {
  const char* path;      // Exact ASCII key
  Nb4Str label;
  Nb4RouteState state;

  Nb4Str reason;         // nullptr when the route needs no explanation
  void (*open)();              // nullptr when NotBuiltYet

  bool (*available)();   // nullptr means always available

  uint8_t tab;
  Nb4RouteAccess access = Nb4RouteAccess::ModelData;
  const char* destination = nullptr;
  Nb4Str help = nullptr;
  bool shortcut = true;
  // Presentation category may change; path/route_id must remain stable on disk.
  const char* section = nullptr;
};

Nb4RouteAccess nb4RouteAccessOf(const char* path);

bool nb4RouteIsOpenable(const Nb4Route& route);

struct Nb4Section2 {
  const char* id;
  Nb4Str label;
  uint8_t icon;
  const char* parent = nullptr;
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
bool nb4RouteInSection(const Nb4Route& route, const char* sectionId);
std::string nb4RouteScope(const Nb4Route& route);

void nb4OpenQuickAccessModal();

struct Nb4QuickEntry { const char* path; Nb4Str label; uint8_t icon; };
const Nb4QuickEntry* nb4QuickAccessDefaults(unsigned* count);

void nb4OpenSettingsModal();
void nb4OpenSettingsSection(const char* section);
void nb4OpenAppearance();
void nb4OpenScreens();
void nb4OpenQuickAccessSetup();
void nb4OpenTimers();
void nb4OpenSessionResets();
void nb4OpenHelp(const char* path = nullptr, bool contextual = false);
bool nb4RouteInQuickAccess(const Nb4Route& route);
bool nb4RouteInSettings(const Nb4Route& route);
void nb4OpenTemplates();

constexpr unsigned NB4_QUICK_ACCESS_COUNT = 8;
uint32_t nb4RouteId(const char* path);
const Nb4Route* nb4RouteById(uint32_t id);
void nb4QuickAccessReset();
void nb4QuickAccessNormalize();
bool nb4QuickAccessSet(unsigned slot, uint32_t id);
void nb4QuickAccessMove(unsigned slot, int direction);

// Strings used by the NB4 menu shell are part of the regular EN/ES catalogue.
const char* nb4RouteHelp(const Nb4Route& route);
std::string nb4QuickAccessLabel(const Nb4Route& route);
// Compact, fixed-size label used only by icon grids. Destination pages retain
// their complete translated title.
const char* nb4AppTileLabel(const char* key, const char* fallback);

#endif  // RADIO_NB4_FAMILY
