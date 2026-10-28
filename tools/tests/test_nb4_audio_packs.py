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
sys.path.insert(0, str(ROOT / "tools"))


def load(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    if spec is None or spec.loader is None:
        raise RuntimeError(relative)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


audio = load("nb4_audio_packs", "tools/nb4-audio-packs.py")
installer = load("nb4_audio_install", "tools/nb4_audio_install.py")


class Nb4AudioPacksTest(unittest.TestCase):
    def test_chinese_numbers_and_welcome_have_matching_recording_names(self) -> None:
        phrases = load('nb4_tts_cn', 'radio/util/tts_cn.py')
        system = dict((name, text) for text, name in phrases.systemSounds)
        # tts_cn.cpp uses these numbered recordings for numbers and decimals;
        # units and warnings use named files from the same upstream table.
        for index in list(range(113)) + list(range(167, 177)):
            self.assertIn(f'{index:04}.wav', system)
        for name in ('volt0.wav', 'volt1.wav', 'second0.wav', 'second1.wav',
                     'lowbatt.wav', 'rssi_org.wav', 'rssi_red.wav', 'telemko.wav'):
            self.assertIn(name, system)
        self.assertEqual(system['hello.wav'], '欢迎使用 Apex T X')
        self.assertEqual(len(system), len(phrases.systemSounds))

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
            "release": "test",
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

    def test_installer_accepts_every_language_the_builder_makes(self) -> None:
        self.assertEqual(installer.SUPPORTED_LANGUAGES,
                         audio.SUPPORTED_LANGUAGES)
        for language in audio.SUPPORTED_LANGUAGES:
            with self.subTest(language=language):
                prompt = audio.ROOT / "sdcard/SOUNDS" / language / "SYSTEM/hello.wav"
                self.assertTrue(prompt.is_file())
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary) / "extracted"
                    archive = self.make_pack(Path(temporary), language)
                    with ZipFile(archive) as pack:
                        pack.extractall(root)
                    manifest, sounds = installer.read_and_verify_pack(root)
                    self.assertEqual(manifest["language"], language)
                    self.assertTrue((sounds / language).is_dir())

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
            with self.assertRaisesRegex(RuntimeError, "Unsafe .*path"):
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
            fake_usage = SimpleNamespace(total=audio.NB4_VOLUME_BYTES, free=1024 * 1024)
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
            self.assertEqual((result.backup / "data/SOUNDS/en/old.wav").read_bytes(),
                             b"old audio")
            self.assertTrue((volume / "SOUNDS/en/old.wav").is_file())
            self.assertTrue(
                (volume / "SOUNDS" / "es" / "SYSTEM" / "hello.wav").is_file()
            )

    def test_installer_refuses_backup_inside_radio_volume(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            volume = root / "volume"
            volume.mkdir()
            fake_usage = SimpleNamespace(total=audio.NB4_VOLUME_BYTES, free=1024 * 1024)
            with mock.patch.object(installer.shutil, "disk_usage",
                                   return_value=fake_usage):
                with self.assertRaisesRegex(RuntimeError, "on the computer"):
                    installer.install(archive, volume, volume / "backup", True)

    def test_installer_keeps_personal_audio_and_resumes_after_copy_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            volume = root / "volume"
            old = volume / "SOUNDS" / "en" / "old.wav"
            old.parent.mkdir(parents=True)
            old.write_bytes(b"old audio")
            fake_usage = SimpleNamespace(total=audio.NB4_VOLUME_BYTES, free=1024 * 1024)
            copy = installer.shutil.copyfileobj

            def fail_device_copy(source, destination, *args, **kwargs):
                if str(getattr(destination, "name", "")).endswith(".partial"):
                    raise OSError("simulated copy failure")
                return copy(source, destination, *args, **kwargs)

            with mock.patch.object(installer.shutil, "disk_usage",
                                   return_value=fake_usage), \
                 mock.patch.object(installer.shutil, "copyfileobj",
                                   side_effect=fail_device_copy):
                with self.assertRaisesRegex(OSError, "simulated"):
                    installer.install(archive, volume, root / "backups", True)

            self.assertEqual(old.read_bytes(), b"old audio")
            self.assertTrue((volume / installer.JOURNAL).is_file())
            self.assertLess((volume / installer.JOURNAL).stat().st_size, 1024)
            self.assertNotIn("plan", json.loads((volume / installer.JOURNAL).read_text()))
            with mock.patch.object(installer.shutil, "disk_usage", return_value=fake_usage):
                with self.assertRaisesRegex(RuntimeError, "--resume"):
                    installer.install(archive, volume, root / "backups", True)
                installed = installer.install(archive, volume, root / "backups", True, resume=True)
            self.assertTrue(installed.installed)
            self.assertEqual(old.read_bytes(), b"old audio")
            self.assertFalse((volume / installer.JOURNAL).exists())

    def test_bound_audio_backup_can_be_restored_without_removing_later_personal_tracks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); archive=self.make_pack(root); volume=root/'volume'; volume.mkdir()
            device=dict(serial='000000000001',target='nb4',nonce='ab'*16)
            (volume/'APEXTX-USB.json').write_text(json.dumps(dict(format='apextx-storage-session-1',**device)))
            old=volume/'SOUNDS/en/personal.wav'; old.parent.mkdir(parents=True); old.write_bytes(b'personal audio')
            with mock.patch.object(installer.shutil,'disk_usage',return_value=SimpleNamespace(total=audio.NB4_VOLUME_BYTES,free=1024*1024)):
                result=installer.install(archive,volume,root/'backups',True,device=device)
                import struct,zlib
                identity=(volume/installer.IDENTITY).read_bytes()
                self.assertEqual(len(identity),80)
                self.assertEqual(identity[:8],b'APXAUD1\0')
                self.assertEqual(zlib.crc32(identity[:-4]),struct.unpack('<I',identity[-4:])[0])
                later=volume/'SOUNDS/en/later.wav'; later.write_bytes(b'later custom recording')
                restored=installer.restore_backup(volume,result.backup,device,confirm=True)
            self.assertEqual(restored['state'],'complete')
            self.assertEqual(old.read_bytes(),b'personal audio'); self.assertEqual(later.read_bytes(),b'later custom recording')
            self.assertFalse((volume/'APEXTX-AUDIO.json').exists())
            self.assertFalse((volume/'APEXTX-AUDIO-RESTORE.json').exists())

    def test_personal_greeting_and_omitted_assignable_track_survive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            volume = root / "volume"
            hello = volume / "SOUNDS/es/SYSTEM/hello.wav"
            hello.parent.mkdir(parents=True)
            hello.write_bytes(b"personal greeting")
            track = volume / "SOUNDS/es/assigned.wav"
            track.write_bytes(b"track assigned to a switch")
            with mock.patch.object(installer.shutil, "disk_usage",
                                   return_value=SimpleNamespace(total=audio.NB4_VOLUME_BYTES, free=1024 * 1024)):
                result = installer.install(archive, volume, root / "backups", True)
            self.assertEqual(result.preserved, 1)
            self.assertEqual(hello.read_bytes(), b"personal greeting")
            self.assertEqual(track.read_bytes(), b"track assigned to a switch")

    def test_insufficient_space_changes_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            volume = root / "volume"
            volume.mkdir()
            with mock.patch.object(installer.shutil, "disk_usage",
                                   return_value=SimpleNamespace(total=audio.NB4_VOLUME_BYTES, free=0)):
                with self.assertRaisesRegex(RuntimeError, "free space"):
                    installer.install(archive, volume, root / "backups", True)
            self.assertEqual(list(volume.iterdir()), [])
            self.assertFalse((root / "backups").exists())

    def test_rejects_duplicate_and_symbolic_link_archives(self) -> None:
        import stat
        import warnings
        from zipfile import ZipInfo
        for kind in ("duplicate", "symlink"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = root / "bad.zip"
                with warnings.catch_warnings(), ZipFile(archive, "w") as package:
                    warnings.simplefilter("ignore")
                    if kind == "duplicate":
                        package.writestr("SOUNDS/es/x.wav", b"x")
                        package.writestr("sounds/ES/x.wav", b"y")
                    else:
                        info = ZipInfo("SOUNDS/es/x.wav")
                        info.external_attr = (stat.S_IFLNK | 0o777) << 16
                        package.writestr(info, b"/private/file")
                with self.assertRaises(RuntimeError):
                    installer.safe_extract(archive, root / "output")

    def test_forged_audio_sizes_cannot_bypass_storage_budget(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_pack(root)
            extracted = root / "extracted"
            installer.safe_extract(archive, extracted)
            manifest_path = extracted / "APEXTX-AUDIO.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["files"][0]["bytes"] = 1
            manifest["installedBytes"] = 1
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(RuntimeError, "length"):
                installer.read_and_verify_pack(extracted)


if __name__ == "__main__":
    unittest.main()
