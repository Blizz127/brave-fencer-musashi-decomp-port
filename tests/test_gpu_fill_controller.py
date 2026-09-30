"""Focused GP0(02) controller tests with strict and sanitizer builds."""

from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "tests/gpu_fill_controller_probe.c"
CONTROLLER = ROOT / "pc_port/gpu_controller.c"
INCLUDE = ROOT / "pc_port/include"
EXPECTED = "GPU_FILL_CONTROLLER_PASS full_wrap=1 quantization=1 partial_reset=1 refusals=1\n"


def _compile_and_run(tmp_path, compiler="cc", source=CONTROLLER,
                     extra_flags=()):
    binary = tmp_path / "gpu_fill_controller_probe"
    subprocess.run([
        compiler, "-std=c99", "-Wall", "-Wextra", "-Wconversion",
        "-Wsign-conversion", "-Werror", *extra_flags,
        "-I", str(INCLUDE), str(PROBE), str(source), "-o", str(binary),
    ], check=True, timeout=30)
    return subprocess.run([str(binary)], capture_output=True, text=True,
                          check=False, timeout=20)


def test_gpu_fill_controller_strict_compile_and_run(tmp_path):
    result = _compile_and_run(tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout == EXPECTED


def test_gpu_fill_controller_sanitizers(tmp_path):
    clang = shutil.which("clang")
    if clang is None:
        pytest.skip("clang is required for the sanitizer check")
    result = _compile_and_run(
        tmp_path, clang,
        extra_flags=("-O1", "-g", "-fsanitize=address,undefined",
                     "-fno-omit-frame-pointer"),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout == EXPECTED
    assert result.stderr == ""


@pytest.mark.parametrize("old, new", [
    (
        "((color >> 9) & 0x7c00u)",
        "((color >> 8) & 0x7c00u)",
    ),
    (
        "if (!backend_clear_fifo(controller)) return 0;\n"
        "        discard_fill(controller);",
        "if (!backend_clear_fifo(controller)) return 0;\n"
        "        (void)controller;",
    ),
])
def test_gpu_fill_controller_mutations_are_rejected(tmp_path, old, new):
    source = CONTROLLER.read_text()
    assert source.count(old) == 1
    mutant = tmp_path / "gpu_controller_mutant.c"
    mutant.write_text(source.replace(old, new))
    result = _compile_and_run(tmp_path, source=mutant)
    assert result.returncode != 0, "GPU fill semantic mutation unexpectedly passed"
