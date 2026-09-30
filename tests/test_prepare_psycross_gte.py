"""The generated GTE adaptation is exact and refuses source drift."""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_gte", ROOT / "tools/prepare_psycross_gte.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def test_exact_six_changes_and_drift_rejection():
    source = (ROOT / "tools/third_party/psycross/src/gte/PsyX_GTE.cpp").read_text()
    generated = module.prepare(source)
    restored = generated
    for selector in (1, 2, 3):
        old = f"(long long)CV{selector}(cv) << 12"
        new = f"(long long)CV{selector}(cv) * 4096LL"
        assert generated.count(new) == 2
        restored = restored.replace(new, old)
        with pytest.raises(ValueError):
            module.prepare(source.replace(old, "0", 1))
        with pytest.raises(ValueError):
            module.prepare(source + old)
    assert restored == source
    with pytest.raises(ValueError):
        module.prepare(generated)
