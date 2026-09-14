#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verify the RF route marker and size policy of one NB4 firmware image."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


BASELINE = 1_603_824
# The NB4 linker script folds read-only data into .text. Keep that explicit in
# the report instead of pretending there is a standalone .rodata section.
TEXT_RODATA_BASELINE = 1_262_680
DATA_BASELINE = 3_608
MARKERS = {
    "UNQUALIFIED": "nb4_rf_linked_unqualified",
    "CANDIDATE_USART3": "nb4_rf_linked_usart3",
    "CANDIDATE_USART6": "nb4_rf_linked_usart6",
    "RECOVERED_USART6": "nb4_rf_linked_usart6",
    "QUALIFIED_USART3": "nb4_rf_linked_usart3",
    "QUALIFIED_USART6": "nb4_rf_linked_usart6",
}
FRAMING_MARKERS = {
    "UNKNOWN": "nb4_rf_framing_unqualified",
    "ADDRESSED_SLIP": "nb4_rf_framing_addressed_slip",
    "ADDRESSLESS_SLIP": "nb4_rf_framing_addressless_slip",
}
IMAGE_MARKERS = {
    False: "nb4_rf_image_laboratory",
    True: "nb4_rf_image_public",
}


def fail(message: str) -> None:
    raise SystemExit(f"NB4 firmware rejected: {message}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--bin", required=True, type=Path)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--size", required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--profile", required=True, choices=MARKERS)
    parser.add_argument("--framing", required=True, choices=FRAMING_MARKERS)
    parser.add_argument("--public", action="store_true")
    args = parser.parse_args()

    if not args.elf.is_file() or not args.bin.is_file():
        fail("firmware.elf or firmware.bin is missing")
    try:
        symbols = subprocess.run(
            [args.nm, "--defined-only", str(args.elf)],
            check=True, capture_output=True, text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot inspect ELF symbols: {error}")

    try:
        size_output = subprocess.run(
            [args.size, "-A", str(args.elf)],
            check=True, capture_output=True, text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot inspect ELF sections: {error}")
    sections: dict[str, int] = {}
    for line in size_output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith("."):
            try:
                sections[fields[0]] = int(fields[1], 0)
            except ValueError:
                fail(f"invalid section size reported for {fields[0]}")
    if ".text" not in sections or ".data" not in sections:
        fail("ELF section report has no .text or .data")

    def require_one(markers: dict[object, str], expected_key: object, label: str) -> None:
        present = sorted(name for name in set(markers.values()) if name in symbols)
        expected = markers[expected_key]
        if present != [expected]:
            fail(f"expected only {expected} {label}, found {present or 'none'}")

    require_one(MARKERS, args.profile, "route marker")
    require_one(FRAMING_MARKERS, args.framing, "framing marker")
    require_one(IMAGE_MARKERS, args.public, "image marker")
    size = args.bin.stat().st_size
    delta = size - BASELINE
    text_rodata = sections[".text"] + sections.get(".rodata", 0)
    text_rodata_delta = text_rodata - TEXT_RODATA_BASELINE
    data = sections[".data"]
    data_delta = data - DATA_BASELINE
    report = {
        "profile": args.profile,
        "framing": args.framing,
        "image": "public" if args.public else "laboratory",
        "sections": {
            "text_and_rodata": text_rodata,
            "text_and_rodata_delta": text_rodata_delta,
            "data": data,
            "data_delta": data_delta,
        },
        "binary": {
            "size": size,
            "delta": delta,
        },
    }
    if args.report:
        args.report.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    print(
        f"NB4 firmware verified: profile={args.profile} framing={args.framing} "
        f"image={'public' if args.public else 'laboratory'} "
        f"text+rodata={text_rodata} ({text_rodata_delta:+d}) "
        f"data={data} ({data_delta:+d}) size={size} delta={delta:+d}"
    )


if __name__ == "__main__":
    main()
