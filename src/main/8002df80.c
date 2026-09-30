/* Exact retail word export for [8002DF80,8002E138); EXE and assembly verified. */
#include "psx_types.h"

/* Verified: 110/110 words at vram 0x8002DF80.
 * Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s16 D_8006A982;
extern u8 D_8006A984;
void func_8002D320();                                  /* static */
void func_8002DC68(s32, s32);                            /* static */
void func_8002E138(s32, s32, s32);                           /* static */
void func_80030F80();                                  /* static */
void func_80031BE0();                                  /* static */
extern u8 D_8006A980;
extern s16 D_800A4EFC;
extern u8 D_800A4F17;
extern u8 D_800A4F18;

void func_8002DF80(void) {
    s32 var_a0;
    if (D_8006A980 & 7) {
        if ((D_8006A980 & 1) && (D_8006A980 & 2)) {
            func_8002DC68(0x719, 0);
            D_8006A980 &= 0xF8;
        }
        if (D_8006A980 & 7) {
            if (D_8006A982 <= 0) {
                var_a0 = 0x710;
            } else {
                var_a0 = 0x711;
                if (D_8006A982 >= 4) {
                    var_a0 = 0x712;
                }
            }
            func_8002DC68(var_a0, 0);
            D_8006A982 = 0;
            D_8006A980 &= 0xF8;
        }
    }
    if (D_8006A980 & 8) {
        if (D_8006A984 != 0) {
            func_8002DC68(0xA75, D_8006A984 | 0x1000);
            D_8006A984 = 0U;
            D_8006A980 |= 0x10;
        } else {
            D_8006A980 &= 0xEF;
            D_800A4F17 = 1;
            func_8002E138(4, 0xA75, 0);
            D_800A4F17 = D_800A4F18;
            if (D_800A4F18 != 0) {
                if (D_800A4EFC != 0) {
                    D_800A4EFC -= 1;
                }
                func_80030F80();
                func_8002D320();
                func_80031BE0();
                D_800A4F18 = 0;
                D_800A4F17 = 0;
            }
        }
        D_8006A980 &= 0xF7;
    }
}
