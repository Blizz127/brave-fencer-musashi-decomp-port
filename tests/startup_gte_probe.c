/* Native integration probe for the command/register boundary observed in
 * retail 80020248..80020598. Synthetic vectors, NOT retail runtime parity. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psx/inline_c.h"
#include "../pc_port/compat/startup_gte.h"

static uint32_t pack(int x, int y) {
    return (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16);
}

static int check3(int first, int x, int y, int z) {
    int actual[3] = { MFC2_S(first), MFC2_S(first + 1), MFC2_S(first + 2) };
    if (actual[0] == x && actual[1] == y && actual[2] == z) return 1;
    fprintf(stderr, "registers %d..%d: expected (%d,%d,%d), got (%d,%d,%d)\n",
            first, first + 2, x, y, z, actual[0], actual[1], actual[2]);
    return 0;
}

int main(int argc, char **argv) {
    int wrong_v0 = argc == 2 && strcmp(argv[1], "--wrong-v0") == 0;
    int i;
    if (argc > 1 && !wrong_v0) return 2;
    for (i = 0; i < 32; ++i) { CTC2(0, i); MTC2(0, i); }

    /* Rotation [[0,-1,0],[1,0,0],[0,0,1]] in Q12. Explicit five-word
     * loads avoid reading a 20-byte matrix from an 18-byte C object. */
    CTC2(pack(0, -4096), 0);
    CTC2(pack(0, 4096), 1);
    CTC2(0, 2); CTC2(0, 3); CTC2(4096, 4);
    MTC2((uint16_t)123, 9);
    MTC2((uint16_t)-234, 10);
    MTC2((uint16_t)345, 11);
    gte_rtir(); /* retail command 4A49E012 */
    if (!check3(9, 234, 123, 345)) return 1;

    {
        const int16_t input[9] = {1, 2, 3, -4, -5, -6, 7, 8, 9};
        const int16_t expected[9] = {4, 5, 6, 1, 2, 3, 7, 8, 9};
        int16_t guarded[11];
        for (i = 0; i < 11; ++i) guarded[i] = 0x1234;
        musashi_gte_rotate_columns(input, guarded + 1);
        if (memcmp(guarded + 1, expected, sizeof(expected)) != 0 ||
            guarded[0] != 0x1234 || guarded[10] != 0x1234) {
            fputs("rotation columns or output extent differ\n", stderr);
            return 1;
        }
        if (!check3(9, 6, 3, 9) || !check3(25, 6, 3, 9)) return 1;
        /* Retail interleaves per-column loads/stores; exact in-place use
         * must not transpose or consume an earlier column's output. */
        memcpy(guarded + 1, input, sizeof(input));
        musashi_gte_rotate_columns(guarded + 1, guarded + 1);
        if (memcmp(guarded + 1, expected, sizeof(expected)) != 0) return 1;
    }

    {
        const int16_t input[9] = {
            20000, -20000, -20000, -20000, 20000, 20000, 20000, 20000, -20000
        };
        const int16_t expected[9] = {
            32767, -32768, -32768, -32768, 32767, 32767, 32767, 32767, -32768
        };
        int16_t output[9];
        CTC2(8192, 0); CTC2(0, 1); CTC2(8192, 2);
        CTC2(0, 3); CTC2(8192, 4);
        musashi_gte_rotate_columns(input, output);
        if (memcmp(output, expected, sizeof(expected)) != 0) {
            fputs("rotation must preserve signed IR saturation\n", stderr);
            return 1;
        }
        if (!check3(25, -40000, 40000, -40000)) return 1;
    }

    CTC2(4096, 0); CTC2(0, 1); CTC2(4096, 2);
    CTC2(0, 3); CTC2(4096, 4);
    CTC2_S(10, 5); CTC2_S(20, 6); CTC2_S(-30, 7);
    /* Poison V0 so writing SXY0 (12) instead of VXY0 (0) cannot pass. */
    MTC2(pack(7, 9), 0);
    MTC2(pack(123, -234), wrong_v0 ? 12 : 0);
    MTC2(345, 1);
    gte_rt(); /* retail command 4A480012 */
    if (!check3(25, 133, -214, 315)) return 1;
    {
        const int32_t translation[3] = {40000, -40000, 100000};
        int32_t vector[3] = {0x1234007B, 0x7FFFFF16, 0x23450159};
        const int32_t expected[3] = {40123, -40234, 100345};
        int32_t guarded[5] = {123456, 0, 0, 0, -123456};
        musashi_gte_translate_vector(translation, vector, guarded + 1);
        if (memcmp(guarded + 1, expected, sizeof(expected)) != 0 ||
            guarded[0] != 123456 || guarded[4] != -123456) {
            fputs("translation must pack low halves and store full MAC words\n", stderr);
            return 1;
        }
        if (!check3(9, 32767, -32768, 32767)) return 1;
        musashi_gte_translate_vector(translation, vector, vector);
        if (memcmp(vector, expected, sizeof(expected)) != 0) return 1;
    }
    {
        const int16_t identity[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
        const int16_t expected_rotation[9] = {0, -4096, 0, 4096, 0, 0, 0, 0, 4096};
        const int32_t translation[3] = {10, 20, -30};
        const int32_t vector[3] = {123, -234, 345};
        const int32_t expected_translation[3] = {244, 143, 315};
        int16_t rotation[9];
        int32_t position[3];
        CTC2(pack(0, -4096), 0); CTC2(pack(0, 4096), 1);
        CTC2(0, 2); CTC2(0, 3); CTC2(4096, 4);
        musashi_gte_rotate_columns(identity, rotation);
        /* No GTE reset or rotation reload between the two retail fragments. */
        musashi_gte_translate_vector(translation, vector, position);
        if (memcmp(rotation, expected_rotation, sizeof(rotation)) != 0 ||
            memcmp(position, expected_translation, sizeof(position)) != 0) {
            fputs("rotation/translation sequence lost shared control state\n", stderr);
            return 1;
        }
    }
    puts("STARTUP_GTE_PROBE_PASS synthetic register-transfer cases only");
    return 0;
}
