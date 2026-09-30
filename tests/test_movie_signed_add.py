"""Exercise production trapping ADD with boundary and overflow operands."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_add_signed_boundaries():
    source = (ROOT / 'pc_port/mips_formatter.c').read_text()
    body = source.split('        case 32: {', 1)[1].split('        case 33:', 1)[0]
    harness = r'''
#include <stdint.h>
#include <limits.h>
#include <assert.h>
typedef struct { uint32_t r[32]; uint32_t pc; } CPU;
/* The production ADD body refuses overflow through this helper; the probe
 * only cares that the refusal happens before the destination is published. */
static int formatter_refuse0(int line, uint32_t pc) { (void)line; (void)pc; return 0; }
static int step(CPU *cpu) {
 unsigned rs=1, rt=2, rd=3;
 switch(32) { case 32: {
''' + body + r'''
 } return 1;
}
int main(void) {
 CPU c={{0},0};
 const uint32_t pairs[][3] = {
 {0,0,0},{0x7fffffff,0,0x7fffffff},{0x80000000,0,0x80000000},
 {0xffffffff,1,0},{0x80000000,0x7fffffff,0xffffffff},
 {0x80000000,0xffffffff,0},{0x7fffffff,1,0},
 {0x80000000,0x80000000,0},{0x7fffffff,0x7fffffff,0}
 };
 for(unsigned i=0;i<9;i++) {
  c.r[1]=pairs[i][0];c.r[2]=pairs[i][1];c.r[3]=0x12345678;
  int ok=step(&c);assert(ok==(i<5));
  assert(c.r[3]==(i<5?pairs[i][2]:0x12345678));
 }
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp)
        (p/'probe.c').write_text(harness)
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        str(p/'probe.c'), '-o', str(p/'probe')], check=True)
        subprocess.run([str(p/'probe')], check=True)
