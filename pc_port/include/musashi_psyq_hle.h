#ifndef MUSASHI_PSYQ_HLE_H
#define MUSASHI_PSYQ_HLE_H

/* Our own implementations of the small PsyQ runtime services the port used
 * to take from decompiled Sony code (docs/PC-PORT.md, "PsyQ exclusion").
 * Written from the documented C-library/libetc behaviour, not from PsyQ
 * code. They operate on host views of guest RAM that the caller bounded.
 * Header-only, so every translation unit that needs one links nothing new. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* libetc clear of word_count 32-bit words. */
static inline void musashi_hle_clear_words(uint8_t *destination, uint32_t word_count) {
    if (word_count) memset(destination, 0, (size_t)word_count * 4u);
}

/* libc memset: count bytes of (uint8_t)value. */
static inline void musashi_hle_fill_bytes(uint8_t *destination, int32_t value, uint32_t count) {
    if (count) memset(destination, (uint8_t)value, count);
}

/* libc memchr with a signed count: first byte equal to (uint8_t)target in
 * source[0..count), or NULL (also for a NULL source or count <= 0). */
static inline uint8_t *musashi_hle_find_byte(uint8_t *source, int32_t target, int32_t count) {
    if (!source || count <= 0) return NULL;
    return memchr(source, (uint8_t)target, (size_t)count);
}

/* Exchange: store value, return the previous word. */
static inline int32_t musashi_hle_exchange_word(int32_t *word, int32_t value) {
    int32_t previous = *word;
    *word = value;
    return previous;
}

/* Console TTY output as the PsyQ libc2 putchar/puts pair behaves: '\n' is
 * written as "\r\n" and resets the column; '\t' pads with spaces to the
 * next multiple of 8; any other byte is written, advancing the column when
 * the game's ctype table marks it printable (classification & 0x97). */
typedef void (*MusashiHleConsoleWrite)(void *userdata, const uint8_t *bytes, int32_t length);

static inline void musashi_hle_console_emit(uint8_t byte, const uint8_t *classification,
                                            int32_t *column, MusashiHleConsoleWrite write,
                                            void *userdata) {
    if (classification[byte] & 0x97u) ++*column;
    write(userdata, &byte, 1);
}

static inline void musashi_hle_console_putc(int32_t value, const uint8_t *classification,
                                            int32_t *column, MusashiHleConsoleWrite write,
                                            void *userdata) {
    uint8_t byte = (uint8_t)value;
    if (byte == '\n') {
        musashi_hle_console_emit('\r', classification, column, write, userdata);
        *column = 0;
        write(userdata, &byte, 1);
    } else if (byte == '\t') {
        do {
            musashi_hle_console_emit(' ', classification, column, write, userdata);
        } while (*column & 7);
    } else {
        musashi_hle_console_emit(byte, classification, column, write, userdata);
    }
}

/* puts without a trailing newline; a NULL source prints fallback. */
static inline void musashi_hle_console_puts(const uint8_t *source, const uint8_t *fallback,
                                            const uint8_t *classification, int32_t *column,
                                            MusashiHleConsoleWrite write, void *userdata) {
    const uint8_t *p = source ? source : fallback;
    while (*p) musashi_hle_console_putc(*p++, classification, column, write, userdata);
}

#endif
