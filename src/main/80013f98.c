/* Main-exec helper [80013F98,80013FBC): three strided u16 copies.
 * SHA256(span)=547b1f81ba7674adcf846adb36569b84fc274f6f865560b683f72a2cbd6702e6. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

void func_80013F98(u16 *arg0, u16 *arg1) {
    arg1[0] = arg0[0];
    arg1[1] = arg0[3];
    arg1[2] = arg0[6];
}
