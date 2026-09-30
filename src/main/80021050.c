/* Main-exec range [80021050,8002109C) from the SLUS executable.
 * SHA256(span)=02424107ded113140c49bd7199de9ff237257394a980297e8c94905b9ad757ad.
 * Word export for the native seam; the C body below keeps its
 * own oracle match claim. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80028620();
typedef struct { u8 b[16]; } S;
extern S D_800A5E88[];
void func_80021050(void) {
    func_80028620(0, &D_800A5E88[0]);
    func_80028620(1, &D_800A5E88[1]);
    func_80028620(2, &D_800A5E88[2]);
}
