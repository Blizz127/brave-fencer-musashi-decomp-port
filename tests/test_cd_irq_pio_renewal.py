"""Execute production PIO grant/completion/selection fragments with hostile state."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_pio_renewal_requires_completed_consecutive_sector(tmp_path):
    source = (ROOT / 'pc_port/mips_formatter.c').read_text()
    dispatch = source[source.index('int musashi_boot_execute_cd_irq_with_services('):]
    begin = dispatch.index('                if (call == 0x8003ae78u')
    end = dispatch.index('                returns[depth].pc = call+8u;', begin)
    grant = dispatch[begin:end]
    begin = dispatch.index('                if (depth == 4 && returns[0].target == 0x800377d8u')
    end = dispatch.index('                if (depth && returns[depth-1].target == 0x8001a338u)', begin)
    complete = dispatch[begin:end]
    begin = dispatch.index('        int bank_pio = depth >= 4')
    end = dispatch.index('        if (!*budget)', begin)
    select = dispatch[begin:end]
    code = r'''
#include <assert.h>
#include <stdint.h>
#include <limits.h>
typedef struct { uint32_t pc,sp,target; } Return;
typedef struct { uint32_t r[32],pc; } Cpu;
static uint32_t sector;
static int readable=1;
static int musashi_boot_read32(void *memory,uint32_t address,uint32_t *out) {
    (void)memory;assert(address==0x8007623c);if(!readable)return 0;*out=sector;return 1;
}
static unsigned pio_remaining,remaining=16384,decode_remaining;
static int pio_budget_granted,pio_completed;
static uint32_t pio_lba;
static Return returns[6];
static Cpu cpu;
static int grant(unsigned depth,uint32_t call,uint32_t expected) {
    void *memory=0;
    do {
''' + grant + r'''
        return 1;
    } while(0);
    return 0;
}
static void complete(unsigned depth) {
''' + complete + r'''
}
static unsigned *select_budget(unsigned depth) {
''' + select + r'''
    return budget;
}
int main(void) {
    returns[0]=(Return){.target=0x800377d8,.pc=0x80045308};
    returns[3]=(Return){.target=0x8003a84c,.pc=0x8003ae80};
    cpu.r[5]=2048;sector=100;
    assert(grant(3,0x8003ae78,0x8003a84c));
    assert(pio_remaining==2312048u && !pio_completed && pio_lba==100);
    assert(remaining==16384);
    pio_remaining=17;
    sector=101;
    assert(!grant(3,0x8003ae78,0x8003a84c)); /* no completed return */
    complete(5);assert(!pio_completed); /* nested delay return */
    assert(!grant(3,0x8003ae78,0x8003a84c));
    returns[3].pc+=4;complete(4);assert(!pio_completed);returns[3].pc-=4;
    complete(4);assert(pio_completed);
    sector=100;assert(!grant(3,0x8003ae78,0x8003a84c));
    sector=102;assert(!grant(3,0x8003ae78,0x8003a84c));
    sector=99;assert(!grant(3,0x8003ae78,0x8003a84c));
    assert(pio_remaining==17 && pio_lba==100);
    sector=101;assert(grant(3,0x8003ae78,0x8003a84c));
    assert(!pio_completed && pio_lba==101 && remaining==16384);
    assert(select_budget(4)==&pio_remaining && select_budget(5)==&pio_remaining);
    assert(select_budget(3)==&remaining);
    remaining=0;assert(*select_budget(3)==0); /* grants cannot revive outer budget */
    pio_completed=1;pio_lba=UINT_MAX;sector=0;
    assert(!grant(3,0x8003ae78,0x8003a84c));
    pio_lba=100;sector=101;readable=0;
    assert(!grant(3,0x8003ae78,0x8003a84c));readable=1;
    cpu.r[5]=0x7eff1;assert(!grant(3,0x8003ae78,0x8003a84c));
    cpu.r[5]=0x7eff0;assert(grant(3,0x8003ae78,0x8003a84c));
    assert(pio_remaining==569032048u && remaining==0);
    return 0;
}
'''
    path = tmp_path / 'pio.c'
    path.write_text(code)
    binary = tmp_path / 'pio'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
