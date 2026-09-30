#include "musashi_boot_memory.h"
#include "musashi_psyq_hle.h"

/* libgpu graphics reset (guest 8005BD7C), port-owned HLE.
 *
 * Interrupts are masked for the reset by exchanging I_MASK (pointer cell
 * 8006CB8C) with 0, and restored after. The reset state words 80072890 and
 * 8007288C are cleared and the saved mask kept at 8007289C. Mode & 7:
 *   0, 5: GPU DMA CHCR (cell 80072868) = 401h, DPCR (cell 80072878) |= 800h
 *         (GPU channel enable), GP1(0) full GPU reset, and the libgpu state
 *         blocks 80078874 (100h bytes) and 800C5510 (1800h bytes) cleared;
 *   1, 3: the same DMA setup, then GP1(02000000h) acknowledge IRQ and
 *         GP1(01000000h) reset the command buffer;
 *   other: DMA and GPU untouched.
 * Mode 0 finishes with the GPU-type probe (8005C1C0) and returns its value;
 * every other mode returns 0. Pointer cells are read at each access. */

enum {
    I_MASK_POINTER = 0x8006cb8cu,
    RESET_STATE = 0x80072890u,
    RESET_STATE_COPY = 0x8007288cu,
    SAVED_MASK = 0x8007289cu,
    GPU_CHCR_POINTER = 0x80072868u,
    DPCR_POINTER = 0x80072878u,
    GP1_POINTER = 0x8007285cu,
};

static uint32_t cell(MusashiBootMemory *memory, uint32_t address) {
    uint32_t value = 0;
    musashi_boot_read32(memory, address, &value);
    return value;
}

static int32_t exchange_mask(MusashiBootMemory *memory, const MusashiGpuDevice *device,
                             int32_t value) {
    uint32_t address = cell(memory, I_MASK_POINTER);
    uint16_t previous = device->read16(device->userdata, address);
    device->write16(device->userdata, address, (uint16_t)value);
    return previous;
}

static void write_through(MusashiBootMemory *memory, const MusashiGpuDevice *device,
                          uint32_t pointer, uint32_t value) {
    device->write32(device->userdata, cell(memory, pointer), value);
}

static void dma_setup(MusashiBootMemory *memory, const MusashiGpuDevice *device) {
    uint32_t dpcr;
    write_through(memory, device, GPU_CHCR_POINTER, 0x401u);
    dpcr = cell(memory, DPCR_POINTER);
    device->write32(device->userdata, dpcr, device->read32(device->userdata, dpcr) | 0x800u);
}

int musashi_boot_call_8005bd7c(MusashiBootMemory *memory,
                               const MusashiGpuDevice *device,
                               int32_t mode, int32_t *result) {
    int32_t saved;
    uint32_t state;
    uint8_t *state_a, *state_b;
    if (!memory || !device || !device->read16 || !device->write16 ||
        !device->read32 || !device->write32 || !result ||
        !(state_a = musashi_boot_ram_span(memory, 0x80078874u, 0x100u)) ||
        !(state_b = musashi_boot_ram_span(memory, 0x800c5510u, 0x1800u)))
        return 0;
    saved = exchange_mask(memory, device, 0);
    musashi_boot_write32(memory, RESET_STATE, 0);
    musashi_boot_write32(memory, SAVED_MASK, (uint32_t)saved);
    state = cell(memory, RESET_STATE);
    musashi_boot_write32(memory, RESET_STATE_COPY, state);
    switch (mode & 7) {
    case 0:
    case 5:
        dma_setup(memory, device);
        write_through(memory, device, GP1_POINTER, 0);
        musashi_hle_fill_bytes(state_a, 0, 0x100u);
        musashi_hle_fill_bytes(state_b, 0, 0x1800u);
        break;
    case 1:
    case 3:
        dma_setup(memory, device);
        write_through(memory, device, GP1_POINTER, 0x02000000u);
        write_through(memory, device, GP1_POINTER, 0x01000000u);
        break;
    default:
        break;
    }
    exchange_mask(memory, device, (int32_t)cell(memory, SAVED_MASK));
    if ((mode & 7) == 0)
        return musashi_boot_call_8005c1c0(memory, device, mode, result);
    *result = 0;
    return 1;
}
