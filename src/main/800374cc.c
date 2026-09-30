/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

/* Exact retail word export for [800374CC,8003750C); EXE and assembly verified. */
#include "psx_types.h"

extern s32 D_8007623C;
extern u8 D_80076243;

s32 func_800374CC(s32 *arg0) {
    if ((D_80076243 != 4) && (D_80076243 != 0) && (D_80076243 != 5)) {
        *arg0 = D_8007623C;
        return 0;
    }
    return 1;
}
