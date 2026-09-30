#ifndef MUSASHI_STARTUP_GTE_H
#define MUSASHI_STARTUP_GTE_H

#include <stdint.h>
#include "psx/inline_c.h"

/* Native lift of the register transfers at retail 80020354..8002040C
 * (repeated at 8002043C..800204F4), not a byte-matched PS1 function.
 *
 * Requires valid arrays of nine row-major signed halfwords, plus a loaded
 * GTE rotation in control registers 0..4. Processes columns 0,1,2 with
 * command 0x049E012 (SF=1, rotation matrix, IR vector, no translation,
 * LM=0), preserving the command's saturation and final GTE side effects.
 * Input/output may be identical or disjoint; other overlap is unspecified.
 * Does not load a matrix, read padding, reset GTE state, or translate.
 * Uses the shared GTE state: caller must serialize access. */
static inline void musashi_gte_rotate_columns(const int16_t input[9],
                                               int16_t output[9]) {
    int column;
    for (column = 0; column < 3; ++column) {
        MTC2((uint16_t)input[column], 9);
        MTC2((uint16_t)input[column + 3], 10);
        MTC2((uint16_t)input[column + 6], 11);
        gte_rtir();
        output[column] = (int16_t)MFC2_S(9);
        output[column + 3] = (int16_t)MFC2_S(10);
        output[column + 6] = (int16_t)MFC2_S(11);
    }
}

/* Native lift of 800204F8..80020544. Rotation control registers must
 * already be loaded. Inputs and output each contain three valid 32-bit
 * words. Translation loads control registers 5..7. Retail packs the low
 * halves of vector X/Y into data register 0 and loads vector Z into 1;
 * the GTE interprets the vector components as signed 16-bit values.
 * Output comes from full MAC1..3, NOT saturated IR1..3. Arrays may be
 * identical or disjoint; all input loads precede output stores.
 * Retains shared GTE state and requires serialized access like the helper
 * above. This is not a full-width vector transform or a PS1 match claim. */
static inline void musashi_gte_translate_vector(const int32_t translation[3],
                                                const int32_t vector[3],
                                                int32_t output[3]) {
    uint32_t xy;
    CTC2((uint32_t)translation[0], 5);
    CTC2((uint32_t)translation[1], 6);
    CTC2((uint32_t)translation[2], 7);
    xy = (uint32_t)(uint16_t)vector[0] |
         ((uint32_t)(uint16_t)vector[1] << 16);
    MTC2(xy, 0);
    MTC2((uint32_t)vector[2], 1);
    gte_rt();
    output[0] = MFC2_S(25);
    output[1] = MFC2_S(26);
    output[2] = MFC2_S(27);
}

#endif
