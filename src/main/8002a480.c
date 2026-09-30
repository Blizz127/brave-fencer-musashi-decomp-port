/* Main-exec range [8002A480,8002A4B8) from the SLUS executable.
 * SHA256(span)=da9e72e94af95182f859b4627351b0100cfd5bae75cc3d6d3cb2de5a346c7a74.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 D_80078EEC;
extern s32 D_80078EF0;

void func_8002A480(void) {
    s32 var_v1;

    var_v1 = D_80078EEC + 1;
    if (var_v1 >= 0x1F) {
        var_v1 = 0x1E;
    }
    D_80078EEC = var_v1;
    D_80078EF0 = 0;
}
