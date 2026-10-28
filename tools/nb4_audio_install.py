#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verified, resumable audio installation shared by the CLI and flasher."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import tempfile
import uuid
import wave
import struct
import zlib
from nb4_storage_backup import backup_storage, verify_backup, validate_storage_session
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from zipfile import ZipFile


MIN_NB4_VOLUME_BYTES = 7_500_000
MAX_NB4_VOLUME_BYTES = 9_000_000
MAX_ARCHIVE_BYTES = 12 * 1024 * 1024
MAX_AUDIO_BUDGET_BYTES = 7 * 1024 * 1024
MIN_STORAGE_RESERVE = 256 * 1024
JOURNAL = ".APEXTX-AUDIO-INSTALL.json"
JOURNAL_FORMAT = "apextx-audio-install-1"
SUPPORTED_LANGUAGES = ("en", "es", "de", "cn")
IDENTITY='APEXTX-AUDIO.ID'


def write_identity(volume: Path, manifest: dict, archive: Path):
    release=manifest['release'].encode('ascii')
    payload=struct.pack('<8s2sH32s32s',b'APXAUD1',manifest['language'].encode('ascii'),1,release,bytes.fromhex(digest(archive)))
    payload+=struct.pack('<I',zlib.crc32(payload))
    target=volume/IDENTITY; staging=target.with_name('.'+target.name+'.'+uuid.uuid4().hex+'.tmp')
    with staging.open('xb') as stream: stream.write(payload); stream.flush(); os.fsync(stream.fileno())
    os.replace(staging,target); sync_directory(volume)


@dataclass(frozen=True)
class InstallResult:
    language: str
    sounds: int
    installed: bool
    backup: Path | None = None
    preserved: int = 0
    required_free: int = 0


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def relative_path(value: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 240:
        raise RuntimeError("Invalid audio path")
    relative = PurePosixPath(value)
    if (relative.is_absolute() or ".." in relative.parts or
            "\\" in value or ":" in value or "\0" in value or
            any(part.endswith((" ", ".")) for part in relative.parts) or
            relative.as_posix() != value):
        raise RuntimeError(f"Unsafe audio path: {value}")
    return value


def safe_extract(archive: Path, destination: Path) -> None:
    with ZipFile(archive) as source:
        expanded = 0
        names = set()
        for member in source.infolist():
            name = relative_path(member.filename.rstrip("/"))
            key = name.casefold()
            mode = member.external_attr >> 16
            if (key in names or member.flag_bits & 1 or
                    stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                raise RuntimeError("Duplicate, encrypted or special audio archive entry")
            names.add(key)
            expanded += member.file_size
            if expanded > MAX_ARCHIVE_BYTES or len(names) > 2048:
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
    if not isinstance(manifest.get('release'),str) or not re.fullmatch('[A-Za-z0-9.+_-]{1,31}',manifest['release']):
        raise RuntimeError('Invalid audio release identity')
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
                or type(entry["bytes"]) is not int
                or entry["bytes"] <= 0
                or type(entry["allocatedBytes"]) is not int
                or entry["allocatedBytes"] != allocated_bytes(entry["bytes"])
            ):
                raise TypeError
            int(entry["sha256"], 16)
        expected = {entry["path"]: entry["sha256"] for entry in entries}
        for name in expected:
            relative_path(name)
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
    for entry in entries:
        path = root / entry["path"]
        if not path.is_file() or path.is_symlink() or path.stat().st_size != entry["bytes"]:
            raise RuntimeError("Audio file length does not match its manifest")
        try:
            with wave.open(str(path), "rb") as audio:
                if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(),
                        audio.getcomptype()) != (1, 2, 8000, "NONE"):
                    raise RuntimeError("Audio must be mono 16-bit PCM WAV at 8 kHz")
        except (wave.Error, EOFError) as error:
            raise RuntimeError("Invalid audio recording") from error
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


