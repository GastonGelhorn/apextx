/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "gtests.h"

#if defined(RADIO_NB4_FAMILY)
#include "nb4_routes.h"

#include "location.h"
#include "nb4_model_compat.h"
#include "sdcard.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <set>
#include <string>
#include <vector>

TEST(Nb4Routes, EveryPathIsLowercaseAsciiSoItCannotDependOnAccents)
{
  unsigned count = 0;
  const Nb4Route* routes = nb4Routes(&count);
  for (unsigned i = 0; i < count; ++i) {
    for (const char* c = routes[i].path; *c; ++c) {
      const unsigned char ch = (unsigned char)*c;
      EXPECT_LT(ch, 0x80u);
      EXPECT_FALSE(ch >= 'A' && ch <= 'Z');
      EXPECT_TRUE((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                  ch == '_' || ch == '/');
    }
    EXPECT_EQ(strncmp(routes[i].path, "settings/", 9), 0);
  }
}

TEST(Nb4Routes, APendingRouteRefusesAndNeverOpensSomethingElse)
{
  unsigned count = 0;
  const Nb4Route* routes = nb4Routes(&count);

  unsigned pending = 0, available = 0;
  for (unsigned i = 0; i < count; ++i) {
    if (routes[i].state == Nb4RouteState::Available) {
      available += 1;
      EXPECT_NE(routes[i].open, nullptr);

      if (!routes[i].available)
        EXPECT_EQ(routes[i].reasonEs, nullptr);

      if (routes[i].available)
        EXPECT_NE(routes[i].reasonEs, nullptr);

      if (routes[i].available && !routes[i].available())
        EXPECT_FALSE(nb4OpenRoute(routes[i].path));
    } else {
      pending += 1;

      EXPECT_EQ(routes[i].open, nullptr);
      EXPECT_NE(routes[i].reasonEs, nullptr);
      EXPECT_FALSE(nb4OpenRoute(routes[i].path));
    }
    EXPECT_NE(routes[i].labelEs, nullptr);
    EXPECT_NE(routes[i].labelEn, nullptr);

    EXPECT_EQ(routes[i].reasonEs == nullptr, routes[i].reasonEn == nullptr);
  }

  EXPECT_GT(available, pending * 4);

  EXPECT_FALSE(nb4OpenRoute("settings/no/exists"));
  EXPECT_FALSE(nb4OpenRoute(""));
  EXPECT_FALSE(nb4OpenRoute(nullptr));
}

TEST(Nb4Routes, EverySectionHasViewsAndNoRouteIsOrphaned)
{
  unsigned sectionCount = 0, total = 0;
  const Nb4Section2* sections = nb4Sections(&sectionCount);
  unsigned sum = 0;
  for (unsigned i = 0; i < sectionCount; ++i) {
    const Nb4Route* views[32];
    const unsigned n = nb4RoutesOfSection(sections[i].id, views, 32);
    EXPECT_GT(n, 0u);
    EXPECT_LE(n, 32u);
    sum += n;
  }
  nb4Routes(&total);
  EXPECT_EQ(sum, total);
  EXPECT_EQ(nb4RoutesOfSection("no_existe", nullptr, 0), 0u);
  EXPECT_EQ(nb4RoutesOfSection(nullptr, nullptr, 0), 0u);
}

TEST(Nb4Routes, EveryQuickAccessDefaultPointsAtARouteThatExists)
{
  unsigned qcount = 0, rcount = 0;
  const Nb4QuickEntry* quick = nb4QuickAccessDefaults(&qcount);
  const Nb4Route* routes = nb4Routes(&rcount);
  EXPECT_GE(qcount, 6u);

  for (unsigned i = 0; i < qcount; ++i) {
    const char* path = quick[i].path;
    EXPECT_NE(quick[i].labelEs, nullptr);
    EXPECT_NE(quick[i].labelEn, nullptr);

    const char* slash = strchr(path, '/');
    ASSERT_NE(slash, nullptr);

    if (!strchr(slash + 1, '/')) {

      const Nb4Route* views[32];
      const unsigned n = nb4RoutesOfSection(slash + 1, views, 32);
      EXPECT_GT(n, 0u);
      unsigned openable = 0;
      for (unsigned v = 0; v < n && v < 32; ++v)
        if (nb4RouteIsOpenable(*views[v])) openable += 1;
      EXPECT_GT(openable, 0u);
    } else {
      bool found = false;
      for (unsigned r = 0; r < rcount; ++r)
        if (strcmp(routes[r].path, path) == 0) { found = true; break; }
      EXPECT_TRUE(found);
    }
  }
}

TEST(Nb4Routes, ASectionWhoseViewsAllLeadToOnePlaceOpensItDirectly)
{
  unsigned sectionCount = 0;
  const Nb4Section2* sections = nb4Sections(&sectionCount);

  unsigned collapsed = 0, kept = 0;
  for (unsigned i = 0; i < sectionCount; ++i) {
    const Nb4Route* views[32];
    const unsigned n = nb4RoutesOfSection(sections[i].id, views, 32);
    std::set<void*> destinations;

    bool hasPending = false;
    for (unsigned v = 0; v < n && v < 32; ++v) {
      if (views[v]->state == Nb4RouteState::NotBuiltYet) hasPending = true;
      if (!nb4RouteIsOpenable(*views[v])) continue;
      destinations.insert((void*)views[v]->open);
    }
    auto only = nb4SingleDestinationOf(sections[i].id);

    if (hasPending) {
      EXPECT_EQ(only, nullptr);
      kept += 1;
    } else if (destinations.size() == 1) {
      EXPECT_NE(only, nullptr);
      collapsed += 1;
    } else {
      EXPECT_EQ(only, nullptr);
      kept += 1;
    }
  }

  EXPECT_GE(collapsed, 3u);
  EXPECT_GE(kept, 3u);

  EXPECT_EQ(nb4SingleDestinationOf("no_existe"), nullptr);
}

TEST(Nb4Routes, NoTwoViewsOfASectionOpenTheSameThing)
{
  unsigned sectionCount = 0;
  const Nb4Section2* sections = nb4Sections(&sectionCount);

  for (unsigned i = 0; i < sectionCount; ++i) {

    if (nb4SingleDestinationOf(sections[i].id)) continue;

    const Nb4Route* views[32];
    const unsigned n = nb4RoutesOfSection(sections[i].id, views, 32);
    for (unsigned a = 0; a < n && a < 32; ++a) {

      if (!nb4RouteIsOpenable(*views[a])) continue;
      for (unsigned b = a + 1; b < n && b < 32; ++b) {
        if (!nb4RouteIsOpenable(*views[b])) continue;
        if (views[a]->open != views[b]->open) continue;
        EXPECT_NE(views[a]->tab, views[b]->tab);
      }
    }
  }
}

TEST(Nb4Routes, EveryAxisRouteLandsOnItsOwnTab)
{
  struct Expected { const char* path; uint8_t tab; };
  static const Expected kTabs[] = {
      {"settings/steering/general", 2},                 // Center
      {"settings/steering/travel", 0},              // Travel
      {"settings/steering/response", 1},               // Curve
      {"settings/throttle_brake/general", 0},                 // Travel
      {"settings/throttle_brake/throttle", 0},                     // Travel
      {"settings/throttle_brake/brake", 2},                   // Brake
      {"settings/throttle_brake/abs", 2},                     // Brake
      {"settings/throttle_brake/trims", 0},                   // Travel
      {"settings/throttle_brake/nitro_engine", 3},             // Engine
      {"settings/throttle_brake/advanced_mixing", 0},   // Travel
  };

  unsigned count = 0;
  const Nb4Route* all = nb4Routes(&count);

  for (const auto& e : kTabs) {
    const Nb4Route* found = nullptr;
    for (unsigned i = 0; i < count; ++i)
      if (strcmp(all[i].path, e.path) == 0) found = &all[i];
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->tab, e.tab);
  }

  const Nb4Route* steering[8];
  const unsigned ns = nb4RoutesOfSection("steering", steering, 8);
  ASSERT_GT(ns, 1u);
  for (unsigned i = 1; i < ns; ++i)
    EXPECT_EQ(steering[i]->open, steering[0]->open);

  for (unsigned i = 0; i < count; ++i) {
    bool isAxis = false;
    for (const auto& e : kTabs)
      if (strcmp(all[i].path, e.path) == 0) isAxis = true;
    if (isAxis) continue;
    EXPECT_EQ(all[i].tab, 0);
  }
}

