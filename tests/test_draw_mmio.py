"""Checked graphics bus routes and actual source partial-refusal fixture."""
from pathlib import Path
import subprocess
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("mutation", [None, "leak_refused_output", "remove_pointer_guard", "remove_initializer_guard",
                                      "bad_bcr_word", "bad_initializer_word"])
def test_checked_draw_mmio_and_source_guards(tmp_path, mutation):
    generated = tmp_path / "generated"
    generated.mkdir()
    _generate_formatter_includes(generated)
    probe = tmp_path / "probe"
    extra = []
    word_case = mutation in ("bad_bcr_word", "bad_initializer_word")
    if word_case:
        name, old, new, number = (
            ("8005b710_words.inc", "0xac400000", "0xac440000", 1)
            if mutation == "bad_bcr_word" else
            ("80042580_words.inc", "0x0040f809", "0x0060f809", 2))
        stream = generated / name
        original = stream.read_text()
        assert original.count(old) == 1
        stream.write_text(original.replace(old, new))
        extra = ["-DDRAW_MMIO_BAD_WORD=" + str(number)]
    elif mutation:
        source = (ROOT / "pc_port/mips_formatter.c").read_text()
        if mutation == "leak_refused_output":
            old = "cpu->gpu_io_device->read32(cpu->gpu_io_device->userdata,address,&accepted)) return 0;"
            new = "cpu->gpu_io_device->read32(cpu->gpu_io_device->userdata,address,value)) return 0;"
            # Keep accepted output defined too, so this tests the refusal leak
            # rather than a compiler warning about an uninitialized local.
            new += "\n        accepted = *value;"
        elif mutation == "remove_pointer_guard":
            old = "if (instruction != word || !musashi_boot_read32(memory,cell,&actual) || actual != expected ||\n            cpu->r[reg] != expected) return formatter_refuse0(__LINE__, cpu->pc);"
            new = "(void)cell; (void)actual; (void)expected; (void)reg; (void)word;"
        else:
            old = ("if (!code_site(0x80042598u, 16u, instruction) ||\n"
                   "            !musashi_boot_read32(memory,0x8006cb84u,&table) || table != 0x8006cb64u ||\n"
                   "            !musashi_boot_read32(memory,table+0xcu,&target) || target != 0x80042718u ||\n"
                   "            cpu->r[2] != target) return formatter_refuse0(__LINE__, cpu->pc);")
            new = "(void)table; (void)target;"
        assert source.count(old) == 1
        mutant = tmp_path / "formatter_mutant.c"
        mutant.write_text(source.replace(old, new))
        extra = ['-DDRAW_MMIO_FORMATTER="' + str(mutant) + '"']
    subprocess.run([
        "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-parentheses",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
        "-I", str(generated), str(ROOT / "tests/draw_mmio_probe.c"),
        str(ROOT / "pc_port/boot_memory.c"), str(ROOT / "pc_port/gpu_controller.c"),
        *extra, "-o", str(probe),
    ], check=True, timeout=40)
    _sync_code_image(generated)
    run = subprocess.run([str(probe)], capture_output=True, text=True, timeout=15)
    if word_case:
        assert run.returncode == 0, run.stdout + run.stderr
        assert run.stdout == "DRAW_WORD_GUARD_PASS\n"
        return
    if mutation:
        assert run.returncode != 0, "compiled semantic mutant escaped: " + mutation
        assert "Assertion" in run.stderr or "assertion" in run.stderr, run.stderr
        return
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout == "DRAW_MMIO_PASS routing=1 fetched_cases=38 callback_cases=6 real_control=1 dma=UNOWNED fixture_only=1\n"