def sync_directory(path: Path) -> None:
    # Windows does not permit opening directories for fsync. File handles are
    # flushed on every platform; the UI also requires safe USB ejection.
    if os.name == "nt":
        return
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def atomic_json(path: Path, value: dict) -> None:
    temporary = path.with_name(path.name + ".partial")
    if temporary.is_symlink() or path.is_symlink():
        raise RuntimeError("Refusing a symbolic-link journal")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    sync_directory(path.parent)


def snapshot(root: Path) -> dict:
    """Only ordinary files are eligible for backup or replacement."""
    result = {}
    if not root.exists():
        return result
    if root.is_symlink():
        raise RuntimeError("Refusing a symbolic-link audio directory")
    keys = set()
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError("Refusing a symbolic link in radio audio")
        if path.is_dir():
            continue
        if not path.is_file():
            raise RuntimeError("Refusing a special file in radio audio")
        name = relative_path(path.relative_to(root).as_posix())
        if name.casefold() in keys:
            raise RuntimeError("Audio paths collide on the radio filesystem")
        keys.add(name.casefold())
        result["SOUNDS/" + name] = dict(sha256=digest(path), bytes=path.stat().st_size)
    return result


def allocated_bytes(size: int) -> int:
    return (size + 511) // 512 * 512


def previous_manifest(volume: Path) -> dict:
    path = volume / "APEXTX-AUDIO.json"
    if path.is_symlink():
        raise RuntimeError("Refusing a symbolic-link audio manifest")
    try:
        if path.stat().st_size > 512 * 1024:
            return {}
        data = json.loads(path.read_text(encoding="utf-8"))
        if (data.get("formatVersion") != 1 or data.get("target") != "NB4" or
                data.get("language") not in SUPPORTED_LANGUAGES):
            return {}
        result = {}
        prefix = "SOUNDS/" + data["language"] + "/"
        for entry in data["files"]:
            name = relative_path(entry["path"])
            if (not name.startswith(prefix) or name in result or
                    not re.fullmatch("[0-9a-f]{64}", entry["sha256"])):
                return {}
            result[name] = entry["sha256"]
        return result
    except (OSError, ValueError, TypeError, KeyError):
        # With no trustworthy baseline every existing recording is personal.
        return {}


def audio_plan(volume: Path, manifest: dict) -> dict:
    old = snapshot(volume / "SOUNDS")
    baseline = previous_manifest(volume)
    wanted = dict(old)
    writes = {}
    preserved = 0
    language = manifest["language"]
    prefix = f"SOUNDS/{language}/"
    incoming = {entry["path"]: entry for entry in manifest["files"]}

    for name, info in old.items():
        # A known, unchanged previous-language file belongs to the old pack.
        # Unknown/custom files remain untouched, including a custom greeting.
        managed = baseline.get(name) == info["sha256"]
        if managed and not name.startswith(prefix):
            wanted.pop(name)
        if "/SYSTEM/" not in name and name.lower().endswith(".wav"):
            # Carry forward assignable tracks omitted from the new pack.
            # This preserves references without guessing every YAML schema.
            target = prefix + name.split("/", 2)[2]
            if target not in incoming:
                carried = writes.get(target)
                if (carried and carried["location"] == "backup" and
                        carried["sha256"] != info["sha256"]):
                    raise RuntimeError("Several personal tracks share the same target name; resolve the conflict before changing language")
                if target not in wanted:
                    wanted[target] = dict(info)
                    writes[target] = dict(source=name, location="backup", **info)
                    preserved += 1

    for name, entry in incoming.items():
        info = dict(sha256=entry["sha256"], bytes=entry["bytes"])
        existing = old.get(name)
        personal = existing and baseline.get(name) != existing["sha256"]
        if personal and existing["sha256"] != info["sha256"]:
            preserved += 1
            continue
        wanted[name] = info
        if existing != info:
            writes[name] = dict(source=name, location="pack", **info)

    keys = [name.casefold() for name in wanted]
    if len(keys) != len(set(keys)):
        raise RuntimeError("Personal and packaged audio paths collide")
    deletes = [name for name in old if name not in wanted]
    # Delete only unmodified files from a verified previous pack. File-sized
    # staging avoids requiring enough room for two complete language packs.
    reclaimed = sum(allocated_bytes(old[name]["bytes"]) for name in deletes)
    growth, peak = 0, 0
    for name, entry in sorted(writes.items()):
        size = allocated_bytes(entry["bytes"])
        peak = max(peak, growth + size)
        growth += size - allocated_bytes(old.get(name, {}).get("bytes", 0))
    reserve = max(MIN_STORAGE_RESERVE, allocated_bytes(len(json.dumps(manifest))) + 8192)
    return dict(before=old, wanted=wanted, writes=writes, deletes=deletes,
                preserved=preserved, required_free=max(0, peak - reclaimed) + reserve)


