/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace afhds3 { class Transport; }
bool nb4RfUpdateCommand(uint8_t command, const uint8_t* payload, size_t length,
                         char* output, size_t capacity);
void nb4RfUpdateService();
void nb4RfUpdateUsbDisconnected();
void nb4RfUpdateReceive(uint8_t type, uint8_t opcode, uint8_t sequence,
                        const uint8_t*, size_t);
#if defined(NB4_RF_UPDATE_BENCH)
void nb4RfUpdateRxByte();
#endif
// Called on the mixer task before live traffic. Never opens a storage file.
bool nb4RfUpdateFrame(afhds3::Transport& transport);
bool nb4RfUpdateStorageOwned();
bool nb4RfUpdateDrivingLocked();
bool nb4RfUpdateWriteSupported(bool receiver);
int nb4RfUpdateCandidate(bool receiver);
void nb4RfUpdateArmBench();
bool nb4RfUpdateRecoverySupported();
bool nb4RfUpdatePrepared();
bool nb4RfUpdateNeedsRecovery();
bool nb4RfUpdateLocalStart(bool recovery);
// Catalog image on the card that is newer than a connected, qualified
// component; refreshed by the service, never by the caller's thread.
int nb4RfUpdateInstallable();
// Journals a local operation for that image and starts it.
bool nb4RfUpdateLocalInstall();
int nb4RfUpdatePercent();
const char* nb4RfUpdateBuildKind();
#if defined(SIMU)
void nb4RfUpdateTestReset();
void nb4RfUpdateTestBeginValidation(bool recovery);
bool nb4RfUpdateTestEntryProbeActive();
void nb4RfUpdateTestRequireRecovery(bool allowInventory = false);
void nb4RfUpdateTestStartModule();
#endif
