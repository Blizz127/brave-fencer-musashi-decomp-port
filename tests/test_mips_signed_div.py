"""Strict and sanitizer validation for the header-local MIPS signed DIV helper."""

from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "tests/mips_signed_div_probe.c"
INCLUDE = ROOT / "pc_port/include"
EXPECTED = "MIPS_SIGNED_DIV_PASS directed=12 random=100000 oracle=bitlongdivision\n"


def _compile_and_run(tmp_path, compiler, extra_flags=()):
    binary = tmp_path / "mips_signed_div_probe"
    subprocess.run([
        compiler,
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-Wconversion",
        "-Wsign-conversion",
        "-Werror",
        *extra_flags,
        "-I",
        str(INCLUDE),
        str(PROBE),
        "-o",
        str(binary),
    ], check=True, timeout=30)
    return subprocess.run([str(binary)], check=False, capture_output=True,
                          text=True, timeout=15)


def test_mips_signed_div_strict_compile_and_run(tmp_path):
    result = _compile_and_run(tmp_path, "cc")
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout == EXPECTED


def test_mips_signed_div_clang_sanitizers(tmp_path):
    clang = shutil.which("clang")
    if clang is None:
        pytest.skip("clang is required for the standalone sanitizer check")
    result = _compile_and_run(
        tmp_path,
        clang,
        ("-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout == EXPECTED
    assert result.stderr == ""
