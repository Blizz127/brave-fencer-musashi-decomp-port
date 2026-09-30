"""Member identity gates rotation/translation, and every matrix word is checked."""
from test_gte_init_source import _run_gte_source_probe


def test_gte_bank_pair_source(tmp_path):
    _run_gte_source_probe(
        tmp_path, "baseline", probe_file="gte_bank_pair_source_probe.c",
        expected="GTE_BANK_PAIR_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED\n",
        extra_spans=((0x8004914c, 12), (0x800491ac, 8)))
