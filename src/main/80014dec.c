/* Main-exec range [80014DEC,80014E24) from the SLUS executable.
 * SHA256(span)=9f5c886b45c1fda2c57d33e954db97d4acac5723fb4229f50f5b530f56095da8.
 * Word export for the native seam; the body below is an
 * UNVERIFIED draft, not an oracle match claim. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u8 D_80078D98[];

/* Staged so each step pins its codegen: the `u8` parameters emit the
 * two `andi` masks just before their first uses (early for the long
 * index chain, late for the single offset add), the explicit index
 * temp carries the `* 76` strength-reduced chain, and the address
 * temp forces base materialization instead of folding. */
u8 func_80014DEC(u8 arg0, u8 arg1) {
    s32 idx = arg0 * 76;
    u8 *p = &D_80078D98[idx];

    return *(p + arg1 + 0x32);
}
