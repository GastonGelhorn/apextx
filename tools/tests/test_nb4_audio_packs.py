# SPDX-License-Identifier: GPL-2.0-only
"""Tests for the size-bounded NB4 audio package and installer tools."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
import wave
from pathlib import Path
from types import SimpleNamespace
from unittest import mock
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[2]


def load(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    if spec is None or spec.loader is None:
        raise RuntimeError(relative)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


audio = load("nb4_audio_packs", "tools/nb4-audio-packs.py")
installer = load("nb4_install_audio", "tools/nb4-install-audio.py")


class Nb4AudioPacksTest(unittest.TestCase):
    def make_pack(self, root: Path, language: str = "es") -> Path:
        staging = root / "pack"
        sound = staging / "SOUNDS" / language / "SYSTEM" / "hello.wav"
        sound.parent.mkdir(parents=True)
        with wave.open(str(sound), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(audio.SAMPLE_RATE)
            output.writeframes(b"\0\0" * 80)
        size = sound.stat().st_size
        entry = {
            "path": f"SOUNDS/{language}/SYSTEM/hello.wav",
            "bytes": size,
            "allocatedBytes": audio.allocated_size(size),
            "sha256": installer.digest(sound),
            "required": True,
        }
        manifest = {
            "formatVersion": 1,
            "target": "NB4",
            "language": language,
            "deviceAudioBudgetBytes": audio.DEVICE_AUDIO_BUDGET,
            "installedBytes": entry["bytes"],
            "installedAllocatedBytes": entry["allocatedBytes"],
            "installedFiles": 1,
            "files": [entry],
        }
        (staging / "APEXTX-AUDIO.json").write_text(
            json.dumps(manifest), encoding="utf-8")
        archive = root / "pack.zip"
        with ZipFile(archive, "w") as package:
            for path in staging.rglob("*"):
                if path.is_file():
                    package.write(path, path.relative_to(staging))
        return archive

    def test_budget_leaves_space_on_the_real_volume(self) -> None:
        self.assertLess(audio.DEVICE_AUDIO_BUDGET, audio.NB4_VOLUME_BYTES)
        self.assertGreaterEqual(
            audio.NB4_VOLUME_BYTES - audio.DEVICE_AUDIO_BUDGET,
            800 * 1024,
        )
        self.assertEqual(audio.UPSTREAM_REF, "v2.12.3")
        self.assertEqual(len(audio.UPSTREAM_COMMIT), 40)

    def test_selection_keeps_every_system_sound_then_maximizes_optional(self) -> None:
        root = Path("/")
        required = audio.ConvertedSound(
            root, Path("SYSTEM/hello.wav"), root, 600, 1024, True, 0)
        preferred = audio.ConvertedSound(
            root, Path("armed.wav"), root, 900, 1024, False, 0)
        small = audio.ConvertedSound(
            root, Path("small.wav"), root, 400, 512, False, 10_000)
        large = audio.ConvertedSound(
            root, Path("large.wav"), root, 1500, 1536, False, 10_000)
        selected, skipped = audio.select_sounds(
            [large, small, preferred, required], budget=2560)
        self.assertEqual(
            [item.relative.as_posix() for item in selected],
            ["SYSTEM/hello.wav", "armed.wav", "small.wav"],
        )
        self.assertEqual([item.relative.as_posix() for item in skipped],
                         ["large.wav"])

    def test_nb4_wav_validation_requires_pcm16_mono_8khz(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sound.wav"
            with wave.open(str(path), "wb") as sound:
                sound.setnchannels(1)
                sound.setsampwidth(2)
                sound.setframerate(audio.SAMPLE_RATE)
                sound.writeframes(b"\0\0" * 80)
            self.assertTrue(audio.is_nb4_wav(path))

    def test_installer_rejects_archive_path_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "bad.zip"
            output = Path(temporary) / "out"
            output.mkdir()
            with ZipFile(archive, "w") as package:
                package.writestr("../outside", "bad")
            with self.assertRaisesRegex(RuntimeError, "Unsafe archive path"):
                installer.safe_extract(archive, output)

    def test_installer_dry_run_and_verified_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            volume = root / "volume"
            current = volume / "SOUNDS" / "en"
            current.mkdir(parents=True)
            (current / "old.wav").write_bytes(b"old audio")
            backups = root / "backups"
            fake_usage = SimpleNamespace(total=audio.NB4_VOLUME_BYTES)

            with mock.patch.object(installer.shutil, "disk_usage",
                                   return_value=fake_usage):
                verified = installer.install(archive, volume, backups, False)
                self.assertFalse(verified.installed)
                self.assertTrue((current / "old.wav").is_file())

                result = installer.install(archive, volume, backups, True)

            self.assertTrue(result.installed)
            self.assertEqual(result.language, "es")
            self.assertEqual(result.sounds, 1)
            self.assertIsNotNone(result.backup)
            assert result.backup is not None
            self.assertEqual((result.backup / "en" / "old.wav").read_bytes(),
                             b"old audio")
            self.assertFalse((volume / "SOUNDS" / "en").exists())
            self.assertTrue(
                (volume / "SOUNDS" / "es" / "SYSTEM" / "hello.wav").is_file()
            )

    def test_installer_refuses_backup_inside_radio_volume(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            volume = root / "volume"
            volume.mkdir()
            fake_usage = SimpleNamespace(total=audio.NB4_VOLUME_BYTES)
            with mock.patch.object(installer.shutil, "disk_usage",
                                   return_value=fake_usage):
                with self.assertRaisesRegex(RuntimeError, "on the computer"):
                    installer.install(archive, volume, volume / "backup", True)

    def test_installer_restores_backup_if_copy_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            volume = root / "volume"
            old = volume / "SOUNDS" / "en" / "old.wav"
            old.parent.mkdir(parents=True)
            old.write_bytes(b"old audio")
            fake_usage = SimpleNamespace(total=audio.NB4_VOLUME_BYTES)
            real_copytree = installer.shutil.copytree

            def failing_copytree(source, destination, *args, **kwargs):
                if Path(destination).name == "SOUNDS.NEW":
                    raise OSError("simulated copy failure")
                return real_copytree(source, destination, *args, **kwargs)

            with mock.patch.object(installer.shutil, "disk_usage",
                                   return_value=fake_usage), \
                 mock.patch.object(installer.shutil, "copytree",
                                   side_effect=failing_copytree):
                with self.assertRaisesRegex(OSError, "simulated"):
                    installer.install(archive, volume, root / "backups", True)

            self.assertEqual(old.read_bytes(), b"old audio")
            self.assertFalse((volume / "SOUNDS.NEW").exists())


if __name__ == "__main__":
    unittest.main()
