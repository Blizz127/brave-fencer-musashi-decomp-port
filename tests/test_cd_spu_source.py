"""Execute the exact 80044BF4 source leaf against owned CD/SPU devices."""

from pathlib import Path
import subprocess
import sys
import tempfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image


def test_cd_spu_source_owner_fixture():
    cue = ROOT / "extracted/disc/disc.cue"
    image = ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not image.exists():
        pytest.skip("pinned licensed disc image is not present locally")

    with tempfile.TemporaryDirectory(prefix="musashi-cd-spu-source-") as temp:
        temp_path = Path(temp)
        generated = temp_path / "generated"
        generated.mkdir()
        _generate_formatter_includes(generated)
        probe = temp_path / "cd_spu_source_probe"
        subprocess.run(
            [
                "cc",
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                "-Wno-parentheses",
                "-ffunction-sections",
                "-fdata-sections",
                "-Wl,--gc-sections",
                "-I",
                str(ROOT / "include"),
                "-I",
                str(ROOT / "pc_port/include"),
                "-I",
                str(generated),
                str(ROOT / "tests/cd_spu_source_probe.c"),
                str(ROOT / "pc_port/boot_memory.c"),
                str(ROOT / "pc_port/cd_controller.c"),
                str(ROOT / "pc_port/disc_media.c"),
                str(ROOT / "pc_port/spu_cd_audio.c"),
                "-lcrypto",
                "-o",
                str(probe),
            ],
            check=True,
            timeout=30,
        )
        _sync_code_image(generated)
        subprocess.run([str(probe), str(cue), str(image)], check=True, timeout=30)
