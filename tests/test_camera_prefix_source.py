"""Retail camera prefix, real owners, explicit fixture source inputs."""
import pytest
from test_gte_init_source import _run_gte_source_probe

SPANS = ((0x800144d4,32),(0x80053308,8),(0x80053f6c,245),
         (0x80053b20,46),(0x8004787c,15),(0x800478b8,36),
         (0x80047948,40),(0x80054340,60),(0x80054430,50),
         (0x800544f8,7),(0x80047d3c,33))


@pytest.mark.parametrize("variant", ["baseline", "camera_scalar_mutant", "matrix_copy_mutant", "sanitizers"])
def test_camera_prefix_source(tmp_path, variant):
    def mutate(generated):
        if variant=="camera_scalar_mutant":
            path=generated/"800144d4_words.inc"
            old,new="0x2402fed4u","0x2402fed5u"
        elif variant=="matrix_copy_mutant":
            path=generated/"80053f6c_words.inc"
            old,new="0xaea20000u","0x00000000u"
            lines=path.read_text().splitlines()
            index=1+(0x80053fac-0x80053f6c)//4
            assert lines[index]==old+","
            lines[index]=new+","
            path.write_text("\n".join(lines)+"\n")
            return
        else:
            return
        source=path.read_text()
        assert source.count(old)==1
        path.write_text(source.replace(old,new))
    _run_gte_source_probe(tmp_path,variant,"camera_prefix_source_probe.c",
        "CAMERA_PREFIX_SOURCE_PASS fixture_only=1 mtc2=REFUSED\n",SPANS,mutate)
