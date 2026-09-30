#include "musashi_mips_div.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t musashi_mips_apply_sign(uint32_t magnitude, int negative) {
    return negative ? UINT32_C(0) - magnitude : magnitude;
}

static void musashi_mips_unsigned_long_divide(uint32_t dividend_magnitude,
                                              uint32_t divisor_magnitude,
                                              uint32_t *quotient,
                                              uint32_t *remainder) {
    uint64_t remainder_work = 0;
    uint32_t quotient_work = 0;

    for (unsigned int step = 32; step != 0; --step) {
        const unsigned int shift = step - 1;
        remainder_work = (remainder_work << 1) |
                         (uint64_t)((dividend_magnitude >> shift) & UINT32_C(1));
        if (remainder_work >= (uint64_t)divisor_magnitude) {
            remainder_work -= (uint64_t)divisor_magnitude;
            quotient_work |= UINT32_C(1) << shift;
        }
    }
    *quotient = quotient_work;
    *remainder = (uint32_t)remainder_work;
}

/* Independent oracle: raw bits, unsigned magnitudes, and bitwise long division. */
static MusashiMipsDivResult musashi_mips_oracle(uint32_t dividend_bits,
                                                uint32_t divisor_bits) {
    MusashiMipsDivResult expected;
    const int dividend_negative = (dividend_bits & UINT32_C(0x80000000)) != 0;
    const int divisor_negative = (divisor_bits & UINT32_C(0x80000000)) != 0;

    if (divisor_bits == UINT32_C(0)) {
        expected.lo = dividend_negative ? UINT32_C(1) : UINT32_C(0xffffffff);
        expected.hi = dividend_bits;
    } else {
        uint32_t quotient_magnitude;
        uint32_t remainder_magnitude;
        const uint32_t dividend_magnitude = dividend_negative ?
            UINT32_C(0) - dividend_bits : dividend_bits;
        const uint32_t divisor_magnitude = divisor_negative ?
            UINT32_C(0) - divisor_bits : divisor_bits;

        musashi_mips_unsigned_long_divide(dividend_magnitude, divisor_magnitude,
                                          &quotient_magnitude, &remainder_magnitude);
        expected.lo = musashi_mips_apply_sign(quotient_magnitude,
                                               dividend_negative != divisor_negative);
        expected.hi = musashi_mips_apply_sign(remainder_magnitude, dividend_negative);
    }
    return expected;
}

static void musashi_mips_check_case(uint32_t dividend, uint32_t divisor,
                                    const char *label) {
    const MusashiMipsDivResult actual = musashi_mips_signed_div(dividend, divisor);
    const MusashiMipsDivResult expected = musashi_mips_oracle(dividend, divisor);
    if (actual.hi != expected.hi || actual.lo != expected.lo) {
        fprintf(stderr,
                "%s: dividend=%08" PRIx32 " divisor=%08" PRIx32
                " got HI=%08" PRIx32 " LO=%08" PRIx32
                " want HI=%08" PRIx32 " LO=%08" PRIx32 "\n",
                label, dividend, divisor, actual.hi, actual.lo,
                expected.hi, expected.lo);
        exit(EXIT_FAILURE);
    }
}

static uint32_t musashi_mips_next_random(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

int main(void) {
    static const struct {
        uint32_t dividend;
        uint32_t divisor;
        const char *label;
    } cases[] = {
        {UINT32_C(0), UINT32_C(1), "zero"},
        {UINT32_C(1), UINT32_C(1), "positive-exact"},
        {UINT32_C(0xffffffff), UINT32_C(1), "negative-sign"},
        {UINT32_C(0x12345678), UINT32_C(0x00000100), "positive-remainder"},
        {UINT32_C(0xedcba988), UINT32_C(0xffffff00), "negative-remainder"},
        {UINT32_C(0x7fffffff), UINT32_C(0x80000000), "max-over-min"},
        {UINT32_C(0x80000000), UINT32_C(1), "int-min-positive-one"},
        {UINT32_C(0x80000000), UINT32_C(0xffffffff), "int-min-div-minus-one"},
        {UINT32_C(0x80000000), UINT32_C(0x80000000), "int-min-div-itself"},
        {UINT32_C(0x7fffffff), UINT32_C(0), "positive-divzero"},
        {UINT32_C(0x80000000), UINT32_C(0), "int-min-divzero"},
        {UINT32_C(0xffffffff), UINT32_C(0), "negative-divzero"},
    };
    uint32_t random_state = UINT32_C(0x6d2b79f5);

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        musashi_mips_check_case(cases[index].dividend, cases[index].divisor,
                                cases[index].label);
    }
    for (unsigned int index = 0; index < 100000; ++index) {
        const uint32_t dividend = musashi_mips_next_random(&random_state);
        const uint32_t divisor = musashi_mips_next_random(&random_state);
        musashi_mips_check_case(dividend, divisor, "random");
    }

    puts("MIPS_SIGNED_DIV_PASS directed=12 random=100000 oracle=bitlongdivision");
    return EXIT_SUCCESS;
}
