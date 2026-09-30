/* Main-exec range [8002906C,80029094) from the SLUS executable.
 * SHA256(span)=9a61d38d49181c476117b5e74703072cf540a84af8da6eb75d917e603876b6ef.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

extern u8 D_800BA1B8[];

/* Recovered from retail and verified byte-exact with gcc-2.7.2-psx -O2. */
#ifdef MUSASHI_NATIVE_TABLE_CLEAR_BINDING
void func_8002906C_native(u8 *destination) {
    u32 var_v1 = 0;
    do {
        destination[var_v1] = 0;
        var_v1 += 1;
    } while (var_v1 < 0x100U);
}
#else
void func_8002906C(void) {
    u32 var_v1 = 0;
    do {
        D_800BA1B8[var_v1] = 0;
        var_v1 += 1;
    } while (var_v1 < 0x100U);
}
#endif
