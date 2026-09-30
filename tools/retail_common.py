"""Shared, fail-closed helpers for the retail provenance pipeline."""

from __future__ import annotations

import hashlib
import json
import os
import tempfile
from pathlib import Path
from typing import Any


EXPECTED_SIZE = 210_618_433
EXPECTED_SHA256 = "511e4bcae71bb09b5b7e4818aa303a6fa06c189a29dfdef55224a68d4e4150cc"
PSX_EXE_HEADER_SIZE = 2048
PSX_EXE_MAGIC = b"PS-X EXE"


class RetailError(RuntimeError):
    """A user-actionable provenance or extraction failure."""


def sha256_file(path: Path, chunk_size: int = 1024 * 1024) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(chunk_size):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as stream:
            value = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        raise RetailError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise RetailError(f"JSON root must be an object: {path}")
    return value


def write_json_atomic(path: Path, value: dict[str, Any], *, dry_run: bool = False) -> None:
    """Replace a JSON file via a same-directory temporary file and rename."""

    if dry_run:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary_path = Path(temporary)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, ensure_ascii=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, path)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise


def resolve_repo_root(script_file: Path) -> Path:
    return script_file.resolve().parents[1]


def verify_chd(path: Path, *, expected_size: int = EXPECTED_SIZE, expected_sha256: str = EXPECTED_SHA256) -> str:
    if not path.exists():
        raise RetailError(f"retail CHD does not exist: {path}")
    if not path.is_file():
        raise RetailError(f"retail CHD is not a regular file: {path}")
    actual_size = path.stat().st_size
    if actual_size != expected_size:
        raise RetailError(
            f"CHD size mismatch for {path}: expected {expected_size} bytes, got {actual_size}"
        )
    actual_sha256 = sha256_file(path)
    if actual_sha256.lower() != expected_sha256.lower():
        raise RetailError(
            f"CHD SHA-256 mismatch for {path}: expected {expected_sha256}, got {actual_sha256}"
        )
    return actual_sha256
