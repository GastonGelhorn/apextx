#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the ApexTX welcome prompts (SOUNDS/<lang>/SYSTEM/hello.wav).

The phrase comes from radio/util/tts_<lang>.py, the same table the rest of a
voice pack is described by, and is spoken by a fixed macOS system voice so
the prompts can be regenerated identically. Requires macOS `say` and ffmpeg.
"""
from __future__ import annotations

import argparse
import array
import importlib.util
import platform
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAMPLE_RATE = 16_000
PEAK = 23_197          # -3 dBFS, so all languages play equally loud.
TAIL_SECONDS = 0.1     # Silence kept after the last word.

# One voice per language, all installed with macOS.
VOICES = {"en": "Samantha", "es": "Mónica", "de": "Anna", "cn": "Tingting"}


def phrase(language: str) -> str:
    util = ROOT / "radio" / "util"
    if str(util) not in sys.path:
        sys.path.insert(0, str(util))
    path = util / f"tts_{language}.py"
    spec = importlib.util.spec_from_file_location(f"apextx_tts_{language}", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for text, name in module.systemSounds + module.sounds:
        if name == "hello.wav":
            return text
    raise RuntimeError(f"{path} has no hello phrase")


def normalize(path: Path) -> None:
    """Scale to a common peak and drop the voice engine's trailing silence."""
    with wave.open(str(path), "rb") as sound:
        samples = array.array("h", sound.readframes(sound.getnframes()))
    if sys.byteorder != "little":
        samples.byteswap()
    peak = max(abs(value) for value in samples)
    if peak == 0:
        raise RuntimeError(f"{path} is silent")
    last = max(i for i, value in enumerate(samples) if abs(value) > peak // 100)
    end = min(len(samples), last + int(TAIL_SECONDS * SAMPLE_RATE))
    scaled = array.array("h", (round(value * PEAK / peak)
                               for value in samples[:end]))
    if sys.byteorder != "little":
        scaled.byteswap()
    with wave.open(str(path), "wb") as sound:
        sound.setnchannels(1)
        sound.setsampwidth(2)
        sound.setframerate(SAMPLE_RATE)
        sound.writeframes(scaled.tobytes())


def generate(language: str, output: Path, ffmpeg: str = "ffmpeg") -> Path:
    text = phrase(language)
    target = output / "SOUNDS" / language / "SYSTEM" / "hello.wav"
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        spoken = Path(work) / "hello.aiff"
        subprocess.run(["say", "-v", VOICES[language], "-o", str(spoken), text],
                       check=True)
        subprocess.run(
            [ffmpeg, "-loglevel", "error", "-y", "-i", str(spoken),
             "-ac", "1", "-ar", str(SAMPLE_RATE), "-c:a", "pcm_s16le",
             "-map_metadata", "-1", "-fflags", "+bitexact",
             "-flags:a", "+bitexact", str(target)],
            check=True)
    normalize(target)
    with wave.open(str(target), "rb") as sound:
        seconds = sound.getnframes() / sound.getframerate()
    print(f"{target.relative_to(output)}: {VOICES[language]}, "
          f"{seconds:.2f} s, \"{text}\"")
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--language", choices=sorted(VOICES),
                        action="append", dest="languages")
    parser.add_argument("--output", type=Path, default=ROOT / "sdcard")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("the welcome prompts are spoken by macOS system voices")
    for language in args.languages or sorted(VOICES):
        generate(language, args.output.resolve(), args.ffmpeg)


if __name__ == "__main__":
    main()
