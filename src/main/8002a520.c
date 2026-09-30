/* Main-exec range [8002A520,8002A544) from the SLUS executable.
 * SHA256(span)=3096fb8201e58abc0d944b227d651574f4baf04bb49af181934297b2246f982b.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

/* HAND MODEL from m2c draft of func_8002A520 (main.s): NOT
 * verified against retail. C89-gated only; promotion requires an
 * oracle MATCH. Byte value at +0xC8 (lbu) passed to a u8 function Body is m2c's verbatim with types
 * filled in. */

struct data_8002A520 {
    u8 pad[0xC8];
    u8 unkC8;
};

void func_8002A544(u8);

void func_8002A520(struct data_8002A520 *arg0) {
    func_8002A544(arg0->unkC8);
}

