/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
void nb4RfNoticeService();
void nb4RfNoticeInvalidate();
bool nb4RfNoticeStorageOwned();
int nb4RfNoticeTake();
bool nb4RfNoticePostpone();
#if defined(SIMU)
bool nb4RfNoticeTestDeferred(const uint8_t key[32],bool write);
#endif
