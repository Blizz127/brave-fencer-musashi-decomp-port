#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "musashi_boot_memory.h"

typedef struct ResetDevice {
    uint32_t status;
    uint32_t control;
    uint32_t other;
    uint32_t addresses[16];
    uint32_t values[16];
    char kinds[16];
    unsigned count;
    uint16_t mask;
    MusashiBootMemory *redirect_memory;
    MusashiBootMemory *redirect_memory32;
} ResetDevice;

static uint16_t read16(void *userdata, uint32_t address) {
    ResetDevice *device = userdata;
    if (device->count >= 16 ||
        (address != 0x1f801074u && address != 0x1f801076u)) abort();
    device->addresses[device->count] = address;
    device->values[device->count] = 0;
    device->kinds[device->count++] = 'r';
    if (device->redirect_memory) {
        musashi_boot_write32(device->redirect_memory, 0x8006cb8cu, 0x1f801076u);
        device->redirect_memory = NULL;
    }
    return device->mask;
}

static void write16(void *userdata, uint32_t address, uint16_t value) {
    ResetDevice *device = userdata;
    if (device->count >= 16 ||
        (address != 0x1f801074u && address != 0x1f801076u)) abort();
    device->addresses[device->count] = address;
    device->values[device->count] = value;
    device->kinds[device->count++] = 'w';
    device->mask = value;
}

static uint32_t read32(void *userdata, uint32_t address) {
    ResetDevice *device = userdata;
    if (device->count >= 16) abort();
    device->addresses[device->count] = address;
    device->values[device->count] = 0;
    device->kinds[device->count++] = 'R';
    if (address == 0x1f801810u)
        return device->status;
    if (address == 0x1f801814u)
        return device->control;
    if (address == 0x1f8010f0u || address == 0x1f8020f0u) {
        if (device->redirect_memory32) {
            musashi_boot_write32(device->redirect_memory32, 0x80072878u, 0x1f8010f0u);
            musashi_boot_write32(device->redirect_memory32, 0x8007285cu, 0x1f802814u);
        }
        return device->other;
    }
    abort();
}

static void write32(void *userdata, uint32_t address, uint32_t value) {
    ResetDevice *device = userdata;
    if (device->count >= 16) abort();
    device->addresses[device->count] = address;
    device->values[device->count] = value;
    device->kinds[device->count++] = 'W';
    if (address == 0x1f801810u)
        device->status = value;
    else if (address == 0x1f801814u || address == 0x1f802814u) {
        device->control = value;
        if (device->redirect_memory32) {
            musashi_boot_write32(device->redirect_memory32, 0x8007285cu, 0x1f801814u);
            device->redirect_memory32 = NULL;
        }
    } else if (address == 0x1f8010f0u || address == 0x1f8020f0u)
        device->other = value;
    else if (address != 0x1f8010a8u && address != 0x1f8020a8u) abort();
}

static int check(int condition, int line) {
    if (!condition) {
        fprintf(stderr, "device_reset_probe: failed at line %d\n", line);
        return 0;
    }
    return 1;
}

#define CHECK(condition) do { if (!check((condition), __LINE__)) return 1; } while (0)

static int seed_pointers(MusashiBootMemory *memory) {
    return musashi_boot_write32(memory, 0x8006cb8cu, 0x1f801074u) &&
           musashi_boot_write32(memory, 0x80072858u, 0x1f801810u) &&
           musashi_boot_write32(memory, 0x80072868u, 0x1f8010a8u) &&
           musashi_boot_write32(memory, 0x80072878u, 0x1f8010f0u) &&
           musashi_boot_write32(memory, 0x8007285cu, 0x1f801814u);
}

