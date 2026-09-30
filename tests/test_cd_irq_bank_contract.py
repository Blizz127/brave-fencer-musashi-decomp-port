from test_gte_init_source import _run_gte_source_probe


def test_bank_contract_refusals(tmp_path):
    _run_gte_source_probe(tmp_path, 'baseline', 'cd_irq_bank_contract_probe.c',
                          expected='CD_IRQ_BANK_CONTRACT_PASS\n')
