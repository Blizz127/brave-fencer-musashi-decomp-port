#include "musashi_bios_events.h"
#include "musashi_boot_memory.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define CALLBACK_FIRST 0x800617ccu
#define CALLBACK_STRIDE 0x14u
#define CALLBACK_COUNT 8u
#define FLAG_FIRST 0x80078c5cu

static int execute_callback(void *userdata, uint32_t target) {
    return musashi_boot_execute_bios_event((MusashiBootMemory *)userdata,
                                           target);
}

static void seed_word(MusashiBootMemory *memory, uint32_t address,
                      uint32_t value) {
    assert(musashi_boot_write32(memory, address, value));
}

static void test_all_exact_targets(void) {
    MusashiBootMemory memory;
    MusashiBootMemory expected;
    MusashiBootMemory repeated;
    MusashiBiosEvents events;
    unsigned index;

    memset(&memory, 0, sizeof(memory));
    musashi_bios_events_init(&events);
    for (index = 0; index < CALLBACK_COUNT; ++index) {
        events.records[index].class_word = 7;
        events.records[index].status = 0x2000;
        events.records[index].spec = 9;
        events.records[index].mode = 0x1000;
        events.records[index].callback = CALLBACK_FIRST + CALLBACK_STRIDE * index;
        seed_word(&memory, FLAG_FIRST + 4u * index, 7);
    }
    expected = memory;
    for (index = 0; index < CALLBACK_COUNT; ++index)
        seed_word(&expected, FLAG_FIRST + 4u * index, 1);
    assert(musashi_bios_events_deliver(&events, 7, 9,
                                       execute_callback, &memory));
    assert(memcmp(&memory, &expected, sizeof(memory)) == 0);
    for (index = 0; index < CALLBACK_COUNT; ++index) {
        uint32_t value = 0;
        assert(musashi_boot_read32(&memory, FLAG_FIRST + 4u * index, &value));
        assert(value == 1);
        assert(events.records[index].status == 0x2000);
    }
    repeated = memory;
    assert(musashi_bios_events_deliver(&events, 7, 9,
                                       execute_callback, &memory));
    assert(memcmp(&memory, &repeated, sizeof(memory)) == 0);
    for (index = 0; index < CALLBACK_COUNT; ++index) {
        uint32_t value = 0;
        assert(musashi_boot_read32(&memory, FLAG_FIRST + 4u * index, &value));
        assert(value == 1);
        assert(events.records[index].status == 0x2000);
    }
}

static void assert_refused_without_write(uint32_t target) {
    MusashiBootMemory memory;
    MusashiBootMemory before;
    MusashiBiosEvents events;

    memset(&memory, 0xa5, sizeof(memory));
    before = memory;
    musashi_bios_events_init(&events);
    events.records[0].class_word = 7;
    events.records[0].status = 0x2000;
    events.records[0].spec = 9;
    events.records[0].mode = 0x1000;
    events.records[0].callback = target;
    assert(!musashi_bios_events_deliver(&events, 7, 9,
                                        execute_callback, &memory));
    assert(memcmp(&memory, &before, sizeof(memory)) == 0);
    assert(events.records[0].status == 0x2000);
}

static void test_refused_boundaries(void) {
    unsigned index;
    unsigned offset;

    for (index = 0; index < CALLBACK_COUNT; ++index) {
        uint32_t start = CALLBACK_FIRST + CALLBACK_STRIDE * index;
        for (offset = 1; offset < CALLBACK_STRIDE; ++offset)
            assert_refused_without_write(start + offset);
    }
    assert_refused_without_write(CALLBACK_FIRST - 4u);
    assert_refused_without_write(CALLBACK_FIRST + CALLBACK_STRIDE * CALLBACK_COUNT);
}

static void test_direct_refusals(void) {
    MusashiBootMemory memory;
    MusashiBootMemory before;

    memset(&memory, 0xa5, sizeof(memory));
    before = memory;
    assert(!musashi_boot_execute_bios_event(NULL, CALLBACK_FIRST));
    assert(!musashi_boot_execute_bios_event(&memory, 0));
    assert(memcmp(&memory, &before, sizeof(memory)) == 0);
    assert(!musashi_boot_execute_bios_event(&memory, UINT32_MAX));
    assert(memcmp(&memory, &before, sizeof(memory)) == 0);
}

int main(void) {
    test_all_exact_targets();
    test_refused_boundaries();
    test_direct_refusals();
    return 0;
}
