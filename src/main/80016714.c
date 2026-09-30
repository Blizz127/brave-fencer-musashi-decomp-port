/* Main-exec range [80016714,800167B8) from the SLUS executable.
 * SHA256(span)=58996412878b336abbb2eb32b4501a76201ed20b3c60b76bdca4c3bc60d419ca.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

#ifdef MUSASHI_NATIVE_MEMSET_BINDING
#include <stdint.h>
#endif

/* Decompiled from retail and verified byte-exact with the identified
 * gcc-2.7.2-psx -O1 build. The separate word counter preserves the retail
 * use of v1 while the original byte count remains in a1 for the tail. */
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
void func_80016714_native(u8 *arg0, s32 arg1) {
#else
void func_80016714(s8 *arg0, s32 arg1) {
#endif
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
    uintptr_t var_address;
    s32 var_word_count;
#else
    u32 var_v1;
#endif

    if (arg1 < 4) {
        arg1--;
        if (arg1 != -1) {
            do {
                *arg0 = 0;
                arg1--;
                arg0++;
            } while (arg1 != -1);
        }
    } else {
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
        var_address = (uintptr_t)arg0;
#else
        var_v1 = (u32)arg0;
#endif
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
        if (var_address & 3) {
#else
        if (var_v1 & 3) {
#endif
            do {
                *arg0 = 0;
                arg0++;
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
                var_address++;
#else
                var_v1++;
#endif
                arg1--;
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
            } while (var_address & 3);
#else
            } while (var_v1 & 3);
#endif
        }
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
        var_word_count = (arg1 >> 2) - 1;
        if (var_word_count != -1) {
#else
        var_v1 = (arg1 >> 2) - 1;
        if (var_v1 != -1) {
#endif
            do {
                *(u32 *)arg0 = 0;
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
                var_word_count--;
#else
                var_v1--;
#endif
                arg0 += 4;
#ifdef MUSASHI_NATIVE_MEMSET_BINDING
            } while (var_word_count != -1);
#else
            } while (var_v1 != -1);
#endif
        }
        arg1 = (arg1 & 3) - 1;
        if (arg1 != -1) {
            do {
                *arg0 = 0;
                arg1--;
                arg0++;
            } while (arg1 != -1);
        }
    }
}