def save_journal(volume: Path, backup: Path, journal: dict) -> None:
    # Keep the complete immutable plan on the computer. Rewriting it on NOR
    # for every recording would consume settings space and wear flash.
    host = backup / "install.json"
    if not host.exists():
        atomic_json(host, journal)
    receipt = {key: journal[key] for key in ("format", "id", "archive")}
    receipt["plan_sha256"] = hashlib.sha256(
        json.dumps(journal["plan"], sort_keys=True).encode()).hexdigest()
    receipt["completed"] = journal.get("completed", 0)
    receipt["complete"] = journal.get("complete", False)
    atomic_json(backup / "progress.json", receipt)
    atomic_json(volume / JOURNAL, receipt)


def install(
    archive: Path, volume: Path, backup_parent: Path, confirm: bool,
    *, resume: bool = False, device: dict | None = None, progress=lambda *_: None
) -> InstallResult:
    def check_owner():
        if device is not None: validate_storage_session(volume,**device)
    check_owner()
    if not volume.is_dir():
        raise RuntimeError(f"USB Storage volume not found: {volume}")
    usage = shutil.disk_usage(volume)
    if usage.total < MIN_NB4_VOLUME_BYTES or usage.total > MAX_NB4_VOLUME_BYTES:
        raise RuntimeError(
            f"Refusing unexpected {usage.total}-byte volume; select the NB4 storage root"
        )
    pending = volume / JOURNAL
    if pending.exists() and not resume:
        raise RuntimeError("An interrupted audio install needs --resume and its original backup directory")
    if resume and not pending.is_file():
        raise RuntimeError("No interrupted audio install was found")
    if volume.is_symlink():
        raise RuntimeError("Select the real radio volume, not a symbolic link")
    with tempfile.TemporaryDirectory(prefix="apextx-audio-install-") as temporary:
        extracted = Path(temporary)
        safe_extract(archive, extracted)
        manifest, sounds = read_and_verify_pack(extracted)
        language = manifest["language"]
        volume_root = volume.resolve()
        backup_root = backup_parent.resolve()
        if backup_root == volume_root or volume_root in backup_root.parents:
            raise RuntimeError("The audio backup directory must be on the computer")
        if resume:
            if pending.is_symlink() or pending.stat().st_size > 1024 * 1024:
                raise RuntimeError("Invalid interrupted audio journal")
            journal = json.loads(pending.read_text(encoding="utf-8"))
            identifier = journal.get("id", "")
            if (journal.get("format") != JOURNAL_FORMAT or
                    not re.fullmatch("apextx-audio-[0-9a-f]{32}", identifier)):
                raise RuntimeError("Invalid interrupted audio journal")
            backup = backup_parent / identifier
            host = backup / "install.json"
            if (backup.is_symlink() or not host.is_file() or host.is_symlink() or
                    host.stat().st_size > 1024 * 1024):
                raise RuntimeError("The original computer backup is required")
            trusted = json.loads(host.read_text(encoding="utf-8"))
            # The host can be one checkpoint ahead if the cable was removed
            # between its flush and the corresponding radio journal flush.
            for key in ("format", "id", "archive"):
                if trusted.get(key) != journal.get(key):
                    raise RuntimeError("The radio and computer audio journals disagree")
            plan_hash = hashlib.sha256(
                json.dumps(trusted["plan"], sort_keys=True).encode()).hexdigest()
            if journal.get("plan_sha256") != plan_hash:
                raise RuntimeError("The audio recovery plan failed verification")
            journal = trusted
            if journal.get('device')!=device:
                raise RuntimeError('The interrupted audio installation belongs to another storage session')
            if journal["archive"] != digest(archive):
                raise RuntimeError("Resume requires the exact original audio package")
            plan = journal["plan"]
            verify_backup(backup)
            if snapshot(backup / "data/SOUNDS") != plan["before"]:
                raise RuntimeError("The computer audio backup failed verification")
        else:
            plan = audio_plan(volume, manifest)
            if usage.free < plan["required_free"]:
                raise RuntimeError("Not enough free space for audio and the radio storage reserve; personal files were not removed")
            if not confirm:
                return InstallResult(language, manifest["installedFiles"], False,
                                     preserved=plan["preserved"], required_free=plan["required_free"])
            identifier = "apextx-audio-" + uuid.uuid4().hex
            backup = backup_parent / identifier
            progress('Backing up radio storage before audio',5)
            backup_storage(volume, backup, device=device)
            if snapshot(backup / "data/SOUNDS") != plan["before"]:
                raise RuntimeError("Audio backup verification failed; radio was not changed")
            sync_directory(backup)
            journal = dict(format=JOURNAL_FORMAT, id=identifier,
                           archive=digest(archive), plan=plan, completed=0, device=device)
            save_journal(volume, backup, journal)

        if not confirm:
            return InstallResult(language, manifest["installedFiles"], False, backup,
                                 plan["preserved"], plan["required_free"])
        if resume:
            for name in plan["writes"]:
                check_owner()
                relative_path(name)
                target = volume / name
                staging = target.with_name("." + target.name + "." + journal["id"] + ".partial")
                if staging.is_symlink():
                    raise RuntimeError("Refusing a symbolic-link staging file")
                if staging.exists():
                    staging.unlink()
        # Unexpected edits while mounted are a conflict, never an instruction
        # to overwrite a newly changed personal recording.
        current = snapshot(volume / "SOUNDS")
        if any(name not in current and name not in plan["writes"] for name in plan["wanted"]):
            raise RuntimeError("A personal recording was removed during installation; resolve the conflict before continuing")
        for name, info in current.items():
            if info not in (plan["before"].get(name), plan["wanted"].get(name)):
                raise RuntimeError("Radio audio changed during installation; keep the backup and resolve the conflict")
        for name in plan["deletes"]:
            check_owner()
            relative_path(name)
            path = volume / name
            if path.exists():
                if digest(path) != plan["before"][name]["sha256"]:
                    raise RuntimeError("A personal audio file changed; it was not deleted")
                path.unlink()
                sync_directory(path.parent)
        for name, entry in sorted(plan["writes"].items()):
            check_owner()
            relative_path(name)
            target = volume / name
            if target.is_file() and digest(target) == entry["sha256"]:
                continue
            source = (extracted if entry["location"] == "pack" else backup / "data") / relative_path(entry["source"])
            if not source.is_file() or source.is_symlink() or digest(source) != entry["sha256"]:
                raise RuntimeError("An audio source changed; installation stopped")
            target.parent.mkdir(parents=True, exist_ok=True)
            staging = target.with_name("." + target.name + "." + journal["id"] + ".partial")
            if staging.is_symlink():
                raise RuntimeError("Refusing a symbolic-link staging file")
            # Only our own incomplete staging file can be replaced on resume.
            with source.open("rb") as src, staging.open("wb") as dst:
                shutil.copyfileobj(src, dst, 64 * 1024)
                dst.flush()
                os.fsync(dst.fileno())
            if digest(staging) != entry["sha256"]:
                raise RuntimeError("Copied audio checksum verification failed")
            check_owner()
            os.replace(staging, target)
            sync_directory(target.parent)
            journal["completed"] += 1
            save_journal(volume, backup, journal)
            progress('Installing audio files',10+journal['completed']*85//max(1,len(plan['writes'])))
        if snapshot(volume / "SOUNDS") != plan["wanted"]:
            raise RuntimeError("Installed audio verification failed; the computer backup was retained")
        installed_manifest = dict(manifest, preservedFiles=plan["preserved"])
        check_owner()
        atomic_json(volume / "APEXTX-AUDIO.json", installed_manifest)
        write_identity(volume,manifest,archive)
        journal["complete"] = True
        journal["completed"] = len(plan["writes"])
        save_journal(volume, backup, journal)
        pending.unlink()
        sync_directory(volume)
        progress('Audio files independently verified',100)
        return InstallResult(
            language=language,
            sounds=manifest["installedFiles"],
            installed=True,
            backup=backup,
            preserved=plan["preserved"],
            required_free=plan["required_free"],
        )


def restore_backup(volume: Path, backup: Path, device: dict, *, confirm: bool = False, progress=lambda *_: None) -> dict:
    if not confirm: raise RuntimeError('Confirm restoration of this radio audio backup')
    validate_storage_session(volume,**device)
    metadata=verify_backup(backup)
    if metadata.get('device')!=device: raise RuntimeError('The audio backup belongs to another radio/storage session')
    host=backup/'install.json'
    if host.is_symlink() or not host.is_file() or host.stat().st_size>1024*1024:
        raise RuntimeError('The original verified audio-install record is required')
    original=json.loads(host.read_text(encoding='utf-8'))
    if (original.get('format')!=JOURNAL_FORMAT or original.get('device')!=device or
            not re.fullmatch('apextx-audio-[0-9a-f]{32}',original.get('id',''))):
        raise RuntimeError('The audio restoration identity does not match')
    plan=original['plan']
    if any(not relative_path(name).startswith('SOUNDS/') for section in ('before','wanted','writes') for name in plan[section]):
        raise RuntimeError('An audio restoration path is outside SOUNDS')
    if snapshot(backup/'data/SOUNDS')!=plan['before']: raise RuntimeError('The previous audio backup changed')
    pending=volume/JOURNAL
    if pending.exists():
        if pending.is_symlink() or pending.stat().st_size>65536 or json.loads(pending.read_text()).get('id')!=original['id']:
            raise RuntimeError('Another audio installation is pending; keep its original recovery record')
    current=snapshot(volume/'SOUNDS')
    for name,info in current.items():
        if name in plan['before'] or name in plan['wanted']:
            if info not in (plan['before'].get(name),plan['wanted'].get(name)):
                raise RuntimeError('A recording was modified after installation; resolve the conflict before restoring')
    reclaimed=sum(allocated_bytes(info['bytes']) for name,info in current.items() if name in plan['wanted'] and name not in plan['before'])
    growth=sum(max(0,allocated_bytes(info['bytes'])-allocated_bytes(current.get(name,{}).get('bytes',0))) for name,info in plan['before'].items())
    temporary=max((allocated_bytes(current[name]['bytes']) for name in plan['before'] if name in current and current[name]!=plan['before'][name]),default=0)
    if shutil.disk_usage(volume).free+reclaimed < growth+temporary+MIN_STORAGE_RESERVE:
        raise RuntimeError('Not enough storage to restore audio while keeping settings/history reserve')
    marker=volume/'APEXTX-AUDIO-RESTORE.json'
    record=dict(format='apextx-audio-restore-1',device=device,backup=str(backup.resolve()),
                installation=original['id'],state='restoring')
    if marker.exists():
        if marker.is_symlink() or marker.stat().st_size>65536 or json.loads(marker.read_text(encoding='utf-8'))!=record:
            raise RuntimeError('Another audio restoration is pending; use its original backup')
    atomic_json(backup/'restore.json',record); atomic_json(marker,record)
    for name in plan['writes']:
        validate_storage_session(volume,**device)
        target=volume/relative_path(name)
        staging=target.with_name('.'+target.name+'.'+original['id']+'.partial')
        if staging.is_symlink(): raise RuntimeError('Invalid interrupted audio staging path')
        if staging.is_file(): staging.unlink()
    for name,info in plan['wanted'].items():
        if name in plan['before']: continue
        validate_storage_session(volume,**device)
        target=volume/relative_path(name)
        if target.is_file():
            if digest(target)!=info['sha256']: raise RuntimeError('An installed recording changed during restoration')
            target.unlink(); sync_directory(target.parent)
    for number,(name,info) in enumerate(plan['before'].items()):
        validate_storage_session(volume,**device)
        target=volume/relative_path(name); source=backup/'data'/name
        if target.is_file() and digest(target)==info['sha256']: continue
        target.parent.mkdir(parents=True,exist_ok=True)
        staging=target.with_name('.'+target.name+'.'+original['id']+'.restore.partial')
        if staging.is_symlink(): raise RuntimeError('Invalid audio restoration staging path')
        with source.open('rb') as src,staging.open('wb') as dst:
            shutil.copyfileobj(src,dst); dst.flush(); os.fsync(dst.fileno())
        if digest(staging)!=info['sha256']: raise RuntimeError('Audio restoration transfer failed verification')
        validate_storage_session(volume,**device); os.replace(staging,target); sync_directory(target.parent)
        progress('Restoring previous audio files',(number+1)*99//max(1,len(plan['before'])))
    restored=snapshot(volume/'SOUNDS')
    if any(restored.get(name)!=info for name,info in plan['before'].items()):
        raise RuntimeError('The restored audio does not match the verified backup')
    validate_storage_session(volume,**device)
    previous=backup/'data/APEXTX-AUDIO.json'; installed=volume/'APEXTX-AUDIO.json'
    if previous.is_file(): atomic_json(installed,json.loads(previous.read_text(encoding='utf-8')))
    elif installed.exists(): installed.unlink()
    previous_id=backup/'data'/IDENTITY
    if previous_id.is_file():
        staging=volume/('.'+IDENTITY+'.restore.tmp')
        if staging.is_symlink(): raise RuntimeError('Invalid audio identity restore path')
        with staging.open('wb') as stream:
            stream.write(previous_id.read_bytes()); stream.flush(); os.fsync(stream.fileno())
        os.replace(staging,volume/IDENTITY)
    elif (volume/IDENTITY).exists(): (volume/IDENTITY).unlink()
    if pending.exists(): pending.unlink()
    record['state']='complete'; atomic_json(backup/'restore.json',record)
    marker.unlink(); sync_directory(volume)
    progress('Previous audio restored and verified',100)
    return dict(state='complete',restored=len(plan['before']),backup=str(backup))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("volume", type=Path,
                        help="Mounted NB4 USB Storage root")
    parser.add_argument("--backup-dir", type=Path, default=Path.home() / "ApexTX-Backups")
    parser.add_argument("--yes", action="store_true",
                        help="Back up and install, preserving personal audio")
    parser.add_argument("--resume", action="store_true",
                        help="Resume with the same package and computer backup directory")
    args = parser.parse_args()
    try:
        result = install(args.archive.resolve(), args.volume.resolve(),
                         args.backup_dir.resolve(), args.yes, resume=args.resume)
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
                f"Preserves {result.preserved} personal tracks. "
                "Run again with --yes to back up and install."
            )
    except Exception as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
