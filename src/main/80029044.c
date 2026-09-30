/* Main-exec range [80029044,8002906C) from the SLUS executable.
 * SHA256(span)=fb5ceee65c72512838f6f97cfd14a4c2ddc73b8ba798731f020c8373ca0a5768.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

extern u8 D_800AE648[];

/* Recovered from retail and verified byte-exact with gcc-2.7.2-psx -O2. */
#ifdef MUSASHI_NATIVE_TABLE_CLEAR_BINDING
void func_80029044_native(u8 *destination) {
    u32 var_v1 = 0;
    do {
        destination[var_v1] = 0;
        var_v1 += 1;
    } while (var_v1 < 0x40U);
}
#else
void func_80029044(void) {
    u32 var_v1 = 0;
    do {
        D_800AE648[var_v1] = 0;
        var_v1 += 1;
    } while (var_v1 < 0x40U);
}
#endif
