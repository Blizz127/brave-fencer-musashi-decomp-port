/* Exact retail word export for [8002EBAC,8002EC10); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_80036D58();                                 /* static */
void func_80038308(s16);                               /* static */
extern s16 D_800A4E86;
extern u16 D_800A4E8E;

void func_8002EBAC(void) {
    func_80036D58(0);
    if (D_800A4E8E & 0x10) {
        func_80038308(D_800A4E86);
        D_800A4E8E = (D_800A4E8E | 9) & 0xFFEF;
    }
}
