/* Exact retail word export for [80034AE0,80034B0C); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u8 D_800A4F17;

/* Holding the flag address across the block materializes it once
 * (`lui` + `addiu` into `$v1`) instead of folding a fresh `lui` per
 * access; the restore sinks into the `jr` delay slot. */
void func_80034AE0(u8 *arg0) {
    u8 *p = &D_800A4F17;
    u8 save = *p;

    *p = 1;
    *(arg0 + 0x2A) = 1;
    *(u16 *)(arg0 + 0x26) = 0x3FF;
    *(u16 *)(arg0 + 0x28) = 0;
    *p = save;
}
