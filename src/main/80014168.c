/* Main-exec range [80014168,800141AC) from the SLUS executable.
 * SHA256(span)=e057702a93e921a02421e5ec73b0279ffe91b95496c440c5d8ca7d111918d253.
 * Word export for the native seam; the body below is an
 * UNVERIFIED draft, not an oracle match claim. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

typedef struct { s32 x[2]; } __attribute__((packed)) T8_80014168;
extern void func_80048FBC(void *, void *, void *);

/* Unaligned 8-byte struct copy (`packed` forces `lwl`/`lwr` +
 * `swl`/`swr` instead of plain `lw`/`sw`). The source pointer is
 * staged into a copy because it is also passed through as the third
 * call argument — that copy is what keeps it alive past the `a1`
 * clobber for the buffer address. m2c dropped the third argument
 * and choked on the unaligned pair. */
void func_80014168(void *arg0, u8 *arg1) {
    u8 *s = arg1;
    T8_80014168 buf = *(T8_80014168 *)s;

    func_80048FBC(arg0, &buf, s);
}
