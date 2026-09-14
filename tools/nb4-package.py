#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Create or verify an ApexTX firmware package. Never flash."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET

from nb4_source_digest import source_digest

ROOT = Path(__file__).resolve().parents[1]
PROJECT_VERSION = (ROOT / "APEXTX_VERSION").read_text(encoding="utf-8").strip()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()

def size(path):
    row = command("arm-none-eabi-size", "-B", str(path)).splitlines()[-1].split()
    return dict(zip(("text", "data", "bss"), map(int, row[:3])))

def verify(package):
    manifest = json.loads((package / "manifest.json").read_text())
    if manifest["formatVersion"] not in (1, 2, 3):
        raise ValueError("Unknown package format")
    for relative, expected in manifest["files"].items():
        path = (package / relative).resolve()
        if not path.is_relative_to(package.resolve()):
            raise ValueError("Invalid manifest path")
        if path.stat().st_size != expected["bytes"] or digest(path) != expected["sha256"]:
            raise ValueError(f"Package mismatch: {relative}")
    print(f"Verified {len(manifest['files'])} files: {manifest['release']}")
    return manifest

def create(args):
    build, out = args.build.resolve(), args.output.resolve()
    baseline = args.baseline.resolve() if args.baseline else None
    head = command("git", "rev-parse", "HEAD")
    source_dirty = bool(command("git", "status", "--porcelain"))
    if out.exists():
        raise ValueError("Choose a new output directory; existing packages are preserved")
    tests = ET.parse(args.tests).getroot() if args.tests else None
    cache = (build / "CMakeCache.txt").read_text()
    if not re.search(r"^PCBREV:[^=]+=NB4$", cache, re.M):
        raise ValueError("This package must be built for NB4")
    profile_match = re.search(
        r"^NB4_RF_PROFILE:STRING=(UNQUALIFIED|CANDIDATE_USART3|"
        r"CANDIDATE_USART6|RECOVERED_USART6|QUALIFIED)$", cache, re.M)
    if not profile_match:
        raise ValueError("Unknown NB4 RF build profile")
    rf_profile = profile_match[1]
    public_image = bool(re.search(r"^APEXTX_PUBLIC_RELEASE:BOOL=ON$", cache, re.M))
    generated = (ROOT / "radio/src/targets/pl18/nb4_rf_qualified.cmake").read_text()
    candidate = re.search(r'^set\(NB4_RF_QUALIFIED_CANDIDATE "(USART3|USART6)"\)$', generated, re.M)
    framing = re.search(r'^set\(NB4_RF_QUALIFIED_FRAMING "(ADDRESSED_SLIP|ADDRESSLESS_SLIP)"\)$', generated, re.M)
    acceptance_hash = re.search(r'^set\(NB4_RF_BENCH_ACCEPTANCE_SHA256 "([0-9a-f]*)"\)$', generated, re.M)
    release_qualified = bool(re.search(
        r'^set\(NB4_RF_RELEASE_QUALIFIED TRUE\)$', generated, re.M))
    nm = re.search(r"^CMAKE_NM:FILEPATH=(.+)$", cache, re.M)
    size_tool = re.search(r"^CMAKE_SIZE_UTIL:INTERNAL=(.+)$", cache, re.M)
    if not all((candidate, framing, acceptance_hash, nm, size_tool)):
        raise ValueError("Generated RF descriptor is incomplete")
    if args.release != PROJECT_VERSION:
        raise ValueError(
            f"Package version {args.release!r} does not match APEXTX_VERSION "
            f"{PROJECT_VERSION!r}")
    if public_image and (
            rf_profile != "QUALIFIED" or not release_qualified or
            not acceptance_hash[1]):
        raise ValueError(
            "Public packages require a release-qualified RF profile and "
            "physical acceptance evidence")
    if public_image and source_dirty:
        raise ValueError("Public packages require a clean source tree")
    if public_image and (tests is None or int(tests.get("tests", 0)) <= 0 or
                         int(tests.get("failures", 0)) +
                         int(tests.get("errors", 0)) != 0):
        raise ValueError("Public packages require a passing native test report")
    verify_profile = {
        "UNQUALIFIED": "UNQUALIFIED",
        "CANDIDATE_USART3": "CANDIDATE_USART3",
        "CANDIDATE_USART6": "CANDIDATE_USART6",
        "RECOVERED_USART6": "RECOVERED_USART6",
        "QUALIFIED": f"QUALIFIED_{candidate[1]}",
    }[rf_profile]
    verify_framing = framing[1] if rf_profile in (
        "RECOVERED_USART6", "QUALIFIED") else "UNKNOWN"
    verify_command = [
        str(ROOT / "tools/nb4-verify-firmware.py"),
        "--elf", str(build / "firmware.elf"),
        "--bin", str(build / "firmware.bin"),
        "--nm", nm[1],
        "--size", size_tool[1],
        "--report", str(build / "nb4-size-report.json"),
        "--profile", verify_profile,
        "--framing", verify_framing,
    ]
    if public_image:
        verify_command.append("--public")
    subprocess.run(verify_command, check=True, cwd=ROOT)
    installation = None
    rollback = None
    if baseline:
        installation = json.loads((baseline / "install.json").read_text())
        previous_hash = digest(baseline / "firmware.bin")
        if installation["sha256"] != previous_hash:
            raise ValueError("Provided rollback firmware does not match install.json")
        rollback = {
            key: installation.get(key)
            for key in (
                "sourceCommit", "workingTreeDirty", "sha256", "status",
                "readbackVerified", "physicalQualification", "runtimeHomeObserved",
            )
        }
    evidence = args.tests.resolve().parent if args.tests else None
    out.mkdir(parents=True)
    copies = {
        "firmware.bin": build / "firmware.bin",
        "validation/size-report.json": build / "nb4-size-report.json",
        "README.md": ROOT / "docs/nb4/README.md",
        "FLASH.md": ROOT / "docs/nb4/FLASH.md",
        "VALIDATION.md": ROOT / "docs/nb4/VALIDATION.md",
        "RELEASE.md": ROOT / "docs/nb4/RELEASE.md",
        "CHANGELOG.md": ROOT / "CHANGELOG.md",
        "SECURITY.md": ROOT / "SECURITY.md",
        "THIRD_PARTY_NOTICES.md": ROOT / "THIRD_PARTY_NOTICES.md",
        "validation/rf/qualification.json": ROOT / "docs/nb4/rf/qualification.json",
        "licenses/GPL-2.txt": ROOT / "LICENSE",
        "licenses/Barlow-OFL.txt": ROOT / "radio/src/fonts/Barlow/OFL.txt",
        "licenses/BarlowCondensed-OFL.txt": ROOT / "radio/src/fonts/Barlow/OFL-Condensed.txt",
        "licenses/Roboto-Apache-2.txt": ROOT / "radio/src/fonts/Roboto/LICENSE.txt",
        "licenses/font-sources.json": ROOT / "radio/src/fonts/Barlow/sources.json",
    }
    if args.tests:
        copies["validation/tests.xml"] = args.tests
        for name in ("final-tests.log", "clean-arm-build.log"):
            log = evidence / name
            if log.is_file():
                copies[f"validation/{name}"] = log
    qualification = json.loads(
        (ROOT / "docs/nb4/rf/qualification.json").read_text(encoding="utf-8"))
    acceptance = qualification.get("bench_acceptance")
    if acceptance:
        acceptance_source = ROOT / "docs/nb4/rf" / acceptance["path"]
        copies[f"validation/rf/{acceptance_source.name}"] = acceptance_source
    if baseline:
        copies["rollback/firmware.bin"] = baseline / "firmware.bin"
    for relative, source in copies.items():
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    if rollback:
        (out / "rollback/version.json").write_text(
            json.dumps(rollback, indent=2) + "\n"
        )
    for relative in ("THEMES/ApexTXLight", "THEMES/ApexTXDark", "SCRIPTS/TOOLS"):
        shutil.copytree(ROOT / "sdcard" / relative, out / "resources" / relative)
    shutil.copy2(ROOT / "sdcard/THEMES/README.md", out / "resources/THEMES/README.md")
    if evidence:
        for path in sorted((evidence / "screens").glob("*.png")):
            if path.name.startswith(("home-", "racing-", "review-final")):
                target = out / "validation/screens" / path.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    manifest = {
        "formatVersion": 3, "release": args.release, "target": "NB4",
        "projectVersion": PROJECT_VERSION, "edgeTxBase": "2.12.4",
        "sourceCommit": head, "sourceTreeSha256": source_digest(ROOT),
        "sourceDirty": source_dirty,
        "baseTag": "v2.12.4",
        "carApi": 1, "drivingFormat": 2, "visualPreferences": 2, "historyFormat": 1, "resourceVersion": 3,
        "languages": ["es", "en"], "homes": ["ApexTX"],
        "fonts": ["Roboto", "Barlow Condensed"],
        "orientations": [[320, 480], [480, 272]], "outputChannels": 8,
        "rf": f"AFHDS3; {rf_profile}; {verify_framing}",
        "qualification": {
            "status": "RELEASE" if release_qualified else "DEVELOPMENT",
            "publicImage": public_image,
            "installed": False,
            "benchAcceptanceSha256": acceptance_hash[1] or None,
            "nativeTests": int(tests.get("tests", 0)) if tests is not None else None,
            "nativeFailures": (
                int(tests.get("failures", 0)) + int(tests.get("errors", 0))
                if tests is not None else None
            ),
            "screenshots": "Offscreen native LVGL simulator; synthetic inputs explicitly seeded by tests",
            "physicalResults": (
                "Hash-bound passing acceptance record"
                if acceptance_hash[1] else "Not yet recorded"
            ),
        },
        "memoryBytes": {
            "current": size(build / "firmware.elf"),
            "lvglPool": 2097152, "luaHeapLimit": 1048576,
            "luaBitmapLimit": 524288, "storageStack": 4096,
            "historyReservations": 2,
        },
        "rollback": rollback,
        "files": {},
    }
    resource = {k: manifest[k] for k in ("release", "projectVersion", "sourceCommit", "target", "resourceVersion", "visualPreferences", "historyFormat")}
    (out / "resources/APEXTX-VERSION.json").write_text(json.dumps(resource, indent=2) + "\n")
    for path in sorted(out.rglob("*")):
        if path.is_file():
            manifest["files"][path.relative_to(out).as_posix()] = {"bytes": path.stat().st_size, "sha256": digest(path)}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    verify(out)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", type=Path)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline", type=Path, help="Directory containing the previously installed firmware.bin and verified install.json")
    parser.add_argument("--tests", type=Path)
    parser.add_argument("--release", default=PROJECT_VERSION)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify)
    elif args.build and args.output:
        create(args)
    else:
        parser.error(
            "Use --verify DIRECTORY, or --build/--output "
            "[--baseline DIRECTORY] [--tests FILE]"
        )

if __name__ == "__main__":
    main()
