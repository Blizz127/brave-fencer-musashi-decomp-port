/* Exact retail word export for [80031E94,80031F14); EXE and assembly verified. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
typedef struct {
    u16 flags;
    u8 pad[8];
    u8 unkA;
    u8 padB;
    u16 unkC;
    u8 rest[0x54-0xE];
} T;
extern T D_800A46E8[];
void func_80034A9C();
void func_80031E94(void) {
    s32 i;
    T *p = D_800A46E8;
    for (i = 0; i < 8; i++, p++) {
        u32 f = p->flags & 0x3F;
        if (f != 1 && f == 5)
            func_80034A9C(p);
    }
}
