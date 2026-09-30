/* Main-exec range [8002A04C,8002A088) from the SLUS executable.
 * SHA256(span)=9449f527502f2333a36fcac22cde5621468f6080f5a9778e3b14b38d6cc02a19.
 * Word export for the native seam; the body below is an
 * UNVERIFIED draft, not an oracle match claim. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 func_8002A088(s32);

/* The `== 0` test with a convergent `goto done` tail lays out as
 * `bnez`-to-body with the zero assignment sunk into the `j`-to-shared-
 * epilogue delay slot. Structured early-`return` form inverts the
 * branch and costs a word. */
s32 func_8002A04C(u8 *arg0) {
    u8 *v0 = *(u8 **)(arg0 + 0x78);
    s32 rc;

    if (v0 == 0) {
        rc = 0;
        goto done;
    }
    rc = func_8002A088(*(s16 *)(v0 + 0x2E));
done:
    return rc;
}
