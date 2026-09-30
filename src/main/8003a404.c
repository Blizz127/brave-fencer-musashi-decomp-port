/* Main-exec range [8003A404,8003A424) from the SLUS executable.
 * SHA256(span)=182e40628f6c293455052e69969c3115e22065d88cc87f3bb9da960295715886.
 * Word export for the native seam; the body below is an
 * UNVERIFIED draft, not an oracle match claim. */
/* Body below is an UNVERIFIED draft, not an oracle match
 * claim; promotion requires tools/match_function.py MATCH. */
#include "psx_types.h"

/* m2c draft from main.s: NOT verified against retail. C89-gated only;
 * promotion requires an oracle MATCH (tools/match_function.py). Types
 * and signatures are whatever the decompiler guessed; they are not
 * evidence of the original declaration. */

s16 func_8003A404(u32 arg0) {
    s32 hi = arg0 >> 8;
    s32 lo = (arg0 & 0xFF) << 8;
    return (s16) ((hi & 0xFF) + lo);
}
