#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Safely replace the audio pack on an NB4 USB Storage volume."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from zipfile import ZipFile


MIN_NB4_VOLUME_BYTES = 7_500_000
MAX_NB4_VOLUME_BYTES = 9_000_000
MAX_ARCHIVE_BYTES = 12 * 1024 * 1024
MAX_AUDIO_BUDGET_BYTES = 7 * 1024 * 1024
SUPPORTED_LANGUAGES = ("en", "es")


@dataclass(frozen=True)
class InstallResult:
    language: str
    sounds: int
    installed: bool
    backup: Path | None = None


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def safe_extract(archive: Path, destination: Path) -> None:
    with ZipFile(archive) as source:
        expanded = 0
        for member in source.infolist():
            relative = PurePosixPath(member.filename)
            if (
                relative.is_absolute()
                or ".." in relative.parts
                or "\\" in member.filename
                or (relative.parts and ":" in relative.parts[0])
            ):
                raise RuntimeError(f"Unsafe archive path: {member.filename}")
            expanded += member.file_size
            if expanded > MAX_ARCHIVE_BYTES:
                raise RuntimeError("Audio archive is unexpectedly large")
        source.extractall(destination)


def read_and_verify_pack(root: Path) -> tuple[dict, Path]:
    manifest_path = root / "APEXTX-AUDIO.json"
    if not manifest_path.is_file():
        raise RuntimeError("This is not an ApexTX audio pack")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise RuntimeError("Invalid ApexTX audio manifest") from error
    if manifest.get("target") != "NB4" or manifest.get("formatVersion") != 1:
        raise RuntimeError("Unsupported audio pack target or format")
    language = manifest.get("language")
    sounds = root / "SOUNDS" / str(language)
    if language not in SUPPORTED_LANGUAGES or not sounds.is_dir():
        raise RuntimeError("The pack does not contain one supported language")
    try:
        entries = manifest["files"]
        if not isinstance(entries, list):
            raise TypeError
        for entry in entries:
            if (
                not isinstance(entry["path"], str)
                or not isinstance(entry["sha256"], str)
                or len(entry["sha256"]) != 64
                or not isinstance(entry["bytes"], int)
                or entry["bytes"] <= 0
                or not isinstance(entry["allocatedBytes"], int)
                or entry["allocatedBytes"] < entry["bytes"]
            ):
                raise TypeError
            int(entry["sha256"], 16)
        expected = {entry["path"]: entry["sha256"] for entry in entries}
        allocated = sum(entry["allocatedBytes"] for entry in entries)
        installed_bytes = sum(entry["bytes"] for entry in entries)
    except (KeyError, TypeError, ValueError) as error:
        raise RuntimeError("Incomplete ApexTX audio manifest") from error
    if len(expected) != len(entries):
        raise RuntimeError("Duplicate paths in ApexTX audio manifest")
    if (
        manifest.get("installedFiles") != len(entries)
        or manifest.get("installedBytes") != installed_bytes
        or manifest.get("installedAllocatedBytes") != allocated
        or manifest.get("deviceAudioBudgetBytes") != MAX_AUDIO_BUDGET_BYTES
        or allocated > MAX_AUDIO_BUDGET_BYTES
    ):
        raise RuntimeError("Invalid ApexTX audio size metadata")
    expected_prefix = f"SOUNDS/{language}/"
    if any(not path.startswith(expected_prefix) for path in expected):
        raise RuntimeError("Audio manifest contains a path for another language")
    payload_files = [
        path for path in sorted((root / "SOUNDS").rglob("*")) if path.is_file()
    ]
    if any(path.suffix.lower() != ".wav" for path in payload_files):
        raise RuntimeError("Unexpected non-audio file in the SOUNDS payload")
    actual = {
        path.relative_to(root).as_posix(): digest(path)
        for path in payload_files
    }
    if actual != expected:
        raise RuntimeError("Audio pack file list or checksum verification failed")
    return manifest, root / "SOUNDS"


def directory_hashes(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): digest(path)
        for path in sorted(root.rglob("*")) if path.is_file()
    }


def install(
    archive: Path, volume: Path, backup_parent: Path, confirm: bool
) -> InstallResult:
    if not volume.is_dir():
        raise RuntimeError(f"USB Storage volume not found: {volume}")
    usage = shutil.disk_usage(volume)
    if usage.total < MIN_NB4_VOLUME_BYTES or usage.total > MAX_NB4_VOLUME_BYTES:
        raise RuntimeError(
            f"Refusing unexpected {usage.total}-byte volume; select the NB4 storage root"
        )
    with tempfile.TemporaryDirectory(prefix="apextx-audio-install-") as temporary:
        extracted = Path(temporary)
        safe_extract(archive, extracted)
        manifest, sounds = read_and_verify_pack(extracted)
        language = manifest["language"]
        if not confirm:
            return InstallResult(
                language=language,
                sounds=manifest["installedFiles"],
                installed=False,
            )

        volume_root = volume.resolve()
        backup_root = backup_parent.resolve()
        if backup_root == volume_root or volume_root in backup_root.parents:
            raise RuntimeError("The audio backup directory must be on the computer")
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        backup = backup_parent / f"nb4-sounds-{timestamp}"
        current = volume / "SOUNDS"
        if current.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(current, backup)
            if directory_hashes(current) != directory_hashes(backup):
                raise RuntimeError("Audio backup verification failed; radio was not changed")

        incoming = volume / "SOUNDS.NEW"
        if incoming.exists():
            shutil.rmtree(incoming)
        try:
            if current.exists():
                shutil.rmtree(current)
            shutil.copytree(sounds, incoming)
            incoming.rename(current)
            installed = {
                path.relative_to(volume).as_posix(): digest(path)
                for path in sorted(current.rglob("*.wav"))
            }
            expected = {
                entry["path"]: entry["sha256"] for entry in manifest["files"]
            }
            if installed != expected:
                raise RuntimeError("Installed audio checksum verification failed")
            shutil.copy2(extracted / "APEXTX-AUDIO.json",
                         volume / "APEXTX-AUDIO.json")
            if hasattr(os, "sync"):
                os.sync()
        except Exception:
            if current.exists():
                shutil.rmtree(current)
            if incoming.exists():
                shutil.rmtree(incoming)
            if backup.exists():
                shutil.copytree(backup, current)
            if hasattr(os, "sync"):
                os.sync()
            raise
        return InstallResult(
            language=language,
            sounds=manifest["installedFiles"],
            installed=True,
            backup=backup if backup.exists() else None,
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("volume", type=Path,
                        help="Mounted NB4 USB Storage root")
    parser.add_argument("--backup-dir", type=Path, default=Path.cwd())
    parser.add_argument("--yes", action="store_true",
                        help="Back up and replace the existing SOUNDS folder")
    args = parser.parse_args()
    try:
        result = install(args.archive.resolve(), args.volume.resolve(),
                         args.backup_dir.resolve(), args.yes)
        if result.installed:
            print(
                f"Installed and verified {result.language} audio pack with "
                f"{result.sounds} sounds."
            )
            if result.backup is not None:
                print(f"Previous sounds: {result.backup}")
        else:
            print(
                f"Verified {result.language} audio pack with {result.sounds} sounds. "
                "Run again with --yes to back up and replace the radio's SOUNDS folder."
            )
    except Exception as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
