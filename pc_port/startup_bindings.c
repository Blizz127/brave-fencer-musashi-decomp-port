#include "musashi_boot_memory.h"
#include <string.h>

#include "musashi_psyq_hle.h"

/* PsyQ services (libetc/libc2) are port-owned HLE, never Sony code. */
extern void func_80016714_native(uint8_t *destination, int32_t count);
extern void func_80029044_native(uint8_t *destination);
extern void func_8002906C_native(uint8_t *destination);

int musashi_boot_search_bytes(MusashiBootMemory *memory, uint32_t address,
                              int32_t target, int32_t count, uint32_t *result) {
    uint8_t *source = NULL;
    uint8_t *found;
    if (!memory || !result)
        return 0;
    if (address && count > 0) {
        source = musashi_boot_ram_span(memory, address, (size_t)count);
        if (!source)
            return 0;
    }
    found = musashi_hle_find_byte(source, target, count);
    *result = found ? address + (uint32_t)(found - source) : 0;
    return 1;
}

typedef struct MusashiNativeConsoleContext {
    MusashiConsoleSink sink;
    void *userdata;
} MusashiNativeConsoleContext;

void musashi_native_console_sink(void *userdata, const uint8_t *bytes,
                                 int32_t length) {
    MusashiNativeConsoleContext *context = userdata;
    context->sink(context->userdata, bytes, length);
}

int musashi_boot_console_char(MusashiBootMemory *memory, int32_t value,
                              MusashiConsoleSink sink, void *userdata) {
    uint8_t *classification;
    uint32_t column_bits;
    int32_t column;
    MusashiNativeConsoleContext context;
    if (!memory || !sink)
        return 0;
    classification = musashi_boot_ram_span(memory, 0x800728a9u, 0x100u);
    if (!classification || !musashi_boot_read32(memory, 0x80078978u,
                                                &column_bits))
        return 0;
    memcpy(&column, &column_bits, sizeof(column));
    context.sink = sink;
    context.userdata = userdata;
    musashi_hle_console_putc(value, classification, &column, musashi_native_console_sink, &context);
    memcpy(&column_bits, &column, sizeof(column_bits));
    return musashi_boot_write32(memory, 0x80078978u, column_bits);
}

static uint8_t *terminated_console_string(MusashiBootMemory *memory,
                                          uint32_t address) {
    uint8_t *string;
    uint32_t offset;
    string = musashi_boot_ram_span(memory, address, 1);
    if (!string)
        return NULL;
    offset = address - 0x80000000u;
    while (offset < MUSASHI_RAM_SIZE) {
        if (memory->bytes[offset] == 0)
            return string;
        offset++;
    }
    return NULL;
}

int musashi_boot_console_string(MusashiBootMemory *memory, uint32_t address,
                                MusashiConsoleSink sink, void *userdata) {
    uint8_t *source;
    uint8_t *fallback;
    uint8_t *classification;
    uint32_t column_bits;
    int32_t column;
    MusashiNativeConsoleContext context;
    if (!memory || !sink)
        return 0;
    source = terminated_console_string(memory, address);
    fallback = terminated_console_string(memory, 0x80074498u);
    classification = musashi_boot_ram_span(memory, 0x800728a9u, 0x100u);
    if (!source || !fallback || !classification ||
        !musashi_boot_read32(memory, 0x80078978u, &column_bits))
        return 0;
    memcpy(&column, &column_bits, sizeof(column));
    context.sink = sink;
    context.userdata = userdata;
    musashi_hle_console_puts(source, fallback, classification, &column,
                             musashi_native_console_sink, &context);
    memcpy(&column_bits, &column, sizeof(column_bits));
    return musashi_boot_write32(memory, 0x80078978u, column_bits);
}

int musashi_boot_call_8005c29c(MusashiBootMemory *memory, uint32_t address,
                              int32_t value, uint32_t count) {
    uint8_t *destination = musashi_boot_ram_span(memory, address, count ? count : 1);
    if (!destination)
        return 0;
    musashi_hle_fill_bytes(destination, value, count);
    return 1;
}

int musashi_boot_call_80016714(MusashiBootMemory *memory, uint32_t address,
                               uint32_t count) {
    uint8_t *destination;
    if (!memory || count > INT32_MAX)
        return 0;
    destination = musashi_boot_ram_span(memory, address, count ? count : 1);
    if (!destination)
        return 0;
    func_80016714_native(destination, (int32_t)count);
    return 1;
}

int musashi_boot_call_8001903c(MusashiBootMemory *memory) {
    return musashi_boot_call_80016714(memory, 0x800747c0u, 8u);
}

int musashi_boot_call_80029044(MusashiBootMemory *memory) {
    uint8_t *destination = musashi_boot_ram_span(memory, 0x800ae648u, 0x40u);
    if (!destination)
        return 0;
    func_80029044_native(destination);
    return 1;
}

int musashi_boot_call_8002906c(MusashiBootMemory *memory) {
    uint8_t *destination = musashi_boot_ram_span(memory, 0x800ba1b8u, 0x100u);
    if (!destination)
        return 0;
    func_8002906C_native(destination);
    return 1;
}

int musashi_boot_call_80043060(MusashiBootMemory *memory, int32_t argument,
                              int32_t *previous) {
    uint32_t bits;
    int32_t word, result;
    if (!previous || !musashi_boot_read32(memory, 0x8006cbe8u, &bits))
        return 0;
    /* Preserve all bit patterns without implementation-defined unsigned-to-
     * signed conversion. No pointer into byte-backed RAM is dereferenced as
     * an int, and no persistent host global can diverge from the RAM image. */
    memcpy(&word, &bits, sizeof(word));
    result = musashi_hle_exchange_word(&word, argument);
    memcpy(&bits, &word, sizeof(bits));
    if (!musashi_boot_write32(memory, 0x8006cbe8u, bits))
        return 0;
    *previous = result;
    return 1;
}

MusashiPrefixStatus musashi_boot_run_prefix(MusashiBootMemory *memory,
                                           MusashiPrefixStop *stop) {
    uint32_t guard;
    int32_t previous;
    if (!stop || !musashi_boot_read32(memory, 0x80062998u, &guard))
        return MUSASHI_PREFIX_INVALID_INPUT;

    /* Platform CRT boundary 800100A0: the pinned executable encodes a literal
     * zero constructor count at 800100D0/D4. Preserve an existing nonzero guard.
     * This adapter is not a pure-C match for that handwritten assembly. */
    if (guard == 0)
        musashi_boot_write32(memory, 0x80062998u, 1);

    /* Call at 800101AC, then store at 800101BC. Use the decomp-owned exchange
     * body, not an equivalent assignment implemented here. All addresses are
     * fixed valid RAM spans after the initial pointer check. */
    musashi_boot_call_80043060(memory, 0, &previous);
    musashi_boot_write32(memory, 0x80074778u, 0x3e0u);

    /* No call to 800141F0 and no success-returning replacement for its missing
     * dependencies. This bounded diagnostic does not replace the game main. */
    stop->call_address = 0x800101c0u;
    stop->target_address = 0x800141f0u;
    return MUSASHI_PREFIX_UNSUPPORTED_CALL;
}
