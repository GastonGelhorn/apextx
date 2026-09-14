# SPDX-License-Identifier: GPL-2.0-only
"""Calculate the reproducible source-tree digest used by NB4 acceptance."""

from __future__ import annotations

import hashlib
import os
import subprocess
from pathlib import Path


EXCLUDED = {
    "radio/src/targets/pl18/nb4_rf_qualified.cmake",
    "radio/src/targets/pl18/nb4_rf_qualified_generated.h",
}
ROOT_FILES = {".gitmodules", "CMakeLists.txt", "APEXTX_VERSION"}
SOURCE_PREFIXES = ("cmake/", "radio/")


def source_digest(root: Path) -> str:
    """Hash build inputs while excluding self-referential release evidence."""
    output = subprocess.check_output(
        ["git", "ls-files", "-s", "-z"], cwd=root)
    entries: dict[str, tuple[str, str | None]] = {}
    for entry in filter(None, output.split(b"\0")):
        metadata, raw_path = entry.split(b"\t", 1)
        mode, object_id, _stage = metadata.decode().split()
        entries[raw_path.decode()] = (mode, object_id)
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "-z"],
        cwd=root).split(b"\0")
    for raw_path in filter(None, untracked):
        path = raw_path.decode()
        executable = bool((root / path).stat().st_mode & 0o111)
        entries[path] = ("100755" if executable else "100644", None)

    digest = hashlib.sha256()
    for path in sorted(entries):
        mode, object_id = entries[path]
        if path in EXCLUDED or not (
                path in ROOT_FILES or path.startswith(SOURCE_PREFIXES)):
            continue
        if mode == "160000":
            assert object_id is not None
            contents_hash = object_id
        else:
            source = root / path
            if not source.exists() and not source.is_symlink():
                raise ValueError(f"tracked source is missing: {path}")
            contents = (os.readlink(source).encode() if source.is_symlink()
                        else source.read_bytes())
            contents_hash = hashlib.sha256(contents).hexdigest()
        digest.update(path.encode())
        digest.update(b"\0")
        digest.update(mode.encode())
        digest.update(b"\0")
        digest.update(contents_hash.encode())
        digest.update(b"\n")
    return digest.hexdigest()


if __name__ == "__main__":
    print(source_digest(Path(__file__).resolve().parents[1]))