int main(void) {
    static MusashiBootMemory memory, expected;
    ResetDevice state = {.status = 2, .other = 0x100, .mask = 0x55aa};
    MusashiGpuDevice device = {read32, write32, &state, read16, write16};
    int32_t result = -1;
    uint32_t value;
    CHECK(seed_pointers(&memory));

    CHECK(!musashi_boot_call_8005bd7c(&memory, NULL, 0, &result));
    CHECK(!musashi_boot_call_8005bd7c(&memory, &device, 0, NULL));
    CHECK(musashi_boot_call_8005bd7c(&memory, &device, 0, &result));
    CHECK(result == 3);
    CHECK(state.count == 10);
    CHECK(state.kinds[0] == 'r' && state.addresses[0] == 0x1f801074u);
    CHECK(state.kinds[1] == 'w' && state.addresses[1] == 0x1f801074u &&
          state.values[1] == 0);
    CHECK(state.kinds[2] == 'W' && state.addresses[2] == 0x1f8010a8u &&
          state.values[2] == 0x401u);
    CHECK(state.kinds[3] == 'R' && state.addresses[3] == 0x1f8010f0u);
    CHECK(state.kinds[4] == 'W' && state.addresses[4] == 0x1f8010f0u &&
          state.values[4] == 0x900u);
    CHECK(state.kinds[5] == 'W' && state.addresses[5] == 0x1f801814u &&
          state.values[5] == 0);
    CHECK(state.kinds[6] == 'r' && state.addresses[6] == 0x1f801074u);
    CHECK(state.kinds[7] == 'w' && state.addresses[7] == 0x1f801074u &&
          state.values[7] == 0x55aau);
    CHECK(state.kinds[8] == 'W' && state.addresses[8] == 0x1f801814u &&
          state.values[8] == 0x10000007u);
    CHECK(state.kinds[9] == 'R' && state.addresses[9] == 0x1f801810u);
    CHECK(memory.bytes[0x78874] == 0 && memory.bytes[0x78973] == 0);
    CHECK(memory.bytes[0xc5510] == 0 && memory.bytes[0xc6d0f] == 0);
    CHECK(musashi_boot_read32(&memory, 0x80072890u, &value) && value == 0);
    CHECK(musashi_boot_read32(&memory, 0x8007289cu, &value) && value == 0x55aau);

    state.count = 0;
    CHECK(musashi_boot_call_8005bd7c(&memory, &device, 1, &result));
    CHECK(result == 0 && state.count == 9);
    CHECK(state.addresses[5] == 0x1f801814u && state.values[5] == 0x02000000u);
    CHECK(state.addresses[6] == 0x1f801814u && state.values[6] == 0x01000000u);
    CHECK(state.kinds[7] == 'r' && state.addresses[7] == 0x1f801074u);
    CHECK(state.kinds[8] == 'w' && state.addresses[8] == 0x1f801074u &&
          state.values[8] == 0x55aau);
    /* Exhaust both values of mode bit 8 and all eight reset classes.
     * Compare every RAM byte so missing clears and collateral writes fail. */
    for (int mode = 0; mode < 16; ++mode) {
        unsigned kind = mode & 7;
        unsigned restore = kind == 0 || kind == 5 ? 6 :
                           kind == 1 || kind == 3 ? 7 : 2;
        memset(&memory, 0xa5, sizeof(memory));
        CHECK(seed_pointers(&memory));
        expected = memory;
        memset(expected.bytes + 0x7288c, 0, 8);
        expected.bytes[0x7289c] = 0xaa;
        expected.bytes[0x7289d] = 0x55;
        expected.bytes[0x7289e] = expected.bytes[0x7289f] = 0;
        if (kind == 0 || kind == 5) {
            memset(expected.bytes + 0x78874, 0, 0x100);
            memset(expected.bytes + 0xc5510, 0, 0x1800);
        }
        state = (ResetDevice){.status = 2, .other = 0x100, .mask = 0x55aa};
        CHECK(musashi_boot_call_8005bd7c(&memory, &device, mode, &result));
        CHECK(result == (kind == 0 ? (mode & 8 ? 4 : 3) : 0));
        CHECK(memcmp(&memory, &expected, sizeof(memory)) == 0);
        CHECK(state.mask == 0x55aa);
        CHECK(state.kinds[restore] == 'r' &&
              state.addresses[restore] == 0x1f801074u);
        CHECK(state.kinds[restore + 1] == 'w' &&
              state.values[restore + 1] == 0x55aa);
        CHECK(state.count == restore + 2 +
              (kind == 0 ? (mode & 8 ? 3u : 2u) : 0u));
    }
    /* Synthetic adapter address tests routing only, not hardware semantics.
     * A read callback changes the guest cell: this exchange must retain its
     * snapshot, while the restoring exchange must reload the changed cell. */
    CHECK(musashi_boot_write32(&memory, 0x8006cb8cu, 0x1f801074u));
    state = (ResetDevice){.mask = 0x55aa, .redirect_memory = &memory};
    CHECK(musashi_boot_call_8005bd7c(&memory, &device, 2, &result));
    CHECK(state.count == 4 && result == 0);
    CHECK(state.addresses[0] == 0x1f801074u);
    CHECK(state.addresses[1] == 0x1f801074u);
    CHECK(state.addresses[2] == 0x1f801076u);
    CHECK(state.addresses[3] == 0x1f801076u);
    CHECK(state.values[1] == 0 && state.values[3] == 0x55aau);
    CHECK(seed_pointers(&memory));
    CHECK(musashi_boot_write32(&memory, 0x80072868u, 0x1f8020a8u));
    CHECK(musashi_boot_write32(&memory, 0x80072878u, 0x1f8020f0u));
    state = (ResetDevice){.mask = 0x55aa, .other = 0x100,
                          .redirect_memory32 = &memory};
    CHECK(musashi_boot_call_8005bd7c(&memory, &device, 1, &result));
    CHECK(result == 0 && state.count == 9);
    CHECK(state.addresses[2] == 0x1f8020a8u);
    CHECK(state.addresses[3] == 0x1f8020f0u);
    CHECK(state.addresses[4] == 0x1f8020f0u && state.values[4] == 0x900u);
    CHECK(state.addresses[5] == 0x1f802814u && state.values[5] == 0x02000000u);
    CHECK(state.addresses[6] == 0x1f801814u && state.values[6] == 0x01000000u);
    puts("device_reset_probe: reset/MMIO/shared-RAM contract PASS; native boot NOT_RUN");
    return 0;
}
