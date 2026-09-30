"""The primitive's real GTE transfers and packet agree with simple geometry."""
from test_gte_init_source import _run_gte_source_probe


def test_gte_gouraud_source(tmp_path):
    _run_gte_source_probe(
        tmp_path, "baseline", probe_file="gte_gouraud_source_probe.c",
        expected="GTE_GOURAUD_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED\n",
        extra_spans=((0x8004b078, 96),), real_gte=True)