TEST(Nb4Routes, OnlyPathsOfThisMapOpenAnything)
{
  const char* bad[] = {
      "garbage/steering",       // Correct shape with the wrong prefix
      "preferences/steering",
      "settings2/steering",
      "/steering",
      "steering",
      "settings/",
      "",
  };
  for (const char* path : bad)
    EXPECT_FALSE(nb4OpenRoute(path));

  unsigned count = 0;
  const Nb4Route* all = nb4Routes(&count);
  bool foundGood = false;
  for (unsigned i = 0; i < count; i += 1)
    if (strcmp(all[i].path, "settings/steering/travel") == 0) foundGood = true;
  EXPECT_TRUE(foundGood);
}

TEST(Nb4Routes, EveryRouteIsClassifiedAndRecoveryIsNeverBlocked)
{
  unsigned count = 0;
  const Nb4Route* all = nb4Routes(&count);
  ASSERT_GT(count, 60u);

  unsigned recovery = 0, radioOnly = 0, modelData = 0;
  for (unsigned i = 0; i < count; i += 1) {
    switch (nb4RouteAccessOf(all[i].path)) {
      case Nb4RouteAccess::Recovery:  recovery += 1; break;
      case Nb4RouteAccess::RadioOnly: radioOnly += 1; break;
      default:                        modelData += 1; break;
    }
  }
  EXPECT_GE(recovery, 6u);
  EXPECT_GT(modelData, 20u);
  EXPECT_GT(radioOnly, 8u);

  EXPECT_EQ(nb4RouteAccessOf("settings/system/calibration"), Nb4RouteAccess::Recovery);
  EXPECT_EQ(nb4RouteAccessOf("settings/controls/monitor"), Nb4RouteAccess::Recovery);
  EXPECT_EQ(nb4RouteAccessOf("settings/models/management"), Nb4RouteAccess::Recovery);
  EXPECT_EQ(nb4RouteAccessOf("settings/system/storage"), Nb4RouteAccess::Recovery);
  EXPECT_EQ(nb4RouteAccessOf("settings/throttle_brake/throttle"), Nb4RouteAccess::ModelData);
  EXPECT_EQ(nb4RouteAccessOf("settings/car/safety"), Nb4RouteAccess::ModelData);
  EXPECT_EQ(nb4RouteAccessOf("settings/receiver_rf/failsafe"), Nb4RouteAccess::ModelData);
  EXPECT_EQ(nb4RouteAccessOf("settings/race/timers"), Nb4RouteAccess::ModelData);
  EXPECT_EQ(nb4RouteAccessOf("settings/display/screens"), Nb4RouteAccess::ModelData);
  EXPECT_EQ(nb4RouteAccessOf("settings/invented/route"), Nb4RouteAccess::ModelData);

  const char* mustRefuse[] = {
      "settings/car/safety", "settings/receiver_rf/rf",
      "settings/receiver_rf/receiver", "settings/receiver_rf/failsafe",
      "settings/controls/trims", "settings/race/timers",
      "settings/race/resets", "settings/throttle_brake/throttle",
      "settings/steering/travel", "settings/display/screens",
  };
  const char* mustAllow[] = {
      "settings/system/calibration", "settings/controls/monitor",
      "settings/models/management", "settings/system/storage",
      "settings/system/general",
  };

  ASSERT_FALSE(nb4ModelBlocked());
  for (unsigned i = 0; i < count; i += 1) {
    const Nb4Route& r = all[i];
    if (r.state != Nb4RouteState::Available) continue;
    for (const char* path : mustRefuse)
      if (strcmp(r.path, path) == 0 && (!r.available || r.available()))
        EXPECT_TRUE(nb4RouteIsOpenable(r));
  }

  const char* broken = "not: [a, valid, model\n";
  const auto dir = std::filesystem::temp_directory_path() /
                   ("nb4-blocked-" + std::to_string(getpid()));
  std::filesystem::create_directories(dir / "MODELS");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      nb4AcceptNewCarModel();
      simuFatfsSetPaths(TESTS_PATH, nullptr);
      std::filesystem::remove_all(path);
    }
  } cleanup{dir};
  {
    std::ofstream f(dir / "MODELS" / "broken.yml");
    f << broken;
  }
  simuFatfsSetPaths(dir.c_str(), nullptr);
  ASSERT_NE(nb4ValidateModelFile("/MODELS/broken.yml"), nullptr);
  ASSERT_TRUE(nb4ModelBlocked());

  for (unsigned i = 0; i < count; i += 1) {
    const Nb4Route& r = all[i];
    if (r.state != Nb4RouteState::Available) continue;
    for (const char* path : mustRefuse)
      if (strcmp(r.path, path) == 0)
        EXPECT_FALSE(nb4RouteIsOpenable(r));
    for (const char* path : mustAllow)
      if (strcmp(r.path, path) == 0)
        EXPECT_TRUE(nb4RouteIsOpenable(r));
  }

  EXPECT_FALSE(nb4OpenRoute("settings/throttle_brake/throttle"));

  nb4AcceptNewCarModel();
  EXPECT_FALSE(nb4ModelBlocked());
}

#endif  // RADIO_NB4_FAMILY
