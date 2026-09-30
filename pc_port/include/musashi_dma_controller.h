#ifndef MUSASHI_DMA_CONTROLLER_H
#define MUSASHI_DMA_CONTROLLER_H

#include "musashi_irq_controller.h"

/* DPCR/DICR and the observed idle DMA2 CHCR configurations only.
 * Channel registers/transfers belong to separate device owners.
 * The IRQ controller must outlive this object. The owner serializes access. */
struct MusashiGpuDma2;
typedef struct MusashiDmaController {
    uint32_t control;
    uint32_t interrupt;
    MusashiIrqController *irq;
    uint32_t gpu_channel_control;
    /* Borrowed exclusive lease; only the installed DMA2 owner writes CHCR. */
    struct MusashiGpuDma2 *gpu_owner;
} MusashiDmaController;

void musashi_dma_controller_init(MusashiDmaController *, MusashiIrqController *);
int musashi_dma_controller_read32(void *, uint32_t address, uint32_t *value);
int musashi_dma_controller_write32(void *, uint32_t address, uint32_t value);
/* Device-owner completion only AFTER its actual accepted transfer. Master and
 * channel enables gate the new flag latch; existing flags retain their W1C
 * semantics. IRQ3 (mask8) is latched on master rising, never guest-dispatched.
 * Invalid channel/missing IRQ refuses before register effects. This function
 * does not start, copy or complete a transfer itself. */
int musashi_dma_controller_complete(MusashiDmaController *, unsigned channel);

#endif
