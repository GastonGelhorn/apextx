#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Contract tests for the NB4 RF evidence and release gates."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nb4_source_digest import source_digest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools/nb4-generate-qualified-profile.py"
VERIFIER = ROOT / "tools/nb4-verify-firmware.py"


class Nb4QualificationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _evidence(self, name: str, contents: bytes) -> dict[str, str]:
        path = self.directory / name
        path.write_bytes(contents)
        return {"path": name, "sha256": hashlib.sha256(contents).hexdigest()}

    def _base(self) -> dict[str, object]:
        step = [
            {
                "line": "PD11", "mode": "output_push_pull", "level": "low",
                "delay_after_us": 0,
            },
            {
                "line": "PI8", "mode": "output_push_pull", "level": "high",
                "delay_after_us": 0,
            },
        ]
        return {
            "status": "development",
            "ambiguous": False,
            "board_revision": "NB4-test",
            "candidate": "USART6",
            "framing": "addressless_slip",
            "frame_address": 0,
            "uart_inverted": False,
            "cadence_us": 5000,
            "response_timeout_us": 30000,
            "electrical": {
                "boot": step,
                "enable": [{**item, "level": "high"} for item in step],
                "disable": step,
                "shutdown": step,
            },
            "bench_acceptance": None,
        }

    def _acceptance(self, *, passed: bool = True) -> dict[str, str]:
        test_names = (
            "flash_readback", "boot_hold_2s", "shutdown_release_required",
            "steering_live", "throttle_live", "curve_live", "bind",
            "reconnect_tx_cycle", "reconnect_rx_cycle", "failsafe",
            "rx_voltage", "link_quality", "telemetry_expires",
            "rx_power_alarm", "usb_serial_disconnect",
            "usb_storage_disconnect", "storage_persistence", "audio",
            "portrait_layout", "landscape_layout",
        )
        report = {
            "schema_version": 1,
            "project_version": "0.1.0-alpha.1",
            "target": "FlySky Noble NB4 (original)",
            "receiver": {"protocol": "AFHDS3", "model": "test receiver"},
            "tested_firmware": {
                "project_version": "0.1.0-alpha.1",
                "source_commit": "a" * 40,
                "source_tree_sha256": source_digest(ROOT),
                "sha256": hashlib.sha256(b"tested firmware").hexdigest(),
            },
            "test_date": "2026-09-14",
            "tester": "test maintainer",
            "tests": {name: {"passed": passed, "notes": "test fixture"}
                      for name in test_names},
        }
        return self._evidence(
            "bench-acceptance.json",
            (json.dumps(report, sort_keys=True) + "\n").encode(),
        )

    def _run(self, data: dict[str, object]) -> subprocess.CompletedProcess[str]:
        manifest = self.directory / "qualification.json"
        manifest.write_text(json.dumps(data), encoding="utf-8")
        return subprocess.run(
            [str(GENERATOR), "--check", str(manifest)],
            text=True, capture_output=True, check=False,
        )

    def _generate(self, data: dict[str, object]) -> tuple[str, str]:
        manifest = self.directory / "qualification.json"
        manifest.write_text(json.dumps(data), encoding="utf-8")
        header = self.directory / "generated.h"
        cmake = self.directory / "generated.cmake"
        result = subprocess.run(
            [str(GENERATOR), str(manifest), "--header-output", str(header),
             "--cmake-output", str(cmake)],
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        probe = self.directory / "generated_probe.cpp"
        profile = ROOT / "radio/src/targets/pl18/nb4_rf_profile.h"
        probe.write_text(
            '#define kNb4QualifiedHardwareProfile kPlaceholderProfile\n'
            f'#include "{profile}"\n'
            '#undef kNb4QualifiedHardwareProfile\n'
            f'#include "{header}"\n'
            'static_assert(nb4::kNb4QualifiedHardwareProfile.qualified);\n',
            encoding="utf-8",
        )
        compiled = subprocess.run(
            ["c++", "-std=c++17", "-fsyntax-only", str(probe)],
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        return header.read_text(encoding="utf-8"), cmake.read_text(encoding="utf-8")

    def test_lab_profile_is_accepted(self) -> None:
        result = self._run(self._base())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("status=development", result.stdout)
        header, cmake = self._generate(self._base())
        self.assertIn("kNb4RfCandidateUsart6", header)
        self.assertIn("Nb4RfFraming::AddresslessSlip", header)
        self.assertIn("\n  false,\n  \"NB4-test\"", header)
        self.assertIn('set(NB4_RF_RELEASE_QUALIFIED FALSE)', cmake)
        self.assertIn('set(NB4_RF_QUALIFIED_FRAMING "ADDRESSLESS_SLIP")', cmake)

    def test_checked_in_recovered_profile_is_valid(self) -> None:
        manifest = ROOT / "docs/nb4/rf/qualification.json"
        result = subprocess.run(
            [str(GENERATOR), "--check", str(manifest)],
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("status=development", result.stdout)

    def test_invalid_status_and_hash_mismatch_are_rejected(self) -> None:
        invalid_status = self._base()
        invalid_status["status"] = "unknown"
        self.assertNotEqual(self._run(invalid_status).returncode, 0)

        mismatch = self._base()
        mismatch["status"] = "release"
        mismatch["bench_acceptance"] = dict(self._acceptance(), sha256="0" * 64)
        self.assertNotEqual(self._run(mismatch).returncode, 0)

        wrong_route = self._base()
        wrong_route["candidate"] = "USART3"
        self.assertNotEqual(self._run(wrong_route).returncode, 0)

        undefined_selector = self._base()
        undefined_selector["electrical"]["boot"][1]["level"] = "low"
        self.assertNotEqual(self._run(undefined_selector).returncode, 0)

    def test_release_requires_passing_physical_acceptance(self) -> None:
        release = self._base()
        release["status"] = "release"
        self.assertNotEqual(self._run(release).returncode, 0)
        release["bench_acceptance"] = self._acceptance()
        accepted = self._run(release)
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        self.assertIn("status=release", accepted.stdout)

    def test_release_rejects_a_failed_physical_test(self) -> None:
        release = self._base()
        release["status"] = "release"
        release["bench_acceptance"] = self._acceptance(passed=False)
        self.assertNotEqual(self._run(release).returncode, 0)

    def test_flash_script_leaves_dfu_without_rewriting_after_readback(self) -> None:
        script = (ROOT / "tools/nb4-flash.sh").read_text(encoding="utf-8")
        self.assertEqual(script.count('-D "$IMAGE"'), 1)
        self.assertIn('readonly BOOT_ADDRESS="0x08000000"', script)
        self.assertIn('-s "$BOOT_ADDRESS:leave"', script)
        self.assertNotIn("LEAVE_IMAGE", script)
        self.assertNotIn("APP_ADDRESS", script)
        self.assertNotIn('-a 0 -e', script)
        self.assertLess(script.index('-U "$READBACK"'),
                        script.index('-s "$BOOT_ADDRESS:leave"'))

    def test_off_state_cannot_be_used_for_enabled_module(self) -> None:
        data = self._base()
        data["electrical"]["enable"][0]["level"] = "low"
        self.assertNotEqual(self._run(data).returncode, 0)


class Nb4FirmwareVerifierTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.elf = self.directory / "firmware.elf"
        self.binary = self.directory / "firmware.bin"
        self.report = self.directory / "nb4-size-report.json"
        self.elf.write_bytes(b"ELF fixture")
        self.binary.write_bytes(b"firmware fixture")
        self.size_tool = self._tool(
            "size-tool", "#!/bin/sh\nprintf '.text 1262680 0\\n.data 3608 0\\n'\n"
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _tool(self, name: str, body: str) -> Path:
        path = self.directory / name
        path.write_text(body, encoding="utf-8")
        path.chmod(0o755)
        return path

    def _run(self, symbols: str, *extra: str) -> subprocess.CompletedProcess[str]:
        nm = self._tool("nm-tool", f"#!/bin/sh\nprintf '%s' '{symbols}'\n")
        return subprocess.run(
            [sys.executable, str(VERIFIER), "--elf", str(self.elf),
             "--bin", str(self.binary), "--nm", str(nm),
             "--size", str(self.size_tool), "--report", str(self.report),
             "--profile", "UNQUALIFIED", "--framing", "UNKNOWN", *extra],
            text=True, capture_output=True, check=False,
        )

    def test_single_route_framing_and_image_emit_machine_report(self) -> None:
        symbols = (
            "nb4_rf_linked_unqualified\\n"
            "nb4_rf_framing_unqualified\\n"
            "nb4_rf_image_laboratory\\n"
        )
        result = self._run(symbols)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(self.report.read_text(encoding="utf-8"))
        self.assertEqual(report["sections"]["text_and_rodata_delta"], 0)
        self.assertEqual(report["sections"]["data_delta"], 0)

    def test_duplicate_route_is_rejected_but_public_is_only_metadata(self) -> None:
        duplicate = (
            "nb4_rf_linked_unqualified\\nnb4_rf_linked_usart3\\n"
            "nb4_rf_framing_unqualified\\nnb4_rf_image_laboratory\\n"
        )
        self.assertNotEqual(self._run(duplicate).returncode, 0)

        public = (
            "nb4_rf_linked_unqualified\\n"
            "nb4_rf_framing_unqualified\\nnb4_rf_image_public\\n"
        )
        accepted = self._run(public, "--public")
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        report = json.loads(self.report.read_text(encoding="utf-8"))
        self.assertEqual(report["image"], "public")


if __name__ == "__main__":
    unittest.main()
