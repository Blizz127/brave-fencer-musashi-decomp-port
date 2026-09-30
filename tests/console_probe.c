/* Isolated decomp execution with a recording BIOS sink, not a boot test. */
#include <assert.h>
#include "psx_types.h"

u8 D_800728A9[256];
s32 D_80078978;
static u8 output[16];
static s32 columns[16];
static unsigned count;

void func_8005CF38(s32 descriptor, const void *bytes, s32 length) {
    assert(descriptor == 1 && length == 1 && count < 16);
    columns[count] = D_80078978;
    output[count++] = *(const u8 *)bytes;
}

#include "../src/main/8005c388.c"

int main(void) {
    unsigned ch, mask, column;
    for (ch = 0; ch < 256; ++ch) {
        if (ch == '\t' || ch == '\n')
            continue;
        for (mask = 0; mask < 256; ++mask) {
            count = 0;
            D_80078978 = 7;
            D_800728A9[ch] = mask;
            func_8005C388(ch + 0x100);
            assert(count == 1 && output[0] == ch);
            assert(columns[0] == 7 + !!(mask & 0x97));
            assert(D_80078978 == columns[0]);
        }
    }
    D_800728A9[' '] = 1;
    for (column = 0; column < 16; ++column) {
        unsigned i;
        count = 0;
        D_80078978 = column;
        func_8005C388('\t');
        assert(count == 8 - (column & 7));
        for (i = 0; i < count; ++i) {
            assert(output[i] == ' ');
            assert(columns[i] == (s32)(column + i + 1));
        }
        assert(D_80078978 == (s32)(column + count));
    }
    count = 0;
    D_80078978 = 19;
    D_800728A9['\r'] = 1;
    func_8005C388('\n');
    assert(count == 2 && output[0] == '\r' && output[1] == '\n');
    assert(columns[0] == 20 && columns[1] == 0 && D_80078978 == 0);
    return 0;
}
