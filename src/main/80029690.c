/* Exact retail word export for [80029690,800296F8); EXE and assembly verified. */
/* Verified byte-exact against retail by tools/match_function.py
 * (26/26 words at 0x80029690). Types and signatures are whatever
 * reproduces the bytes; they are not evidence of the original
 * declaration. */
#include "psx_types.h"

void func_80016714(void *, s32);                            
extern s32 *D_80078F28;
extern s32 *D_800A5E58;

void func_80029690(void) {
    void *var_s1;
    s32 *var_s0;
    s32 var_s2;

    var_s2 = 0;
    var_s1 = &D_80078F28;
    var_s0 = &D_800A5E58;
    do {
        *var_s0 = 0;
        func_80016714(var_s1, 0x5B8);
        var_s1 += 0x2DC;
        var_s2 += 1;
        var_s0 += 1;
    } while (var_s2 < 2);
}
