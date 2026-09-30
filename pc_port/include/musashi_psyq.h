#ifndef MUSASHI_PSYQ_H
#define MUSASHI_PSYQ_H

/* Proof that decomp-owned C reaches the Psy-Q layer PsyCross provides.
 *
 * Only pure fixed-point maths is used, so this runs headless: opening a window
 * would drag in SDL, a display and a GPU, none of which a build check should
 * require.
 */

typedef struct MusashiPsyqProbe {
    int cos_zero;      /* rcos(0)    */
    int sin_zero;      /* rsin(0)    */
    int cos_quarter;   /* rcos(1024) */
    int sin_quarter;   /* rsin(1024) */
} MusashiPsyqProbe;

/* PS1 fixed point: 4096 represents 1.0, and 4096 units span a full turn. */
#define MUSASHI_PSYQ_ONE 4096
#define MUSASHI_PSYQ_QUARTER_TURN 1024

/* Fills `out` and returns non-zero when every value matches the convention. */
int musashi_psyq_probe(MusashiPsyqProbe *out);

#endif
