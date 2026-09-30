#include "musashi_boot_memory.h"
#include "musashi_psyq_hle.h"

int musashi_boot_call_80042c64(MusashiBootMemory *memory, uint32_t address,
                               uint32_t word_count) {
    size_t bytes;
    uint8_t *destination;

    /* Bound before multiplication, including on 32-bit hosts. A zero count
     * performs no retail sw, so retain the existing no-access alignment case. */
    if (word_count > MUSASHI_RAM_SIZE / 4u || (word_count && (address & 3u)))
        return 0;
    bytes = (size_t)word_count * 4u;
    destination = musashi_boot_ram_span(memory, address, bytes ? bytes : 1u);
    if (!destination)
        return 0;
    musashi_hle_clear_words(destination, word_count);
    return 1;
}

int musashi_boot_call_8005b684(MusashiBootMemory *memory,
                               const MusashiGpuDevice *device,
                               uint32_t command) {
    uint32_t control_address;
    uint8_t *status;
    if (!memory || !device || !device->write32 ||
        !musashi_boot_read32(memory, 0x8007285cu, &control_address))
        return 0;
    status = musashi_boot_ram_span(memory, 0x80078874u + (command >> 24), 1);
    if (!status)
        return 0;
    device->write32(device->userdata, control_address, command);
    *status = (uint8_t)command;
    return 1;
}

