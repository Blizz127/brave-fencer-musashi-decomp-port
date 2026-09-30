/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80014070();
void func_800128EC();
void func_800128B4(void *p) {
    s16 tmp[8];
    func_80014070(p, tmp);
    func_800128EC(tmp, p);
}
