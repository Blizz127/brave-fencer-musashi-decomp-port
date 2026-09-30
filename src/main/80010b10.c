/* Exact retail word export for [80010B10,80010B2C); EXE and assembly verified. */
/* Verified byte-exact against retail by tools/match_function.py
 * (7/7 words at 0x80010B10). Types and signatures are whatever
 * reproduces the bytes; they are not evidence of the original
 * declaration. */
#include "psx_types.h"

extern s32 D_80074778;

s32 func_80010B10(void) {
    return D_80074778;
}
