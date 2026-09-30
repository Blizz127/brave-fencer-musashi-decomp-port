/* Exact retail word export for [80034844,800348A8); EXE and assembly verified. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
typedef struct {
    u16 flags;
    u8 pad[0x16-2];
    u8 unk16;
    u8 pad17[0x1A-0x17];
    s16 unk1A;
    u8 pad1C[0x37-0x1C];
    u8 unk37;
    u8 rest[0x54-0x38];
} T;
extern T D_800A46E8[];
void func_80034844(void) {
    s32 i;
    T *p = D_800A46E8;
    for (i = 0; i < 8; i++, p++) {
        if (p->flags == 5 && (p->unk37 & 2) == 0) {
            p->unk16 = 1;
            p->unk1A = 0x220;
        }
    }
}
