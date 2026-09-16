#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build size-bounded, NB4-compatible English and Spanish audio packs."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import wave
import zipfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_URL = "https://github.com/EdgeTX/edgetx-sdcard-sounds.git"
UPSTREAM_REF = "v2.12.3"
UPSTREAM_COMMIT = "4e34b6cb40ab53c6dcd293e2b1344b54d912d41c"
PACK_VERSION = 1
SAMPLE_RATE = 8_000
CLUSTER_BYTES = 512
NB4_VOLUME_BYTES = 8_204_800
# Leave at least 844 KiB for the ApexTX themes/scripts, settings, models,
# history, FAT metadata and future migrations. Only one audio pack is installed.
DEVICE_AUDIO_BUDGET = 7 * 1024 * 1024
SUPPORTED_LANGUAGES = ("en", "es")


@dataclass(frozen=True)
class ConvertedSound:
    source: Path
    relative: Path
    converted: Path
    bytes: int
    allocated: int
    required: bool
    priority: int


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def allocated_size(size: int) -> int:
    return ((size + CLUSTER_BYTES - 1) // CLUSTER_BYTES) * CLUSTER_BYTES


def is_nb4_wav(path: Path) -> bool:
    try:
        with wave.open(str(path), "rb") as sound:
            return (
                sound.getnchannels() == 1
                and sound.getsampwidth() == 2
                and sound.getframerate() == SAMPLE_RATE
                and sound.getcomptype() == "NONE"
                and sound.getnframes() > 0
            )
    except (EOFError, wave.Error):
        return False


def convert_sound(source: Path, target: Path, ffmpeg: str) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if is_nb4_wav(source):
        shutil.copy2(source, target)
    else:
        subprocess.run(
            [
                ffmpeg,
                "-nostdin",
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-i",
                str(source),
                "-map_metadata",
                "-1",
                "-af",
                # Trim only the leading and trailing digital silence. Reversing
                # for the second pass avoids treating a natural pause inside a
                # multi-word prompt as the end of that prompt.
                "silenceremove=start_periods=1:start_duration=0.02:"
                "start_threshold=-55dB,areverse,"
                "silenceremove=start_periods=1:start_duration=0.08:"
                "start_threshold=-55dB,areverse",
                "-ac",
                "1",
                "-ar",
                str(SAMPLE_RATE),
                "-c:a",
                "pcm_s16le",
                str(target),
            ],
            check=True,
        )
    if not is_nb4_wav(target):
        raise RuntimeError(f"NB4-incompatible WAV produced from {source}")


def preferred_optional_names(language: str) -> list[str]:
    util = ROOT / "radio" / "util"
    if str(util) not in sys.path:
        sys.path.insert(0, str(util))
    module_path = util / f"tts_{language}.py"
    spec = importlib.util.spec_from_file_location(f"apextx_tts_{language}", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return [filename for _, filename in module.sounds]


def select_sounds(
    sounds: list[ConvertedSound], budget: int = DEVICE_AUDIO_BUDGET
) -> tuple[list[ConvertedSound], list[ConvertedSound]]:
    required = sorted((sound for sound in sounds if sound.required),
                      key=lambda sound: sound.relative.as_posix())
    optional = sorted(
        (sound for sound in sounds if not sound.required),
        key=lambda sound: (sound.priority, sound.allocated,
                           sound.relative.as_posix()),
    )
    used = sum(sound.allocated for sound in required)
    if used > budget:
        raise RuntimeError(
            f"Required system audio needs {used} allocated bytes; budget is {budget}"
        )
    selected = list(required)
    skipped: list[ConvertedSound] = []
    for sound in optional:
        if used + sound.allocated <= budget:
            selected.append(sound)
            used += sound.allocated
        else:
            skipped.append(sound)
    return selected, skipped


def validate_source(source: Path, allow_unverified: bool) -> None:
    for language in SUPPORTED_LANGUAGES:
        required = source / "SOUNDS" / language / "SYSTEM"
        if not required.is_dir():
            raise RuntimeError(f"Missing upstream directory: {required}")
    if allow_unverified:
        return
    try:
        commit = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
        ).strip()
    except subprocess.CalledProcessError as error:
        raise RuntimeError("Audio source must be a pinned Git checkout") from error
    if commit != UPSTREAM_COMMIT:
        raise RuntimeError(
            f"Audio source is {commit}; expected pinned commit {UPSTREAM_COMMIT}"
        )


def deterministic_zip(root: Path, archive: Path) -> None:
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(path.relative_to(root).as_posix())
            info.date_time = (2020, 1, 1, 0, 0, 0)
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, path.read_bytes(), compresslevel=9)


