"""Retail transform words, real GTE and explicit test-owned caller frames."""
import pytest
from test_gte_init_source import _run_gte_source_probe

@pytest.mark.parametrize("variant", ["baseline", "output_mutant", "sanitizers"])
def test_gte_transform_source(tmp_path, variant, monkeypatch):
    if variant == "sanitizers":
        monkeypatch.setenv("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    def mutate(generated):
        path = generated / "8004945c_words.inc"
        source = path.read_text()
        assert source.count("0xa4a20000u") == 1
        path.write_text(source.replace("0xa4a20000u", "0xa4a20002u"))

    _run_gte_source_probe(
        tmp_path, variant, probe_file="gte_transform_source_probe.c",
        expected="GTE_TRANSFORM_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED\n",
        extra_spans=((0x8004914c, 12), (0x800491ac, 8), (0x8004945c, 12)),
        mutate_generated=mutate if variant == "output_mutant" else None,
        real_gte=True)
