/* Exact retail word export for [80014CAC,80014CF8); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u16 D_80078DCA;
extern u16 D_80078DD2;
extern u16 D_80078E16;
extern u16 D_80078E1E;

s32 func_80014CAC(s32 arg0, s32 arg1) {
    s32 var_v0;

    if ((arg0 & 0xFF) == 1) {
        var_v0 = D_80078DCA | D_80078E16;
    } else {
        var_v0 = D_80078DD2 | D_80078E1E;
    }
    return (var_v0 & arg1) != 0;
}
