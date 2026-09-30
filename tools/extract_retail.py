#!/usr/bin/env python3
"""Extract a verified retail CHD into an ignored local payload.

The CHD is decompressed with chdman and the resulting 2352-byte disc image is
read by dumpsxiso, which understands both CD-XA sector forms. A generic ISO9660
reader cannot: it sees only the 2048-byte Form1 user area, so every Form2 file
(the MDEC .STR video streams and the XA .DA audio) is silently truncated to
roughly 88% of its real length. dumpsxiso also emits the MKPSXISO layout XML
that a rebuilt disc is later assembled from.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from retail_common import RetailError, load_json, resolve_repo_root, verify_chd, write_json_atomic


VENDORED_DUMPSXISO = Path("tools/third_party/mkpsxiso/build/dumpsxiso")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("chd", nargs="?", type=Path, help="verified CHD path (defaults to manifest media.source_path)")
    parser.add_argument("--manifest", type=Path, help="manifest JSON (default: provenance/manifest.json)")
    parser.add_argument("--output", type=Path, help="ignored extraction directory (default: extracted/disc)")
    parser.add_argument("--chdman", default="chdman", help="chdman executable name or path")
    parser.add_argument("--dumpsxiso", help="dumpsxiso executable name or path (default: vendored build, then PATH)")
    parser.add_argument("--dry-run", action="store_true", help="verify and print commands; do not extract or update provenance")
    return parser


def repo_relative(path: Path, root: Path) -> str:
    """Record a path relative to the repository when it lies inside it.

    A committed manifest full of one machine's absolute paths is unreadable from
    a fresh clone. Paths outside the repository stay absolute, since which
    external binary ran is provenance a relative path would destroy.
    """

    try:
        relative = path.relative_to(root)
    except ValueError:
        return str(path)
    return str(relative) if relative.parts else "."


def _run(command: list[str], label: str) -> None:
    try:
        completed = subprocess.run(command, check=False)
    except OSError as exc:
        raise RetailError(f"failed to execute {label}: {exc}") from exc
    if completed.returncode != 0:
        raise RetailError(f"{label} failed with exit code {completed.returncode}")


def resolve_dumpsxiso(root: Path, override: str | None) -> str:
    """Prefer an explicit path, then the vendored build, then PATH."""

    if override:
        resolved = shutil.which(override) or (str(Path(override)) if Path(override).is_file() else None)
        if resolved is None:
            raise RetailError(f"dumpsxiso not found at {override!r}; extraction stopped without writes")
        return resolved
    vendored = root / VENDORED_DUMPSXISO
    if vendored.is_file() and os.access(vendored, os.X_OK):
        return str(vendored)
    found = shutil.which("dumpsxiso")
    if found is None:
        raise RetailError(
            "required extractor 'dumpsxiso' was not found; build it with "
            f"'cmake -B build && cmake --build build' under {root / 'tools/third_party/mkpsxiso'} "
            "or pass --dumpsxiso PATH. Extraction stopped without writes."
        )
    return found


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = resolve_repo_root(Path(__file__))
    manifest_path = (args.manifest or root / "provenance/manifest.json").resolve()
    try:
        manifest = load_json(manifest_path)
        media = manifest.get("media")
        if not isinstance(media, dict):
            raise RetailError("manifest media field must be an object")
        chd_path = (args.chd or Path(str(media.get("source_path", "")))).expanduser().resolve()
        if not str(chd_path):
            raise RetailError("manifest media.source_path is empty; run register_retail.py first")
        verify_chd(chd_path)
        chdman = shutil.which(args.chdman)
        if chdman is None:
            raise RetailError(
                f"required extractor 'chdman' was not found ({args.chdman!r}); extraction stopped without writes"
            )
        dumpsxiso = resolve_dumpsxiso(root, args.dumpsxiso)
        output_dir = (args.output or root / "extracted/disc").resolve()
        if output_dir.exists():
            raise RetailError(f"extraction output already exists: {output_dir}; choose an absent path")

        print("EXTRACT COMMAND:", " ".join([chdman, "extractcd", "-i", str(chd_path), "-o", "<disc.cue>", "-ob", "<disc.bin>"]))
        print("DUMP COMMAND:", " ".join([dumpsxiso, "-x", "<files>", "-s", "<layout.xml>", "<disc.cue>"]))
        if args.dry_run:
            print("EXTRACT DRY_RUN: no payload or provenance writes")
            return 0

        parent = output_dir.parent
        parent.mkdir(parents=True, exist_ok=True)
        temporary_dir = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.", dir=parent))
        cue_path = temporary_dir / "disc.cue"
        bin_path = temporary_dir / "disc.bin"
        files_root = temporary_dir / "files"
        layout_path = temporary_dir / "layout.xml"
        try:
            _run([chdman, "extractcd", "-i", str(chd_path), "-o", str(cue_path), "-ob", str(bin_path)], "chdman extraction")
            if not cue_path.is_file() or not bin_path.is_file() or bin_path.stat().st_size == 0:
                raise RetailError("chdman reported success but did not produce a non-empty BIN/CUE pair")

            files_root.mkdir(parents=True, exist_ok=False)
            _run([dumpsxiso, "-x", str(files_root), "-s", str(layout_path), str(cue_path)], "dumpsxiso extraction")
            if not layout_path.is_file():
                raise RetailError("dumpsxiso reported success but wrote no layout XML")

            system_cnf = [path for path in files_root.rglob("*") if path.is_file() and path.name.lower() == "system.cnf"]
            if len(system_cnf) != 1:
                raise RetailError(f"expected exactly one SYSTEM.CNF after extraction, found {len(system_cnf)}")
            extracted_files = [path for path in files_root.rglob("*") if path.is_file()]
            if not extracted_files:
                raise RetailError("dumpsxiso produced no files")

            os.replace(temporary_dir, output_dir)
        except Exception:
            shutil.rmtree(temporary_dir, ignore_errors=True)
            raise

        extraction = manifest.setdefault("extraction", {})
        if not isinstance(extraction, dict):
            raise RetailError("manifest extraction field must be an object")
        extraction.update(
            {
                "status": "extracted",
                "tool": {
                    "chdman": repo_relative(Path(chdman), root),
                    "dumpsxiso": repo_relative(Path(dumpsxiso), root),
                },
                "output_directory": repo_relative(output_dir, root),
                "layout_xml": repo_relative(output_dir / "layout.xml", root),
                "file_count": len(extracted_files),
                "observed_at": None,
            }
        )
        write_json_atomic(manifest_path, manifest)
        print(f"EXTRACTED output={output_dir} files={len(extracted_files)}")
        return 0
    except RetailError as exc:
        print(f"extract_retail: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
