/* Main-exec range [800388E8,80038908) from the SLUS executable.
 * SHA256(span)=d09c72a984a5846906cb7ea12e748244882289db0912ad4caa13844e3d3a971d.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

/* HAND MODEL from m2c draft of func_800388E8 (main.s): NOT
 * verified against retail. C89-gated only; promotion requires an
 * oracle MATCH. Signed halfword at +0x10 (lh); mult/sll2/sra16 match. Body is m2c's verbatim with the
 * member typed. */

struct data_800388E8 {
    u8 pad[0x10];
    s16 unk10; };

s32 func_800388E8(struct data_800388E8 *arg0, s16 arg1) {
    return (s32) (arg1 * arg0->unk10 * 4) >> 0x10;
}
