from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
WORD_INCLUDE = re.compile(r'#include "([^"]+_words\.inc)"')
WORD_EXPORT = re.compile(r'MUSASHI_NATIVE_MIPS_WORD\(0x[0-9A-Fa-f]{8}\)')
ASM_WORD = re.compile(r'"\.word 0x[0-9A-Fa-f]{8}')
ASM_LABELS = {
    "80044670_full_words.inc": "func_80044670",
    "80043b9c_words.inc": "func_80043B9C",
}
PAC_INCLUDES = {
    "sc01_pac0_words.inc": (0, 128483, "66a047a6242db0d212e27087b7576b19dbc12e399ae0d4f64f370e70bea35069"),
    "sc01_pac1_words.inc": (1, 207899, "cc18c89264c5a8f99041a0e88aad8491936f9c2a98a17b86e0dafe8a3b54ea09"),
}


def _source_for_include(name):
    title = re.fullmatch(r"([0-9a-f]{8})_(sc01_0000|sc02_0031)_words.inc", name)
    if title:
        return ROOT / f"src/overlays/{title[2]}/{title[1]}.c"
    if name == "800cf02c_overlay_words.inc":
        return ROOT / "src/overlays/main_0007/800cf02c.c"
    overlay = re.fullmatch(r"([0-9a-f]{8})_overlay([0-9]{4})_words.inc", name)
    if overlay:
        source = ROOT / f"src/overlays/main_{overlay[2]}/{overlay[1]}.c"
        assert source.is_file(), source
        return source
    if name in ASM_LABELS:
        return ROOT / "asm/main.s"
    # CMake writes 80044670_words.inc from the 25-word prefix; the full
    # 263-word body is 80044670_full_words.inc from asm/main.s.
    if name == "80044670_words.inc":
        return ROOT / "src/main" / "80044670_prefix.c"
    stem = name[:-len("_words.inc")]
    direct = ROOT / "src/main" / f"{stem}.c"
    if direct.is_file():
        return direct
    prefix = ROOT / "src/main" / f"{stem}_prefix.c"
    if prefix.is_file():
        return prefix
    if name in ASM_LABELS:
        return ROOT / "asm/main.s"
    raise AssertionError(f"no audited source for {name}")


def _expected_words(source, include_name):
    if include_name in ASM_LABELS:
        text = source.read_text()
        label = ASM_LABELS[include_name]
        start = text.index(f"glabel {label}")
        end = text.index(f"endlabel {label}", start)
        return len(re.findall(r"/\* [0-9A-F]+ [0-9A-F]{8} [0-9A-F]{8} \*/",
                              text[start:end]))
    source_text = source.read_text()
    exported = WORD_EXPORT.findall(source_text)
    return len(exported) or len(ASM_WORD.findall(source_text))


EXE = ROOT / "extracted/disc/files/SLUS_007.26"
EXE_BASE, EXE_TEXT = 0x80010000, 0x800
RANGE_LIST = ROOT / "tests/data/formatter_word_ranges.txt"


def _word_ranges():
    """<stem> -> guest word addresses, from the committed address list."""
    ranges = {}
    for line in RANGE_LIST.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        stem, count = line.split()
        count = int(count)
        if stem == "8004239c_minus_one":
            # The export joins the entry path to its distant epilogue.
            addresses = list(range(0x8004239C, 0x80042400, 4)) + \
                list(range(0x800424CC, 0x800424E4, 4))
        else:
            start = int(stem[:8], 16)
            addresses = list(range(start, start + 4 * count, 4))
        assert len(addresses) == count, stem
        ranges[stem] = addresses
    ranges["80044670_full"] = ranges["80044670"]
    return ranges


def _exe_bytes():
    if not EXE.is_file():
        try:
            import pytest
            pytest.skip("licensed pinned EXE unavailable")
        except ImportError:
            import unittest
            raise unittest.SkipTest("licensed pinned EXE unavailable")
    return EXE.read_bytes()


def _exe_word(exe, address):
    offset = address - EXE_BASE + EXE_TEXT
    return int.from_bytes(exe[offset:offset + 4], "little")


def _write_words(path, words):
    # Same layout tools/extract_asm_words.py used: one header line, then words.
    path.write_text("/* Generated from the local extracted EXE; do not edit. */\n" +
                    "".join(f"0x{w:08x}u,\n" for w in words))


