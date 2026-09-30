/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
extern u8 D_800B97D8[];
void func_8005C358();
void func_80014588(void) {
    s16 i;
    u8 *p = D_800B97D8;
    for (i = 0; i < 0x20; i++, p += 0x10) {
        *(s16 *)p = 0;
        func_8005C358(p, 0, 0x10);
    }
}
