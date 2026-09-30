"""Mounted LIST/CD owner contract with real media and bounded refusal controls."""
from pathlib import Path
import subprocess
import tempfile

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _compile(probe: Path, cd_source: Path, output: Path) -> None:
    subprocess.run(
        [
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"), str(probe), str(cd_source),
            str(ROOT / "pc_port/disc_media.c"), "-lcrypto", "-o", str(output),
        ],
        check=True,
        timeout=30,
    )


@pytest.mark.parametrize("mutation", [None])
def test_cd_list_owner_direct_compile(mutation, tmp_path):
    cue = ROOT / "extracted/disc/disc.cue"
    image = ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not image.exists():
        pytest.skip("licensed pinned media unavailable")
    probe = ROOT / "tests/cd_list_owner_probe.c"
    executable = tmp_path / "cd-list-owner"
    _compile(probe, ROOT / "pc_port/cd_controller.c", executable)
    result = subprocess.run(
        [str(executable), str(cue), str(image)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout == "CD_LIST_OWNER_PASS\n"


def test_cd_list_owner_rejects_fake_publication_mutant(tmp_path):
    """A publication that marks requested FIFO ready must fail the source fixture."""
    cue = ROOT / "extracted/disc/disc.cue"
    image = ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not image.exists():
        pytest.skip("licensed pinned media unavailable")
    source = (ROOT / "pc_port/cd_controller.c").read_text()
    needle = "s->announced_valid = 1;"
    replacement = "s->announced_valid = 1; s->fifo_valid = 1;"
    assert source.count(needle) == 1
    mutated = tmp_path / "mutated_cd_controller.c"
    mutated.write_text(source.replace(needle, replacement))
    executable = tmp_path / "cd-list-owner-mutant"
    _compile(ROOT / "tests/cd_list_owner_probe.c", mutated, executable)
    result = subprocess.run(
        [str(executable), str(cue), str(image)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode != 0, "fake publication mutant unexpectedly passed"


def test_cd_list_owner_rejects_repeated_bfrd_switch_mutant(tmp_path):
    """A repeated BFRD that relatches announced bytes must fail old-view checks."""
    cue = ROOT / "extracted/disc/disc.cue"
    image = ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not image.exists():
        pytest.skip("licensed pinned media unavailable")
    source = (ROOT / "pc_port/cd_controller.c").read_text()
    needle = "if (!s->data_requested) {"
    replacement = "if (1) {"
    assert source.count(needle) == 1
    mutated = tmp_path / "mutated_cd_controller.c"
    mutated.write_text(source.replace(needle, replacement))
    executable = tmp_path / "cd-list-owner-repeat-mutant"
    _compile(ROOT / "tests/cd_list_owner_probe.c", mutated, executable)
    result = subprocess.run(
        [str(executable), str(cue), str(image)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode != 0, "repeated-BFRD switch mutant unexpectedly passed"
