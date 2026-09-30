/* Exact retail word export for [8002C8BC,8002C8F4); EXE-verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u8 D_800C5328[];
extern u8 D_800C532A[];

/* Dual halfword-table fill: the `-1` lives in `$a0` across the loop
 * (no call to clobber it) while the unsigned byte index walks in
 * `$v1`; both base addresses recompute each iteration. */
void func_8002C8BC(void) {
    s32 v = -1;
    u32 i = 0;

    do {
        *(s16 *)(D_800C5328 + i) = v;
        *(s16 *)(D_800C532A + i) = v;
        i += 4;
    } while (i < 0x1E4);
}
