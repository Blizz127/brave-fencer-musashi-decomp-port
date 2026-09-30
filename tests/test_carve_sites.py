"""Formatter site tables for the third carve batch."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
from test_bios_event_callbacks import _generate_formatter_includes, _materialize_overlays, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]


def _compile_probe(root, generated, probe_source):
    probe = root / "probe"
    subprocess.run([
        "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
        "-O2", "-Wno-parentheses",
        "-ffunction-sections", "-fdata-sections",
        "-Wl,--gc-sections",
        "-I", str(ROOT / "include"),
        "-I", str(ROOT / "pc_port/include"),
        "-I", str(generated),
        str(probe_source),
        str(ROOT / "pc_port/boot_memory.c"),
        "-lcrypto",
        "-o", str(probe),
    ], check=True, timeout=60)
    return probe


def test_carve_sites_are_admitted():
    with tempfile.TemporaryDirectory(prefix="musashi-carve-sites-") as temp:
        generated = Path(temp) / "generated"
        generated.mkdir()
        _generate_formatter_includes(generated)
        _materialize_overlays(generated)
        probe = _compile_probe(Path(temp), generated,
                               ROOT / "tests/carve_site_probe.c")
        _sync_code_image(generated)
        result = subprocess.run([str(probe)], check=True, timeout=10,
                                capture_output=True, text=True)
        assert "CARVE_SITE_PASS" in result.stdout
