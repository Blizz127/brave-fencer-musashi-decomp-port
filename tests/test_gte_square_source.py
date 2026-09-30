"""Square0 executes under its audited SC01 member 77 caller only."""
from test_gte_init_source import _run_gte_source_probe


def test_gte_square_source(tmp_path):
    _run_gte_source_probe(
        tmp_path, "baseline", probe_file="gte_square_source_probe.c",
        expected="GTE_SQUARE_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED\n",
        extra_spans=((0x80049324, 10),), real_gte=True)
