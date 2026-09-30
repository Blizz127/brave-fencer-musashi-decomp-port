#include <stdint.h>
#include <stdio.h>

#include "musashi_boot_memory.h"

typedef struct FakeGpu {
    uint32_t status;
    uint32_t control;
    uint32_t addresses[8];
    uint32_t values[8];
    char kinds[8];
    unsigned count;
    MusashiBootMemory *memory;
} FakeGpu;

static uint32_t read32(void *userdata, uint32_t address) {
    FakeGpu *gpu = userdata;
    if (gpu->memory) {
        if (gpu->count == 1) {
            musashi_boot_write32(gpu->memory, 0x80072858u, 0x1f803810u);
            musashi_boot_write32(gpu->memory, 0x8007285cu, 0x1f802814u);
        } else if (gpu->count == 2) {
            musashi_boot_write32(gpu->memory, 0x8007285cu, 0x1f803814u);
        } else if (gpu->count == 4) {
            musashi_boot_write32(gpu->memory, 0x8007285cu, 0x1f804814u);
        }
    }
    if (gpu->count < 8) {
        gpu->addresses[gpu->count] = address;
        gpu->values[gpu->count] = 0;
        gpu->kinds[gpu->count++] = 'R';
    }
    if (gpu->memory) return gpu->count == 6 ? 0x1000 : gpu->count == 3 ? 5 : 0;
    return address == 0x1f801810u ? gpu->status : gpu->control;
}

static void write32(void *userdata, uint32_t address, uint32_t value) {
    FakeGpu *gpu = userdata;
    if (gpu->memory && gpu->count == 0)
        musashi_boot_write32(gpu->memory, 0x80072858u, 0x1f802810u);
    if (gpu->count < 8) {
        gpu->addresses[gpu->count] = address;
        gpu->values[gpu->count] = value;
        gpu->kinds[gpu->count++] = 'W';
    }
    if (address == 0x1f801810u)
        gpu->status = value;
    else
        gpu->control = value;
}

static int check(int condition, int line) {
    if (!condition) {
        fprintf(stderr, "gpu_device_probe: failed at line %d\n", line);
        return 0;
    }
    return 1;
}

#define CHECK(condition) do { if (!check((condition), __LINE__)) return 1; } while (0)

int main(void) {
    static MusashiBootMemory memory;
    FakeGpu gpu = {0};
    MusashiGpuDevice device = {read32, write32, &gpu};
    int32_t result = -1;
    CHECK(musashi_boot_write32(&memory, 0x80072858u, 0x1f801810u));
    CHECK(musashi_boot_write32(&memory, 0x8007285cu, 0x1f801814u));

    CHECK(!musashi_boot_call_8005c1c0(&memory, NULL, 0, &result));
    CHECK(!musashi_boot_call_8005c1c0(NULL, &device, 0, &result));
    CHECK(!musashi_boot_call_8005c1c0(&memory, &device, 0, NULL));
    CHECK(musashi_boot_call_8005c1c0(&memory, &device, 0, &result));
    CHECK(result == 0);
    CHECK(gpu.count == 6);
    CHECK(gpu.kinds[0] == 'W' && gpu.addresses[0] == 0x1f801814u &&
          gpu.values[0] == 0x10000007u);
    CHECK(gpu.kinds[1] == 'R' && gpu.addresses[1] == 0x1f801810u);
    CHECK(gpu.kinds[2] == 'R' && gpu.addresses[2] == 0x1f801814u);
    CHECK(gpu.kinds[3] == 'W' && gpu.addresses[3] == 0x1f801810u &&
          gpu.values[3] == 0xe1001007u);
    CHECK(gpu.kinds[4] == 'R' && gpu.addresses[4] == 0x1f801810u);

    gpu.status = 2;
    gpu.control = 0;
    gpu.count = 0;
    CHECK(musashi_boot_call_8005c1c0(&memory, &device, 0, &result));
    CHECK(result == 3);
    CHECK(gpu.count == 2);
    CHECK(gpu.kinds[1] == 'R' && gpu.addresses[1] == 0x1f801810u);

    gpu.count = 0;
    CHECK(musashi_boot_call_8005c1c0(&memory, &device, 8, &result));
    CHECK(result == 4);
    CHECK(gpu.count == 3);
    CHECK(gpu.kinds[2] == 'W' && gpu.addresses[2] == 0x1f801814u &&
          gpu.values[2] == 0x09000001u);
    gpu = (FakeGpu){.memory = &memory};
    CHECK(musashi_boot_call_8005c1c0(&memory, &device, 8, &result));
    CHECK(result == 2 && gpu.count == 7);
    CHECK(gpu.addresses[0] == 0x1f801814u);
    CHECK(gpu.addresses[1] == 0x1f802810u);
    CHECK(gpu.addresses[2] == 0x1f802814u);
    CHECK(gpu.addresses[3] == 0x1f802810u && gpu.values[3] == 0xe1001005u);
    CHECK(gpu.addresses[4] == 0x1f803810u);
    CHECK(gpu.addresses[5] == 0x1f803814u);
    CHECK(gpu.addresses[6] == 0x1f803814u && gpu.values[6] == 0x20000504u);
    puts("gpu_device_probe: callback/MMIO contract PASS; native boot NOT_RUN");
    return 0;
}
