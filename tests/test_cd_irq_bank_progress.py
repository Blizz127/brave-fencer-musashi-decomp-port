"""Bank allowance renewal requires active state and consecutive source progress."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_bank_progress_rejects_noop_error_and_nonconsecutive_sectors(tmp_path):
    source = (ROOT / 'pc_port/mips_formatter.c').read_text()
    start = source.index('static int cd_irq_sector_progress(')
    end = source.index('static int cd_irq_ram_word(', start)
    helper = source[start:end]
    code = '''#include <assert.h>
#include <stdint.h>
#include <limits.h>
''' + helper + '''
int main(void) {
    for(unsigned start=0;start<256;start++)
        for(unsigned end=0;end<256;end++)
            assert(cd_irq_bank_progress(start,end,100,101,UINT_MAX) ==
                   (start>=1 && start<=3 && end>=1 && end<=4));
    assert(cd_irq_bank_progress(3,3,100,101,100));
    assert(cd_irq_bank_progress(3,4,UINT_MAX-1,UINT_MAX,UINT_MAX-1));
    assert(!cd_irq_bank_progress(3,3,100,100,UINT_MAX));
    assert(!cd_irq_bank_progress(3,3,100,102,UINT_MAX));
    assert(!cd_irq_bank_progress(3,3,100,99,UINT_MAX));
    assert(!cd_irq_bank_progress(3,3,UINT_MAX,0,UINT_MAX));
    assert(!cd_irq_bank_progress(3,3,100,101,99));
    assert(!cd_irq_bank_progress(3,3,100,101,101));
    return 0;
}
'''
    path = tmp_path / 'bank.c'
    path.write_text(code)
    binary = tmp_path / 'bank'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