def build_pack(
    source: Path,
    output: Path,
    language: str,
    release: str,
    ffmpeg: str = "ffmpeg",
    budget: int = DEVICE_AUDIO_BUDGET,
    allow_unverified_source: bool = False,
) -> Path:
    if language not in SUPPORTED_LANGUAGES:
        raise ValueError(f"Unsupported language: {language}")
    validate_source(source, allow_unverified_source)
    source_language = source / "SOUNDS" / language
    custom_hello = ROOT / "sdcard" / "SOUNDS" / language / "SYSTEM" / "hello.wav"
    if not custom_hello.is_file():
        raise RuntimeError(f"Missing ApexTX welcome prompt: {custom_hello}")

    preferred = {
        name.lower(): index for index, name in
        enumerate(preferred_optional_names(language))
    }
    with tempfile.TemporaryDirectory(prefix=f"apextx-audio-{language}-") as temporary:
        converted_root = Path(temporary) / "converted"
        candidates: list[ConvertedSound] = []
        sources = sorted((source_language / "SYSTEM").glob("*.wav"))
        sources += sorted(source_language.glob("*.wav"))
        for path in sources:
            required = path.parent.name == "SYSTEM"
            actual_source = custom_hello if required and path.name == "hello.wav" else path
            relative = Path("SYSTEM") / path.name if required else Path(path.name)
            target = converted_root / relative
            convert_sound(actual_source, target, ffmpeg)
            size = target.stat().st_size
            candidates.append(
                ConvertedSound(
                    source=actual_source,
                    relative=relative,
                    converted=target,
                    bytes=size,
                    allocated=allocated_size(size),
                    required=required,
                    priority=preferred.get(path.name.lower(), 10_000),
                )
            )

        selected, skipped = select_sounds(candidates, budget)
        pack_name = f"apextx-audio-{language}-{release}"
        staging = Path(temporary) / pack_name
        payload = staging / "SOUNDS" / language
        for sound in selected:
            destination = payload / sound.relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(sound.converted, destination)

        license_dir = staging / "licenses"
        license_dir.mkdir(parents=True)
        upstream_license = source / "LICENSE"
        if upstream_license.is_file():
            shutil.copy2(upstream_license,
                         license_dir / "EdgeTX-sdcard-sounds-GPL-2.0.txt")
        shutil.copy2(ROOT / "LICENSE", license_dir / "ApexTX-GPL-2.0.txt")

        files = []
        for sound in sorted(selected, key=lambda item: item.relative.as_posix()):
            installed = payload / sound.relative
            files.append(
                {
                    "path": f"SOUNDS/{language}/{sound.relative.as_posix()}",
                    "bytes": sound.bytes,
                    "allocatedBytes": sound.allocated,
                    "sha256": sha256(installed),
                    "required": sound.required,
                    "origin": (
                        "ApexTX"
                        if sound.source == custom_hello
                        else "EdgeTX/edgetx-sdcard-sounds"
                    ),
                }
            )
        manifest = {
            "formatVersion": PACK_VERSION,
            "release": release,
            "target": "NB4",
            "language": language,
            "sampleFormat": "PCM signed 16-bit little-endian",
            "sampleRate": SAMPLE_RATE,
            "channels": 1,
            "deviceVolumeBytes": NB4_VOLUME_BYTES,
            "deviceAudioBudgetBytes": budget,
            "installedBytes": sum(item["bytes"] for item in files),
            "installedAllocatedBytes": sum(item["allocatedBytes"] for item in files),
            "installedFiles": len(files),
            "requiredSystemFiles": sum(sound.required for sound in selected),
            "optionalFiles": sum(not sound.required for sound in selected),
            "skippedOptionalFiles": len(skipped),
            "upstream": {
                "url": UPSTREAM_URL,
                "ref": UPSTREAM_REF,
                "commit": UPSTREAM_COMMIT,
                "license": "GPL-2.0-only",
            },
            "files": files,
        }
        (staging / "APEXTX-AUDIO.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        (staging / "README.txt").write_text(
            "ApexTX NB4 audio pack\n\n"
            "Install only one language pack at a time. Copy the SOUNDS folder "
            "to the root of the radio while it is in USB Storage mode. The "
            "interface language and voice language are independent. Safely "
            "eject the volume before leaving USB Storage mode.\n",
            encoding="utf-8",
        )
        archive = output / f"{pack_name}.zip"
        deterministic_zip(staging, archive)
        return archive


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path,
                        help="Pinned edgetx-sdcard-sounds checkout")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--release", required=True)
    parser.add_argument("--language", choices=SUPPORTED_LANGUAGES,
                        action="append", dest="languages")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--budget", type=int, default=DEVICE_AUDIO_BUDGET)
    parser.add_argument("--allow-unverified-source", action="store_true")
    args = parser.parse_args()
    languages = args.languages or list(SUPPORTED_LANGUAGES)
    for language in languages:
        archive = build_pack(
            args.source.resolve(), args.output.resolve(), language,
            args.release, args.ffmpeg, args.budget,
            args.allow_unverified_source,
        )
        print(archive)


if __name__ == "__main__":
    main()
