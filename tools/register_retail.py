#!/usr/bin/env python3
"""Register and verify the pinned Brave Fencer Musashi CHD."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from retail_common import EXPECTED_SHA256, EXPECTED_SIZE, RetailError, load_json, resolve_repo_root, verify_chd, write_json_atomic


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("chd", nargs="?", type=Path, default=Path(os.environ.get("BFM_CHD", "Brave Fencer Musashi (USA).chd")))
    parser.add_argument("--manifest", type=Path, help="manifest JSON (default: provenance/manifest.json)")
    parser.add_argument("--dry-run", action="store_true", help="verify only; do not update provenance")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = resolve_repo_root(Path(__file__))
    manifest_path = (args.manifest or root / "provenance/manifest.json").resolve()
    chd_path = args.chd.expanduser().resolve()
    try:
        digest = verify_chd(chd_path)
        manifest = load_json(manifest_path)
        media = manifest.setdefault("media", {})
        if not isinstance(media, dict):
            raise RetailError("provenance/manifest.json media field must be an object")
        pinned_size = media.get("size_bytes", EXPECTED_SIZE)
        pinned_sha = media.get("sha256", EXPECTED_SHA256)
        if pinned_size != EXPECTED_SIZE or str(pinned_sha).lower() != EXPECTED_SHA256:
            raise RetailError("manifest pin does not match the known USA CHD identity")
        media["source_path"] = str(chd_path)
        media["size_bytes"] = EXPECTED_SIZE
        media["sha256"] = digest
        if not args.dry_run:
            write_json_atomic(manifest_path, manifest)
        print(f"CHD VERIFIED size={EXPECTED_SIZE} sha256={digest}")
        print(f"manifest={'DRY_RUN' if args.dry_run else 'UPDATED'} path={manifest_path}")
        return 0
    except RetailError as exc:
        print(f"register_retail: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
