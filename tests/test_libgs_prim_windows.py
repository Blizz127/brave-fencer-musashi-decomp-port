"""The libgs TMD primitive admission windows match the licensed EXE."""
import hashlib
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "extracted/disc/files/SLUS_007.26"


def test_windows_match_licensed_exe():
    if not EXE.exists():
        pytest.skip("pinned licensed EXE unavailable")
    generated = subprocess.run([sys.executable, str(ROOT / "tools/gen_libgs_prim_windows.py"), str(EXE)],
                               check=True, capture_output=True, text=True).stdout
    source = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert generated in source
    spans = re.findall(r"\{0x([0-9a-f]{8})u, 0x([0-9a-f]{8})u, kLibgsPrim", source)
    assert spans == [("8004a27c", "8004cfec"), ("8004d6bc", "800509c0")]
    for lo, hi in spans:
        table = re.search(r"kLibgsPrim%s\[\] = \{([^}]*)\}" % lo.upper(), source).group(1)
        assert len(re.findall(r"0x[0-9a-f]{16}ull", table)) == -(-(int(hi, 16) - int(lo, 16)) // 256)


def test_delayed_branch_sites_are_cfc2_bltz_pairs():
    words = {int(a, 16): w for a, w in re.findall(
        r"/\* [0-9A-F]+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/", (ROOT / "asm/main.s").read_text())}
    for pc in (0x8004B710, 0x8004B8DC, 0x8004CD38, 0x8004CF18):
        cfc2 = int.from_bytes(bytes.fromhex(words[pc - 4]), "little")
        bltz = int.from_bytes(bytes.fromhex(words[pc]), "little")
        assert cfc2 == 0x4842F800            # cfc2 $v0, $31 (FLAG)
        assert bltz >> 16 == 0x0440          # bltz $v0
