/* Exact retail word export for [8002EB10,8002EBAC); EXE and assembly verified. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

void func_8002EC10();                                  /* static */
void func_80036F18();                                  /* static */
s32 func_800381E4(s16, s32);                          /* static */
s32 func_8003836C(s16);                             /* static */
void func_800383A4(s16);                               /* static */
extern s16 D_800A4E86;
extern u16 D_800A4E8E;

void func_8002EB10(void) {
    if ((D_800A4E8E & 4) && ((func_8003836C(D_800A4E86) << 0x10) != 0)) {
        if (func_800381E4(D_800A4E86, 0) != 0) {
            func_8002EC10();
        } else {
            func_800383A4(D_800A4E86);
            D_800A4E8E |= 0x10;
        }
    }
    func_80036F18();
}
