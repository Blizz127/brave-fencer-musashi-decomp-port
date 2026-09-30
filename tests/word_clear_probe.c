#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "musashi_boot_memory.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "word_clear_probe: failed at line %d\n", __LINE__); \
        return 1; \
    } \
} while (0)

int main(void) {
    MusashiBootMemory memory;
    static MusashiBootMemory expected;
    uint32_t value;
    unsigned offset;

    for (offset = 0; offset < MUSASHI_RAM_SIZE; ++offset)
        memory.bytes[offset] = 0xa5;
    expected = memory;
    CHECK(!musashi_boot_call_80042c64(&memory, 0x8006bafdu, 1));
    CHECK(!musashi_boot_call_80042c64(&memory, 0x8006bafeu, 1));
    CHECK(!musashi_boot_call_80042c64(&memory, 0x8006baffu, 1));
    CHECK(!musashi_boot_call_80042c64(&memory, 0x80000000u, 0x40000000u));
    CHECK(!musashi_boot_call_80042c64(&memory, 0x80000000u, UINT32_MAX));
    CHECK(!musashi_boot_call_80042c64(NULL, 0x8006bafcu, 1));
    CHECK(!musashi_boot_call_80042c64(&memory, 0x1f801074u, 1));
    CHECK(!musashi_boot_call_80042c64(&memory, 0x801ffff0u, 8));
    CHECK(musashi_boot_call_80042c64(&memory, 0x8006bafcu, 0));
    CHECK(musashi_boot_call_80042c64(&memory, 0x8006bafdu, 0));
    CHECK(memcmp(&memory, &expected, sizeof(memory)) == 0);
    CHECK(memory.bytes[0x6bafc] == 0xa5);
    CHECK(musashi_boot_call_80042c64(&memory, 0x8006bafcu, 0x41au));
    for (offset = 0; offset < 0x1068u; offset += 4) {
        CHECK(musashi_boot_read32(&memory, 0x8006bafcu + offset, &value));
        CHECK(value == 0);
    }
    CHECK(memory.bytes[0x6cb64] == 0xa5);
    memset(expected.bytes + 0x6bafc, 0, 0x1068);
    CHECK(memcmp(&memory, &expected, sizeof(memory)) == 0);
    CHECK(!musashi_boot_call_80042c64(&memory, 0x8006bafcu, 0x80000u));
    CHECK(memory.bytes[0x6cb64] == 0xa5);
    puts("word_clear_probe: 80042C64 word-count/shared-RAM contract PASS; native boot NOT_RUN");
    return 0;
}
