"""The primitive's real GTE transfers and packet agree with simple geometry."""
from test_gte_init_source import _run_gte_source_probe


def test_gte_primitive_source(tmp_path):
    _run_gte_source_probe(
        tmp_path, "baseline", probe_file="gte_primitive_source_probe.c",
        expected="GTE_PRIMITIVE_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED\n",
        extra_spans=((0x8004a660, 77),), real_gte=True)
