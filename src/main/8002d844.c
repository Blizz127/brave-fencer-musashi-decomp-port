/* Main-exec range [8002D844,8002D858) from the SLUS executable.
 * SHA256(span)=998026cf26d5cffbd9f9fbb11895c3f39935c5da615f18a82b4b43d0eb1585b9.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled and verified byte-exact against the registered retail EXE. */
extern s32 D_800760E0;

s32 func_8002D844(s32 value) {
    D_800760E0 = value;
    return value;
}
