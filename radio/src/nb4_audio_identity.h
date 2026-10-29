/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
struct Nb4AudioIdentity {
  bool installed=false,pending=false;
  char language[3]{},release[32]{};
};
void nb4AudioIdentityLoad();
const Nb4AudioIdentity& nb4AudioIdentity();
