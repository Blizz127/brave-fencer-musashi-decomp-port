/* Exact retail word export for [80034B0C,80034B3C); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u8 D_800A4F17;

/* Sibling of func_80034AE0 (only the last store differs): same held
 * flag address, same delay-sunk restore. */
void func_80034B0C(u8 *arg0) {
    u8 *p = &D_800A4F17;
    u8 save = *p;

    *p = 1;
    *(arg0 + 0x2A) = 1;
    *(u16 *)(arg0 + 0x26) = 0x3FF;
    *(u16 *)(arg0 + 0x28) = 0x3FFF;
    *p = save;
}
