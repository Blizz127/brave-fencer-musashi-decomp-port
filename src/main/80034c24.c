/* Exact retail word export for [80034C24,80034CF0); EXE-verified. */
/* Verified byte-exact against retail by tools/match_function.py
 * (51/51 words at 0x80034C24). Types and signatures are whatever
 * reproduces the bytes; they are not evidence of the original
 * declaration. */
#include "psx_types.h"

extern s32 D_8006AEE8;
extern s32 D_8006AEF0;
extern s8 D_8006AEF5;
extern s32 D_8006AEF8;
extern s32 D_8006AEFC;
extern s32 D_8006AF00;
s32 func_800430B8(void *);                             /* static */
extern s8 D_8006AEF4;
extern s32 D_8007610C;
extern s8 D_80076110;
extern s32 D_80076114;
extern s8 D_8007620C;
extern s8 D_80076214;
extern s32 *D_80078F20;
extern s32 D_800A5BC8;
extern s8 D_800A63E4;
extern s32 D_800A63E8;
extern s32 D_800A6544;
extern s32 D_800C6D28;
extern s32 *D_800C7D30;

void func_80034C24(void) {
    s32 *var_v0;
    s32 var_v1;

    var_v1 = 4;
    var_v0 = &D_80078F20;
    D_8006AEF8 = 0;
    D_8006AEFC = 0;
    D_8006AEF4 = 0;
    D_800A63E4 = 0;
    D_8006AEF0 = 0;
    D_80076110 = 0;
    D_8006AF00 = 0;
    D_800A6544 = 0;
    D_8006AEE8 = 0;
    D_800A63E8 = 0;
    D_8007610C = 0;
    D_8006AEF5 = 0;
    D_8007620C = 0;
    D_80076114 = 0;
    D_80076214 = 0;
    do {
        *var_v0 = 0;
        var_v1 -= 1;
        var_v0 -= 1;
    } while (var_v1 >= 0);
    D_800A5BC8 = func_800430B8(&D_800C7D30);
    D_800C6D28 = 0;
}
