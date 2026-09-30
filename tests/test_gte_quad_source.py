"""Quad packets and the CFC2/BLTZ load delay use the real source and GTE."""
import pytest
from test_gte_init_source import _run_gte_source_probe

@pytest.mark.parametrize("variant", ["baseline", "immediate_load_mutant", "branch_early_retire_mutant"])
def test_gte_quad_source(tmp_path, variant):
    def mutate(source):
        if variant == "immediate_load_mutant":
            old = "cpu->gte_load_pending = 1;"
            new = old + "\n        cpu->r[rt] = scheduled_gte_load;"
        else:
            old = "if (!libgs_prim_delayed_branch(cpu->pc)) {"
            new = "if (1) {"
        assert source.count(old) == 1
        return source.replace(old, new)
    _run_gte_source_probe(
        tmp_path, variant, probe_file="gte_quad_source_probe.c",
        expected="GTE_QUAD_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED\n",
        extra_spans=((0x8004b614, 115),), real_gte=True,
        mutate_formatter=mutate if variant.endswith("mutant") else None)
