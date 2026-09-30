#include "musashi_psyq.h"

#include "psx/libgte.h"

/* rcos/rsin come from PsyCross's LIBGTE.C, compiled as C so the symbols are
 * unmangled and callable from here.
 *
 * The expected values are not assumed from documentation; they were read off
 * the built library. A quarter turn is 1024 units, so cosine falls to zero
 * exactly where sine reaches one.
 */
int musashi_psyq_probe(MusashiPsyqProbe *out) {
    if (out == 0) {
        return 0;
    }

    out->cos_zero = rcos(0);
    out->sin_zero = rsin(0);
    out->cos_quarter = rcos(MUSASHI_PSYQ_QUARTER_TURN);
    out->sin_quarter = rsin(MUSASHI_PSYQ_QUARTER_TURN);

    return out->cos_zero == MUSASHI_PSYQ_ONE
        && out->sin_zero == 0
        && out->cos_quarter == 0
        && out->sin_quarter == MUSASHI_PSYQ_ONE;
}