def _generate_formatter_includes(output, pac=False):
    """Materialize <stem>_words.inc for every admitted main-exe range from the
    developer's own extracted EXE (never from src/ word exports). The port no
    longer compiles these; tests that assert or mutate words read them here
    and push mutations to the probe with _sync_code_image()."""
    exe = _exe_bytes()
    for stem, addresses in _word_ranges().items():
        _write_words(output / f"{stem}_words.inc", [_exe_word(exe, a) for a in addresses])
    for name in PAC_INCLUDES if pac else ():
        # SC01 PAC members stay on the disc; only tests that ask extract them.
        member, words, sha = PAC_INCLUDES[name]
        archive = ROOT / "extracted/disc/files/SC01.CD"
        if not archive.is_file():
            continue
        subprocess.run([sys.executable, str(ROOT / "tools/extract_pac_words.py"), str(archive),
                        str(output / name), "--member", str(member), "--expected-words",
                        str(words), "--expected-sha256", str(sha)], check=True, timeout=60)


OVERLAY_MEMBERS = {
    # name: (source, guest base)
    "MAIN_0004": ("extracted/overlays/main/0004.bin", 0x800CE5F8),  # file carries a 0x800 header
    "MAIN_0007": ("extracted/overlays/main/0007.bin", 0x800CEDF8),
    "MAIN_0010": ("extracted/overlays/main/0010.bin", 0x800AEDF8),
    "MAIN_0012": ("extracted/overlays/main/0012.bin", 0x80100158),
}


def _materialize_overlays(output):
    """Decode the overlay members probes need from the local disc extract
    into output/<name>.bin + <name>.base and point MUSASHI_OVERLAY_DIR there
    (tests/probe_code_words.h). SC02 member 31 is decoded from SC02.CD."""
    import os
    import struct
    for name, (source, base) in OVERLAY_MEMBERS.items():
        path = ROOT / source
        if path.is_file():
            (output / f"{name}.bin").write_bytes(path.read_bytes())
            (output / f"{name}.base").write_text(f"{base:08x}\n")
    archive = ROOT / "extracted/disc/files/SC02.CD"
    if archive.is_file():
        sys.path.insert(0, str(ROOT))
        from tools.decode_title_pac import decode
        data = archive.read_bytes()
        sector, size = struct.unpack_from("<II", data, 8 + 31 * 8)
        member = data[sector * 2048:sector * 2048 + size]
        code, _ = decode(member[0x800:struct.unpack_from("<4I", member)[3]])
        (output / "SC02_031.bin").write_bytes(code)
        (output / "SC02_031.base").write_text("80128158\n")
    os.environ["MUSASHI_OVERLAY_DIR"] = str(output)
    return output


def _sync_code_image(output):
    """Apply every <stem>_words.inc in output to a copy of the EXE and point
    the probes at it (MUSASHI_CODE_IMAGE, test-only MUSASHI_CODE_IMAGE_MUTANT).
    tests/conftest.py restores the pristine image after each test."""
    import os
    exe = bytearray(_exe_bytes())
    word = re.compile(r"0x([0-9a-fA-F]{8})u")
    for stem, addresses in _word_ranges().items():
        path = output / f"{stem}_words.inc"
        if not path.is_file():
            continue
        words = [int(w, 16) for w in word.findall(path.read_text())]
        assert len(words) == len(addresses), path
        for address, value in zip(addresses, words):
            offset = address - EXE_BASE + EXE_TEXT
            exe[offset:offset + 4] = value.to_bytes(4, "little")
    image = output / "code_image.exe"
    image.write_bytes(bytes(exe))
    os.environ["MUSASHI_CODE_IMAGE"] = str(image)
    if bytes(exe) != EXE.read_bytes():
        os.environ["MUSASHI_CODE_IMAGE_MUTANT"] = "1"
    return image


def test_bios_event_callback_executor_exact_targets_and_refusals():
    with tempfile.TemporaryDirectory(prefix="musashi-bios-event-callback-") as temp:
        probe = Path(temp) / "probe"
        generated = Path(temp) / "generated"
        generated.mkdir()
        _generate_formatter_includes(generated)
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-O2", "-Wno-parentheses",
            "-ffunction-sections", "-fdata-sections",
            "-Wl,--gc-sections",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "pc_port/include"),
            "-I", str(generated),
            str(ROOT / "tests/bios_event_callback_probe.c"),
            str(ROOT / "pc_port/boot_memory.c"),
            str(ROOT / "pc_port/mips_formatter.c"),
            str(ROOT / "pc_port/bios_events.c"),
            "-lcrypto",
            "-o", str(probe),
        ], check=True, timeout=60)
        subprocess.run([str(probe)], check=True, timeout=10)
