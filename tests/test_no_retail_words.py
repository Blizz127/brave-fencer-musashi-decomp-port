"""Retail code must not be committed as instruction words.

The native port reads the game's code from the user's own disc at run time
(docs/PC-PORT.md, "Shipping model: run off the disc"), so no source needs the
retail instruction words that used to be exported next to each function.
This fails if they come back:

* no tracked file may carry a `MUSASHI_NATIVE_MIPS_WORD(...)` export; and
* `.word 0x...` assembly tiles in src/ are frozen at the files listed in
  tests/data/retail_asm_tiles_allowlist.txt (assembly-registered functions
  awaiting a decision), so no new tile can be added.

Test fixtures that build synthetic words live under tests/ and tools/ and are
exempt.
"""

from __future__ import annotations

import re
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ALLOWLIST = REPO / "tests/data/retail_asm_tiles_allowlist.txt"
# Paths temporarily exempt from the export check while a strip is in
# progress; keep this empty.
PENDING_EXPORT_STRIP: frozenset[str] = frozenset()
EXPORT_RE = re.compile(r"MUSASHI_NATIVE_MIPS_WORD\s*\(")
TILE_RE = re.compile(r"\.word\s+0x[0-9A-Fa-f]{8}")
EXEMPT_PREFIXES = ("tests/", "tools/")


def tracked(*pathspec: str) -> list[str]:
    out = subprocess.run(["git", "ls-files", "--", *pathspec], cwd=REPO,
                         check=True, capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line]


def read(path: str) -> str:
    return (REPO / path).read_text(encoding="utf-8", errors="replace")


class NoRetailWordsTests(unittest.TestCase):
    def test_no_word_exports_are_tracked(self) -> None:
        offenders = [
            path for path in tracked(".")
            if not path.startswith(EXEMPT_PREFIXES)
            and not any(path.startswith(p) for p in PENDING_EXPORT_STRIP)
            and (REPO / path).is_file()
            and EXPORT_RE.search(read(path))
        ]
        self.assertEqual(offenders, [], "retail word exports are back in these files")

    def test_asm_tiles_do_not_grow(self) -> None:
        allowed = {line.strip() for line in ALLOWLIST.read_text().splitlines()
                   if line.strip() and not line.startswith("#")}
        tiles = {path for path in tracked("src")
                 if (REPO / path).is_file() and TILE_RE.search(read(path))}
        self.assertEqual(sorted(tiles - allowed), [], "new .word assembly tiles in src/")


if __name__ == "__main__":
    unittest.main()
