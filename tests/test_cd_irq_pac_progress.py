"""Budget renewal must prove bounded output growth across consecutive sectors."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_pac_progress_guard(tmp_path):
    source = (ROOT / 'pc_port/mips_formatter.c').read_text()
    start = source.index('static int cd_irq_pac_progress(')
    end = source.index('\nstatic int cd_irq_ram_word', start)
    code = '#include <stdint.h>\n#include <limits.h>\n#include <assert.h>\n' + source[start:end]
    code += r'''
int main(void) {
 assert(cd_irq_pac_progress(0x80128000,0x801291a4,6164,0,UINT_MAX));
 assert(cd_irq_pac_progress(0x801291a4,0x80129ef8,6165,0x801291a4,6164));
 assert(!cd_irq_pac_progress(0x801291a4,0x801291a4,6165,0x801291a4,6164));
 assert(!cd_irq_pac_progress(0x801291a4,0x80129000,6165,0x801291a4,6164));
 assert(!cd_irq_pac_progress(0x801291a4,0x80129ef8,6164,0x801291a4,6164));
 assert(!cd_irq_pac_progress(0x801291a4,0x80129ef8,6166,0x801291a4,6164));
 assert(!cd_irq_pac_progress(0x80129000,0x80129ef8,6165,0x801291a4,6164));
 assert(!cd_irq_pac_progress(0x801ff000,0x80200004,1,0,UINT_MAX));
 assert(!cd_irq_pac_progress(0x00000000,0x00001000,1,0,UINT_MAX));
 assert(!cd_irq_pac_progress(0x801291a4,0x80129ef8,0,0x801291a4,UINT_MAX-1));
 uint32_t tile=999;
 assert(cd_irq_image_cursor(0x00010000,&tile) && tile==0);
 assert(cd_irq_image_cursor(0x00800000,&tile) && tile==7);
 assert(cd_irq_image_cursor(0x01010000,&tile) && tile==8);
 assert(cd_irq_image_cursor(0x3f800000,&tile) && tile==511);
 assert(cd_irq_image_cursor(0x40010000,&tile) && tile==512);
 assert(!cd_irq_image_cursor(0,&tile));
 assert(!cd_irq_image_cursor(0x00030000,&tile));
 assert(!cd_irq_image_cursor(0x40020000,&tile));
 assert(!cd_irq_image_cursor(0x41010000,&tile));
 assert(cd_irq_sector_progress(6162,6163,UINT_MAX));
 assert(cd_irq_sector_progress(6163,6164,6163));
 assert(!cd_irq_sector_progress(6163,6163,6163));
 assert(!cd_irq_sector_progress(6163,6162,6163));
 assert(!cd_irq_sector_progress(6163,6165,6163));
 assert(!cd_irq_sector_progress(6163,6164,6162));
 assert(!cd_irq_sector_progress(UINT_MAX,0,UINT_MAX));
 /* cd_irq_bank_progress is production code in this slice; exercise it so the
  * probe both covers it and does not fail -Werror=unused-function. */
 assert(cd_irq_bank_progress(1,2,6163,6164,6163));
 assert(!cd_irq_bank_progress(4,2,6163,6164,6163));
 assert(!cd_irq_bank_progress(1,0,6163,6164,6163));
 assert(!cd_irq_bank_progress(1,2,6163,6163,6163));
 return 0;
}
'''
    path = tmp_path / 'probe.c'
    path.write_text(code)
    binary = tmp_path / 'probe'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
