/* Exact retail word export for [80037334,80037358); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u8 D_80076251[];

/* Identical twin of func_80037004: same table, same top-down
 * zeroing. See there for the codegen notes. */
void func_80037334(void) {
    s32 i = 0x40;

    do {
        D_80076251[i] = 0;
        i -= 0x10;
    } while (i >= 0);
}
