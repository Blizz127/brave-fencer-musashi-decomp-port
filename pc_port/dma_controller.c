#include "musashi_dma_controller.h"

/* Register contract: https://psx-spx.consoledev.net/dmachannels/
 * DICR flags are write-one-to-clear; its master flag is derived, and only
 * a rising edge raises I_STAT. BOTH master and channel enables gate a new
 * completion flag, not an already-latched flag. Transfers are separate owners.
 * PSX-SPX DICR; inspected neighbor psxmem.h:147 dmaInterrupt agrees. */
void musashi_dma_controller_init(MusashiDmaController *dma, MusashiIrqController *irq) {
    if (!dma) return;
    dma->control = 0x07654321u;
    dma->interrupt = 0;
    dma->irq = irq;
    dma->gpu_channel_control = 0;
    dma->gpu_owner = 0;
}

int musashi_dma_controller_read32(void *userdata, uint32_t address, uint32_t *value) {
    MusashiDmaController *dma = userdata;
    if (!dma || !value) return 0;
    if (address == 0x1f8010f0u) *value = dma->control;
    else if (address == 0x1f8010f4u) *value = dma->interrupt;
    else if (address == 0x1f8010a8u) *value = dma->gpu_channel_control;
    else return 0;
    return 1;
}

int musashi_dma_controller_write32(void *userdata, uint32_t address, uint32_t value) {
    MusashiDmaController *dma = userdata;
    uint32_t next;
    int rising;
    if (!dma) return 0;
    if (address == 0x1f8010a8u) {
        /* Retail 8005BDFC configures linked-list direction without starting
         * DMA. Keep that real idle state; never turn a transfer into success.
         * Additional modes require a transfer owner before being accepted. */
        if (dma->gpu_owner || (dma->gpu_channel_control & 0x01000000u) ||
            (value != 0 && value != 0x401u)) return 0;
        dma->gpu_channel_control = value;
        return 1;
    }
    if (address == 0x1f8010f0u) {
        dma->control = value;
        return 1;
    }
    if (address != 0x1f8010f4u) return 0;
    next = (value & 0x00ff807fu) | (dma->interrupt & 0x7f000000u & ~value);
    if ((next & 0x8000u) || ((next & 0x800000u) && (next & 0x7f000000u)))
        next |= 0x80000000u;
    rising = (next & ~dma->interrupt & 0x80000000u) != 0;
    if (rising && !dma->irq) return 0;
    dma->interrupt = next;
    if (rising) dma->irq->status |= 8u;
    return 1;
}

int musashi_dma_controller_complete(MusashiDmaController *dma, unsigned channel) {
    uint32_t next;
    if (!dma || !dma->irq || channel > 6) return 0;
    if (!(dma->interrupt & 0x00800000u) || !(dma->interrupt & (1u << (16 + channel)))) return 1;
    next = dma->interrupt | (1u << (24 + channel)) | 0x80000000u;
    if (!(dma->interrupt & 0x80000000u)) dma->irq->status |= 8u;
    dma->interrupt = next;
    return 1;
}
