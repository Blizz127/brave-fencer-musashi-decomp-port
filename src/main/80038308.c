/* Exact retail word export for [80038308,8003834C); EXE and assembly verified. */
/* Body below is an UNVERIFIED draft, not an oracle match
 * claim; promotion requires tools/match_function.py MATCH. */
#include "psx_types.h"

/* m2c draft from main.s: NOT verified against retail. C89-gated only;
 * promotion requires an oracle MATCH (tools/match_function.py). Types
 * and signatures are whatever the decompiler guessed; they are not
 * evidence of the original declaration. */

void func_80038908(void *);                            /* static */
extern u8 D_800B9CD8[];
extern u8 D_800B9ED2[];

void func_80038308(s32 arg0) {
    s32 temp_v0;

    temp_v0 = arg0 * 0x1FC;
    D_800B9ED2[temp_v0] = 1;
    func_80038908(&D_800B9CD8[temp_v0]);
}
