/* Entry-data diagnostic only. The return address below is a synthetic input;
 * this program neither executes retail startup nor claims a native game boot. */
#include "musashi_boot_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "boot_data_probe: failed at line %d\n", __LINE__); \
    return 1; \
} } while (0)

static uint8_t console_bytes[16];
static int32_t console_length;

static void record_console(void *userdata, const uint8_t *bytes, int32_t length) {
    (void)userdata;
    if (length != 1 || console_length >= (int32_t)sizeof(console_bytes))
        abort();
    console_bytes[console_length++] = bytes[0];
}

int main(int argc, char **argv) {
    static MusashiBootMemory memory;
    static uint8_t image[0x65001];
    MusashiEntryState state = {11, 22, 33};
    MusashiPrefixStop stop;
    uint32_t value = 0;
    int32_t previous = 0;
    uint8_t *record;

    CHECK(argc == 1 || argc == 2);
    CHECK(musashi_boot_write32(&memory, 0x8006cbe8u, 0xfedcba98u));
    CHECK(musashi_boot_read32(&memory, 0x8006cbe8u, &value));
    CHECK(value == 0xfedcba98u);
    CHECK(musashi_boot_call_80043060(&memory, -1, &previous));
    CHECK(previous == -19088744);
    CHECK(musashi_boot_read32(&memory, 0x8006cbe8u, &value));
    CHECK(value == 0xffffffffu);
    CHECK(!musashi_boot_write32(&memory, 0x1f801814u, 0));
    CHECK(!musashi_boot_ram_span(&memory, 0x801fffffu, 2));
    record = musashi_boot_ram_span(&memory, 0x80078d98u, 0x4c);
    CHECK(record != NULL);
    CHECK(musashi_boot_ram_span(&memory, 0x80078da0u, 2) == record + 8);
    CHECK(musashi_boot_call_8005c29c(&memory, 0x80078874u, 0x1234, 0x100));
    CHECK(memory.bytes[0x78874] == 0x34 && memory.bytes[0x78973] == 0x34);
    CHECK(memory.bytes[0x78873] == 0 && memory.bytes[0x78974] == 0);
    CHECK(!musashi_boot_call_8005c29c(&memory, 0x1f801810u, 0, 4));
    CHECK(musashi_boot_write32(&memory, 0x80078978u, 7));
    memory.bytes[0x728a9] = 1;
    console_length = 0;
    CHECK(musashi_boot_console_char(&memory, 'A', record_console, NULL));
    CHECK(console_length == 1 && console_bytes[0] == 'A');
    CHECK(musashi_boot_console_char(&memory, '\n', record_console, NULL));
    CHECK(console_length == 3 && console_bytes[1] == '\r' && console_bytes[2] == '\n');
    memcpy(&memory.bytes[0x74498], "fallback\0", 9);
    memcpy(&memory.bytes[0x74500], "OK\0", 3);
    console_length = 0;
    CHECK(musashi_boot_console_string(&memory, 0x80074500u,
                                      record_console, NULL));
    CHECK(console_length == 2 && console_bytes[0] == 'O' && console_bytes[1] == 'K');
    console_length = 0;
    CHECK(musashi_boot_console_string(&memory, 0, record_console, NULL) == 0);
    CHECK(!musashi_boot_load_entry(&memory, image, 0x65000u, 0, &state));
    CHECK(state.stack_address == 11 && state.heap_address == 22 && state.heap_size == 33);

    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb");
        size_t size;
        CHECK(file != NULL);
        size = fread(image, 1, sizeof(image), file);
        CHECK(!ferror(file));
        CHECK(fclose(file) == 0);
        CHECK(size == 0x65000u);
        memset(&memory, 0xa5, sizeof(memory));
        CHECK(musashi_boot_load_entry(&memory, image, size, 0x12345678u, &state));
        CHECK(state.stack_address == 0x801ffff8u);
        CHECK(state.heap_address == 0x800c7f08u && state.heap_size == 0x1374f0u);
        CHECK(musashi_boot_read32(&memory, 0x80074750u, &value));
        CHECK(value == 0x12345678u);
        CHECK(musashi_boot_run_prefix(&memory, &stop) == MUSASHI_PREFIX_UNSUPPORTED_CALL);
        CHECK(stop.call_address == 0x800101c0u && stop.target_address == 0x800141f0u);
        CHECK(musashi_boot_read32(&memory, 0x80062998u, &value) && value == 1);
        CHECK(musashi_boot_read32(&memory, 0x80074778u, &value) && value == 0x3e0u);
        CHECK(musashi_boot_read32(&memory, 0x8006cbe8u, &value) && value == 0);
        puts("boot_data_probe: pinned entry data and bounded prefix checks PASS; "
             "incoming_ra=SYNTHETIC; startup_prefix=PARTIAL; "
             "stop=800101C0->800141F0; game_boot=NOT_RUN");
    } else {
        puts("boot_data_probe: synthetic boundary checks PASS; retail_load=NOT_RUN; game_boot=NOT_RUN");
    }
    return 0;
}
