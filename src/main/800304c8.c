/* Main-exec range [800304C8,80030538) from the SLUS executable.
 * SHA256(span)=c4bfc73944f5a101c1aa33269e7964c3d6a783f19885e1e0731e3be0f8111ace.
 * Word export for the native seam; the body below is an
 * UNVERIFIED draft, not an oracle match claim. */
/* Verified byte-exact against retail by tools/match_function.py
 * (28/28 words at 0x800304C8). Types and signatures are whatever
 * reproduces the bytes; they are not evidence of the original
 * declaration. */
#include "psx_types.h"

s32 func_8003C4F0();                               /* static */
extern u8 D_8006AEF4;
extern u16 D_800A46CC;

s32 func_800304C8(void) {
    if (D_8006AEF4 & 1) {
        if (func_8003C4F0(0) != 0) {
            D_8006AEF4 &= 0xFE;
        }
    } else {
        { u16 *p = &D_800A46CC; *p = *p + 1; }
    }
    return 0;
}
