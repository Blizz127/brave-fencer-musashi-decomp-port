/* Exact retail word export for [8001B7C4,8001B85C); EXE and assembly verified. */
/* Body below is an UNVERIFIED draft, not an oracle match
 * claim; promotion requires tools/match_function.py MATCH. */
#include "psx_types.h"

/* m2c draft from main.s: NOT verified against retail. C89-gated only;
 * promotion requires an oracle MATCH (tools/match_function.py). Types
 * and signatures are whatever the decompiler guessed; they are not
 * evidence of the original declaration. */

void func_80019AF8();                                 /* static */
s32 func_80034B98();                                /* static */
extern u32 D_800AE708;
extern s32 D_800AE70C;
extern s32 *D_800AE7A4;
extern s32 D_800AE7B0;

s32 func_8001B7C4(s32 *arg0) {
    if ((func_80034B98() != 0) || ((D_800AE708 != 0) && (D_800AE708 != -1))) {
        return 0;
    }
    D_800AE708 = -1;
    D_800AE7A4 = arg0;
    D_800AE70C = 0;
    func_80019AF8(1);
    if (D_800AE7B0 == 1) {
        D_800AE70C = *arg0;
    }
    return D_800AE7B0;
}
