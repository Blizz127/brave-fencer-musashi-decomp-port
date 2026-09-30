#!/usr/bin/env python3
"""Observe SYSTEM.CNF and PS-X EXE identity from an extracted disc payload."""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

from retail_common import PSX_EXE_HEADER_SIZE, PSX_EXE_MAGIC, RetailError, load_json, resolve_repo_root, sha256_file, write_json_atomic


BOOT_RE = re.compile(r"^\s*BOOT\s*=\s*(?P<target>[^;#\r\n]+)(?:;\s*\d+)?", re.IGNORECASE)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", nargs="?", type=Path, help="extracted disc directory (defaults to manifest extraction.output_directory)")
    parser.add_argument("--manifest", type=Path, help="manifest JSON (default: provenance/manifest.json)")
    parser.add_argument("--identity", type=Path, help="identity JSON (default: provenance/exe_identity.json)")
    parser.add_argument("--dry-run", action="store_true", help="observe and print identity; do not write provenance")
    return parser


def _normalized_target(value: str) -> str:
    value = value.strip().replace("/", "\\")
    value = value.split(";", 1)[0]
    value = re.sub(r"^cdrom\d*:", "", value, flags=re.IGNORECASE)
    return value.lstrip("\\/").lower()


def find_system_cnf(payload: Path) -> Path:
    candidates = [path for path in payload.rglob("*") if path.is_file() and path.name.lower() == "system.cnf"]
    if len(candidates) != 1:
        raise RetailError(f"expected exactly one SYSTEM.CNF under {payload}, found {len(candidates)}")
    return candidates[0]


def read_boot_target(system_cnf: Path) -> str:
    try:
        text = system_cnf.read_text(encoding="ascii", errors="strict")
    except UnicodeDecodeError as exc:
        raise RetailError(f"SYSTEM.CNF is not strict ASCII: {system_cnf}") from exc
    except OSError as exc:
        raise RetailError(f"cannot read {system_cnf}: {exc}") from exc
    for line in text.splitlines():
        match = BOOT_RE.match(line)
        if match:
            target = _normalized_target(match.group("target"))
            if not target:
                break
            return target
    raise RetailError(f"SYSTEM.CNF has no usable BOOT entry: {system_cnf}")


def find_boot_exe(payload: Path, target: str) -> Path:
    normalized_target = _normalized_target(target)
    matches = []
    for path in payload.rglob("*"):
        if not path.is_file():
            continue
        relative = str(path.relative_to(payload)).replace("/", "\\").lower()
        relative = relative.split(";", 1)[0]
        if relative == normalized_target or relative.endswith("\\" + normalized_target):
            matches.append(path)
    if len(matches) != 1:
        raise RetailError(f"BOOT target {target!r} matched {len(matches)} files under {payload}")
    return matches[0]


def parse_psx_exe(path: Path) -> dict[str, object]:
    try:
        with path.open("rb") as stream:
            header = stream.read(PSX_EXE_HEADER_SIZE)
    except OSError as exc:
        raise RetailError(f"cannot read PS-X EXE {path}: {exc}") from exc
    if len(header) < PSX_EXE_HEADER_SIZE:
        raise RetailError(f"PS-X EXE is shorter than its 2048-byte header: {path}")
    if header[: len(PSX_EXE_MAGIC)] != PSX_EXE_MAGIC:
        raise RetailError(f"BOOT target is not a PS-X EXE (missing {PSX_EXE_MAGIC!r} magic): {path}")
    fields = struct.unpack_from("<10I", header, 0x10)
    text_size = fields[3]
    payload_size = path.stat().st_size - PSX_EXE_HEADER_SIZE
    if text_size > payload_size:
        raise RetailError(
            f"PS-X EXE text size {text_size} exceeds available payload {payload_size}: {path}"
        )
    return {
        "magic": PSX_EXE_MAGIC.decode("ascii"),
        "initial_pc": fields[0],
        "initial_gp": fields[1],
        "text_address": fields[2],
        "text_size_bytes": fields[3],
        "data_address": fields[4],
        "data_size_bytes": fields[5],
        "bss_address": fields[6],
        "bss_size_bytes": fields[7],
        "initial_sp_base": fields[8],
        "initial_sp_offset": fields[9],
    }


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = resolve_repo_root(Path(__file__))
    manifest_path = (args.manifest or root / "provenance/manifest.json").resolve()
    identity_path = (args.identity or root / "provenance/exe_identity.json").resolve()
    try:
        manifest = load_json(manifest_path)
        extraction = manifest.get("extraction")
        default_payload = extraction.get("output_directory") if isinstance(extraction, dict) else None
        # The manifest records in-repo paths relative to the repository, so a
        # relative payload resolves against the repo root rather than the
        # working directory.
        payload = (args.payload or Path(str(default_payload or ""))).expanduser()
        if not payload.is_absolute():
            payload = root / payload
        payload = payload.resolve()
        if not payload.is_dir():
            raise RetailError(f"extracted payload directory does not exist: {payload}")
        system_cnf = find_system_cnf(payload)
        target = read_boot_target(system_cnf)
        exe_path = find_boot_exe(payload, target)
        header = parse_psx_exe(exe_path)
        identity = {
            "schema": 1,
            "status": "observed",
            "reason": "PS-X EXE observed from extracted SYSTEM.CNF BOOT target",
            "source_manifest": "provenance/manifest.json",
            "path": str(exe_path.relative_to(root)) if exe_path.is_relative_to(root) else str(exe_path),
            "file_size_bytes": exe_path.stat().st_size,
            "sha256": sha256_file(exe_path),
            "header_sha256": sha256_file(exe_path) if exe_path.stat().st_size == PSX_EXE_HEADER_SIZE else None,
            "header": header,
        }
        # Header hash is always over exactly the observed 2048-byte header.
        with exe_path.open("rb") as stream:
            identity["header_sha256"] = hashlib.sha256(stream.read(PSX_EXE_HEADER_SIZE)).hexdigest()
        boot = manifest.setdefault("boot", {})
        if not isinstance(boot, dict):
            raise RetailError("manifest boot field must be an object")
        boot.update({"status": "observed", "system_cnf": str(system_cnf.relative_to(root)) if system_cnf.is_relative_to(root) else str(system_cnf), "psx_exe": identity["path"]})
        if args.dry_run:
            print(f"IDENTITY DRY_RUN system_cnf={system_cnf} target={target} exe={exe_path}")
            print(f"IDENTITY sha256={identity['sha256']} size={identity['file_size_bytes']}")
            return 0
        write_json_atomic(identity_path, identity)
        write_json_atomic(manifest_path, manifest)
        print(f"IDENTITY OBSERVED system_cnf={system_cnf} target={target} exe={exe_path}")
        print(f"identity={identity_path} sha256={identity['sha256']} size={identity['file_size_bytes']}")
        return 0
    except RetailError as exc:
        print(f"identify_retail: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
