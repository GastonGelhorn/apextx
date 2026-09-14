#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the immutable NB4 RF profile from reviewed evidence."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import re
import tempfile
from pathlib import Path

from nb4_source_digest import source_digest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "radio/src/targets/pl18/nb4_rf_qualified_generated.h"
CMAKE = ROOT / "radio/src/targets/pl18/nb4_rf_qualified.cmake"
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
BOARD_RE = re.compile(r"^[A-Za-z0-9_.-]{1,32}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
RELEASE_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")
ACCEPTANCE_TESTS = (
    "flash_readback",
    "boot_hold_2s",
    "shutdown_release_required",
    "steering_live",
    "throttle_live",
    "curve_live",
    "bind",
    "reconnect_tx_cycle",
    "reconnect_rx_cycle",
    "failsafe",
    "rx_voltage",
    "link_quality",
    "telemetry_expires",
    "rx_power_alarm",
    "usb_serial_disconnect",
    "usb_storage_disconnect",
    "storage_persistence",
    "audio",
    "portrait_layout",
    "landscape_layout",
)

LINE = {"PD11": "Pd11", "PI8": "Pi8"}
MODE = {
    "input": "Input",
    "input_pull_up": "InputPullUp",
    "input_pull_down": "InputPullDown",
    "output_push_pull": "OutputPushPull",
    "output_open_drain": "OutputOpenDrain",
}
LEVEL = {"keep": "Keep", "low": "Low", "high": "High"}
FRAMING = {"addressed_slip": "AddressedSlip", "addressless_slip": "AddresslessSlip"}


def fail(message: str) -> None:
    raise SystemExit(f"qualification rejected: {message}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sequence(data: object, phase: str) -> list[dict[str, object]]:
    if not isinstance(data, list) or not data:
        fail(f"electrical.{phase} must contain at least one recovered step")
    if len(data) > 8:
        fail(f"electrical.{phase} exceeds eight steps")
    result: list[dict[str, object]] = []
    for index, item in enumerate(data):
        if not isinstance(item, dict):
            fail(f"electrical.{phase}[{index}] is not an object")
        line, mode, level = item.get("line"), item.get("mode"), item.get("level")
        delay = item.get("delay_after_us")
        if line not in LINE or mode not in MODE or level not in LEVEL:
            fail(f"electrical.{phase}[{index}] has an unknown line, mode or level")
        if not isinstance(delay, int) or not 0 <= delay <= 10_000_000:
            fail(f"electrical.{phase}[{index}].delay_after_us is invalid")
        if mode.startswith("input") and level != "keep":
            fail(f"electrical.{phase}[{index}] cannot drive an input")
        result.append({"line": line, "mode": mode, "level": level, "delay": delay})
    if {item["line"] for item in result} != set(LINE):
        fail(f"electrical.{phase} must explicitly record both PD11 and PI8")
    return result


def cpp_sequence(items: list[dict[str, object]]) -> str:
    rows = [
        "      {Nb4RfSharedLine::%s, Nb4RfPinMode::%s, Nb4RfLevel::%s, %d},"
        % (LINE[item["line"]], MODE[item["mode"]], LEVEL[item["level"]], item["delay"])
        for item in items
    ]
    rows.extend("      {}," for _ in range(8 - len(rows)))
    return (
        "  {\n    {\n" + "\n".join(rows) +
        f"\n    }},\n    {len(items)},\n  }}"
    )


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            output.write(text)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--check", action="store_true", help="validate without writing")
    parser.add_argument("--header-output", type=Path, default=HEADER,
                        help=argparse.SUPPRESS)
    parser.add_argument("--cmake-output", type=Path, default=CMAKE,
                        help=argparse.SUPPRESS)
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    try:
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(str(error))

    status = data.get("status")
    if status not in ("development", "release"):
        fail("status must be development or release")
    board = data.get("board_revision")
    candidate = data.get("candidate")
    framing = data.get("framing")
    if not isinstance(board, str) or not BOARD_RE.fullmatch(board):
        fail("board_revision is missing or unsafe")
    if candidate != "USART6":
        fail("the recovered AFHDS3 route must be USART6")
    if framing != "addressless_slip":
        fail("the recovered AFHDS3 framing must be addressless_slip")

    address = data.get("frame_address")
    inverted = data.get("uart_inverted")
    cadence = data.get("cadence_us")
    timeout = data.get("response_timeout_us")
    if not isinstance(address, int) or not 0 <= address <= 255:
        fail("frame_address must be 0..255")
    if not isinstance(inverted, bool):
        fail("uart_inverted must be boolean")
    if not isinstance(cadence, int) or not 100 <= cadence <= 100_000:
        fail("cadence_us is outside the reviewable range")
    if not isinstance(timeout, int) or not cadence <= timeout <= 1_000_000:
        fail("response_timeout_us must be >= cadence_us and <= 1 s")
    if timeout % cadence:
        fail("response_timeout_us must be an exact multiple of cadence_us")

    electrical = data.get("electrical")
    if not isinstance(electrical, dict):
        fail("electrical phases are missing")
    phases = {name: sequence(electrical.get(name), name)
              for name in ("boot", "enable", "disable", "shutdown")}
    for phase, steps in phases.items():
        expected_selector = [
            {"line": "PD11", "mode": "output_push_pull",
             "level": "high" if phase == "enable" else "low", "delay": 0},
            {"line": "PI8", "mode": "output_push_pull", "level": "high", "delay": 0},
        ]
        if steps != expected_selector:
            fail(
                f"electrical.{phase} must use the validated AFHDS3 power state "
                "(PD11 high only when enabled, PI8 high throughout)")

    transport = "kNb4RfCandidateUsart3" if candidate == "USART3" else "kNb4RfCandidateUsart6"
    header = f'''/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
/* Generated by tools/nb4-generate-qualified-profile.py. Do not hand edit. */
#pragma once

namespace nb4 {{

inline constexpr Nb4QualifiedHardwareProfile kNb4QualifiedHardwareProfile = {{
  true,
  {json.dumps(board)},
  {transport},
{cpp_sequence(phases["boot"])},
{cpp_sequence(phases["enable"])},
{cpp_sequence(phases["disable"])},
{cpp_sequence(phases["shutdown"])},
  {{Nb4RfFraming::{FRAMING[framing]}, {address}, {str(inverted).lower()}, {cadence}, {timeout}}},
}};

}}  // namespace nb4
'''
    cmake = f'''# SPDX-License-Identifier: GPL-2.0-only
# Generated by tools/nb4-generate-qualified-profile.py. Do not hand edit.
set(NB4_RF_QUALIFIED TRUE)
set(NB4_RF_QUALIFIED_CANDIDATE "{candidate}")
set(NB4_RF_QUALIFIED_FRAMING "{framing.upper()}")
'''

    if not args.check:
        atomic_write(args.header_output.resolve(), header)
        atomic_write(args.cmake_output.resolve(), cmake)
    print(
        f"RF profile valid: status={status} board={board} "
        f"candidate={candidate} framing={framing}"
    )


if __name__ == "__main__":
    main()
