#ifndef MUSASHI_MIPS_DIV_H
#define MUSASHI_MIPS_DIV_H

#include <stdint.h>

typedef struct {
    uint32_t hi;
    uint32_t lo;
} MusashiMipsDivResult;

/*
 * Compute the PS1/MIPS signed DIV HI/LO result for two register bit patterns.
 * This is a register-result helper only: it does not model BREAK, exception
 * delivery, instruction latency, or surrounding CPU state.
 *
 * Keep this function header-local so formatter and source probes can use the
 * exact arithmetic without adding a link dependency to a production object.
 */
static inline int64_t musashi_mips_sign_extend_word(uint32_t bits) {
    const int64_t magnitude = (int64_t)(bits & UINT32_C(0x7fffffff));
    if ((bits & UINT32_C(0x80000000)) != 0) {
        return magnitude - INT64_C(0x80000000);
    }
    return magnitude;
}

static inline uint32_t musashi_mips_signed_word_bits(int64_t value) {
    if (value < 0) {
        /* The widened value makes INT32_MIN's magnitude representable. */
        return UINT32_C(0) - (uint32_t)(-value);
    }
    return (uint32_t)value;
}

static inline MusashiMipsDivResult musashi_mips_signed_div(uint32_t dividend_bits,
                                                            uint32_t divisor_bits) {
    const int64_t dividend = musashi_mips_sign_extend_word(dividend_bits);
    const int64_t divisor = musashi_mips_sign_extend_word(divisor_bits);
    MusashiMipsDivResult result;

    if (divisor == 0) {
        result.lo = dividend >= 0 ? UINT32_C(0xffffffff) : UINT32_C(1);
        result.hi = dividend_bits;
    } else if (dividend == -INT64_C(0x80000000) && divisor == -1) {
        result.lo = UINT32_C(0x80000000);
        result.hi = UINT32_C(0);
    } else {
        result.lo = musashi_mips_signed_word_bits(dividend / divisor);
        result.hi = musashi_mips_signed_word_bits(dividend % divisor);
    }
    return result;
}

#endif
