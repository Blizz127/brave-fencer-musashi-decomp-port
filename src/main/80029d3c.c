/* Exact retail word export for [80029D3C,80029DB4); EXE and assembly verified. */
/* Verified byte-exact against retail by tools/match_function.py
 * (30/30 words at 0x80029D3C). Types and signatures are whatever
 * reproduces the bytes; they are not evidence of the original
 * declaration. */
#include "psx_types.h"

s32 func_8002A4FC(void);

s32 func_80029D3C(s32 arg0) {
    s32 var_v0;
    s32 rate;

    if (arg0 <= 0) {
        return 0;
    }
    rate = func_8002A4FC() * 0x3C;
    var_v0 = arg0 - ((arg0 * rate) / 4800);
    if (var_v0 <= 0) {
        var_v0 = 1;
    }
    return var_v0;
}
