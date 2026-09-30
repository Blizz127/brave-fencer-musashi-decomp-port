"""Actual startup runner with real CPU/image/kernel/card/GTE owners.

The fixture uses a test-owned CPU/stack and shared GTE bank, never captured
RAM or synthetic SYS success. Unrelated monolithic-runner services trap.
"""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]
EXE_SHA256 = "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a"


def _run_gte_source_probe(tmp_path, variant, probe_file="gte_init_source_probe.c",
                          expected="GTE_INIT_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED\n",
                          extra_spans=(), mutate_generated=None, mutate_formatter=None,
                          real_gte=False):
    exe = ROOT / "extracted/disc/files/SLUS_007.26"
    if not exe.exists():
        pytest.skip("pinned licensed EXE unavailable")
    raw = exe.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == EXE_SHA256
    asm_words = {int(address, 16): bytes.fromhex(raw_word)
                 for address, raw_word in re.findall(
                     r"/\* [0-9A-F]+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/",
                     (ROOT / "asm/main.s").read_text())}
    generated = tmp_path / "generated"
    generated.mkdir()
    _generate_formatter_includes(generated)
    for address, count in (((0x80053cf8, 19), (0x80047cb4, 32),
                           (0x8004d16c, 40), (0x80053ad8, 8),
                           (0x8004921c, 6), (0x8005ce28, 3),
                           (0x80014444, 36), (0x8004923c, 3)) + tuple(extra_spans)):
        words = re.findall(r"0x([0-9a-fA-F]{8})u", (generated / f"{address:08x}_words.inc").read_text())
        assert len(words) == count
        actual = struct.pack(f"<{count}I", *(int(w, 16) for w in words))
        offset = address - 0x80010000 + 0x800
        assert actual == raw[offset:offset + len(actual)]
        assert actual == b"".join(asm_words[address + i * 4] for i in range(count))
    if mutate_generated is not None:
        mutate_generated(generated)
    if variant in ("slot_mutant", "sys_jr_mutant"):
        path = generated / ("8004d16c_words.inc" if variant == "slot_mutant" else "8005cf08_words.inc")
        text = path.read_text()
        old, new = ("0x8c420018u", "0x8c420014u") if variant == "slot_mutant" else ("0x03e00008u", "0x00000000u")
        assert text.count(old) == 1
        path.write_text(text.replace(old, new))
    if variant == "setter_mutant":
        path = generated / "8004923c_words.inc"
        text = path.read_text()
        assert text.count("0x48c4d000u") == 1
        path.write_text(text.replace("0x48c4d000u", "0x48c4c800u"))
    probe_source = ROOT / "tests" / probe_file
    if mutate_formatter is not None:
        formatter = tmp_path / "mips_formatter.c"
        formatter.write_text(mutate_formatter((ROOT / "pc_port/mips_formatter.c").read_text()))
        fixture = tmp_path / "gte_init_source_probe.c"
        fixture.write_text((ROOT / "tests/gte_init_source_probe.c").read_text().replace(
            '#include "../pc_port/mips_formatter.c"', f'#include "{formatter}"'))
        probe_source = tmp_path / probe_file
        probe_source.write_text((ROOT / "tests" / probe_file).read_text().replace(
            '#include "gte_init_source_probe.c"', f'#include "{fixture}"'))
    sources = [ROOT / "pc_port" / name for name in (
        "boot_memory.c", "cpu_status.c", "bios_exception.c", "bios_kernel.c", "bios_events.c", "bios_card.c")]
    if variant == "publication_mutant":
        path = tmp_path / "bios_exception.c"
        text = sources[2].read_text()
        old = "image->variant=variant;image->generation++;image->dirty=0;image->written_mask=0;"
        assert text.count(old) == 1
        path.write_text(text.replace(old, "(void)variant;"))
        sources[2] = path
    sanitize = variant == "sanitizers"
    cc, cxx = ("clang", "clang++") if sanitize else ("cc", "g++")
    flags = ["-Wall", "-Wextra", "-Werror", "-Wno-parentheses", "-ffunction-sections", "-fdata-sections"]
    flags += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitize else ["-O2"]
    includes = ["-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
                "-I", str(ROOT / "tools/third_party/psycross/include"), "-I", str(generated)]
    owner, inline, probe = tmp_path / "gte_owner.o", tmp_path / "inline.o", tmp_path / "probe"
    subprocess.run([cc, "-std=c99", *flags, *includes, "-c", str(ROOT / "pc_port/gte_owner.c"), "-o", str(owner)], check=True, timeout=40)
    subprocess.run([cxx, "-std=c++11", *flags, "-x", "c++", *includes, "-c",
                    str(ROOT / "tools/third_party/psycross/src/psx/INLINE_C.C"), "-o", str(inline)], check=True, timeout=40)
    link_extra = []
    if real_gte:
        # Probes that dispatch real vector cores link the actual operator
        # TU (bank, operator, LIM, leading-zero count) instead of the
        # abort-on-entry stubs; probes without MUSASHI_GTE_REAL_OPERATOR
        # keep the self-contained stubs and must not set this flag.
        gte_op = tmp_path / "gte_op.o"
        # Third-party TU: same -fpermissive tolerance the native
        # psycross_static target applies, without -Werror. First-party
        # probe and owner objects above keep strict warnings.
        tu_flags = [f for f in flags if f != "-Werror"] + ["-fpermissive"]
        # Same PGXP-off posture as the native psycross_static target: the
        # vendor config header otherwise defaults PGXP on, dragging in the
        # half-float and render-config dependencies of the NCLIP path.
        tu_flags += ["-DUSE_EXTENDED_PRIM_POINTERS=1", "-DUSE_PGXP=0"]
        prepared_gte = tmp_path / "PsyX_GTE.cpp"
        subprocess.run(["python3", str(ROOT / "tools/prepare_psycross_gte.py"),
                        str(ROOT / "tools/third_party/psycross/src/gte/PsyX_GTE.cpp"),
                        str(prepared_gte)], check=True, timeout=30)
        subprocess.run([cxx, "-std=c++11", *tu_flags, "-x", "c++", *includes,
                        "-I", str(ROOT / "tools/third_party/psycross/src/gte"), "-c",
                        str(prepared_gte),
                        "-o", str(gte_op)], check=True, timeout=60)
        link_extra = [str(gte_op)]
    subprocess.run([cc, "-std=c99", *flags, "-Wl,--gc-sections", *includes,
                    str(probe_source), *map(str, sources), str(owner), str(inline),
                    *link_extra,
                    "-lcrypto", "-lstdc++", "-o", str(probe)], check=True, timeout=300)
    _sync_code_image(generated)
    run = subprocess.run([str(probe), str(exe)], text=True, capture_output=True, timeout=30)
    if variant.endswith("mutant"):
        assert run.returncode != 0, f"semantic mutant unexpectedly passed: {variant}"
    else:
        assert run.returncode == 0, run.stdout + run.stderr
        assert run.stdout == expected


@pytest.mark.parametrize("variant", ["baseline", "slot_mutant", "sys_jr_mutant", "publication_mutant", "setter_mutant", "sanitizers"])
def test_gte_init_source_path_and_refusals(tmp_path, variant):
    _run_gte_source_probe(tmp_path, variant)
