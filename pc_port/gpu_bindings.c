#include "musashi_boot_memory.h"

/* libgpu GPU-type probe (guest 8005C1C0), port-owned HLE.
 *
 * GPU ports (psx-spx): the guest reaches GP0/GPUREAD through the pointer
 * cell 80072858 and GP1/GPUSTAT through 8007285C. GP1(10000007h) asks for
 * the GPU version; a reply of 2 identifies the 208-pin "new" GPU. Otherwise
 * the probe issues a GP0 draw-mode word built from GPUSTAT (bits 0-13) with
 * texture-disable and drawing-to-display bits (E1001000h), discards one
 * GPUREAD and inspects GPUSTAT bit 12. Mode bit 3 asks for the matching
 * 24-bit/interlace setup: GP1(20000504h) on the older GPU, GP1(09000001h)
 * on the new one. Result: 0 old GPU without bit 12, 1/2 old GPU (without /
 * with setup), 3/4 new GPU (without / with setup).
 *
 * Each access re-reads its pointer cell, and the discard snapshots both
 * cells, in the order the retail probe (and its device tests) observe. */

enum { GP0_POINTER = 0x80072858u, GP1_POINTER = 0x8007285cu };

static uint32_t cell(MusashiBootMemory *memory, uint32_t address) {
    uint32_t value = 0;
    musashi_boot_read32(memory, address, &value);
    return value;
}

int musashi_boot_call_8005c1c0(MusashiBootMemory *memory,
                               const MusashiGpuDevice *device,
                               int32_t mode, int32_t *result) {
    uint32_t gp0, gp1;
    if (!memory || !device || !device->read32 || !device->write32 || !result)
        return 0;
    device->write32(device->userdata, cell(memory, GP1_POINTER), 0x10000007u);
    gp0 = cell(memory, GP0_POINTER);
    if ((device->read32(device->userdata, gp0) & 0xffffffu) != 2u) {
        uint32_t status = device->read32(device->userdata, cell(memory, GP1_POINTER));
        device->write32(device->userdata, gp0, (status & 0x3fffu) | 0xe1001000u);
        gp0 = cell(memory, GP0_POINTER);
        gp1 = cell(memory, GP1_POINTER);
        (void)device->read32(device->userdata, gp0);
        if (!(device->read32(device->userdata, gp1) & 0x1000u)) {
            *result = 0;
        } else if (!(mode & 8)) {
            *result = 1;
        } else {
            device->write32(device->userdata, gp1, 0x20000504u);
            *result = 2;
        }
        return 1;
    }
    if (!(mode & 8)) {
        *result = 3;
        return 1;
    }
    device->write32(device->userdata, cell(memory, GP1_POINTER), 0x09000001u);
    *result = 4;
    return 1;
}
