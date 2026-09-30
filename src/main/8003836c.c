/* Exact retail word export for [8003836C,800383A4); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u8 D_800B9ED3[];
extern u8 D_800B9ED2[];

/* The shared `i * 4` keeps `i` live in `$v0`, so the scaled index lands
 * in `$v1` instead of folding back into `$v0`; the convergent `rc`
 * keeps the taken block falling through into the shared `jr` with the
 * zero delivered in the `beqz` delay slot. */
s32 func_8003836C(s32 arg0) {
    s32 i = arg0 * 127;
    s32 rc;

    if (D_800B9ED3[i * 4] != 0) {
        rc = D_800B9ED2[i * 4];
    } else {
        rc = 0;
    }
    return rc;
}
