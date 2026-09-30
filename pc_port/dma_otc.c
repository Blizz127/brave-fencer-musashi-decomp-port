#include "musashi_dma_otc.h"
#include "musashi_boot_memory.h"
#include "musashi_dma_controller.h"
#include <stdlib.h>

#define DMA6_MADR 0x1f8010e0u
#define DMA6_BCR 0x1f8010e4u
#define DMA6_CHCR 0x1f8010e8u
#define DMA6_BUSY 0x01000000u
#define DMA6_ENABLE 0x08000000u
#define DMA6_START 0x11000002u
#define DMA6_IDLE 0x10000002u
#define DMA6_MAX_WORDS 0x4000u

struct MusashiDmaOtc {
    MusashiBootMemory *memory;
    MusashiDmaController *dma;
    MusashiDmaOtcDevice device;
    MusashiDmaOtcState state;
    uint64_t thread;
    unsigned active;
    unsigned violation;
};

static int fault(MusashiDmaOtc *d, MusashiDmaOtcFault why) {
    if (d->state.fault == MUSASHI_DMA_OTC_OK) d->state.fault = why;
    return 0;
}

static int enter(MusashiDmaOtc *d, int diagnostic) {
    uint64_t thread;
    if (!d) return 0;
    if (d->active) {
        d->violation = 1;
        return fault(d, MUSASHI_DMA_OTC_REENTRY);
    }
    d->active = 1;
    d->violation = 0;
    thread = d->device.current_thread(d->device.userdata);
    if (!thread || thread != d->thread || d->violation) {
        if (!d->violation) fault(d, MUSASHI_DMA_OTC_THREAD);
        d->active = 0;
        return 0;
    }
    if (!diagnostic && d->state.fault != MUSASHI_DMA_OTC_OK) {
        d->active = 0;
        return 0;
    }
    if (!diagnostic && (!d->device.healthy(d->device.userdata) || d->violation)) {
        fault(d, MUSASHI_DMA_OTC_BACKEND);
        d->active = 0;
        return 0;
    }
    return 1;
}

static int leave(MusashiDmaOtc *d, int accepted) {
    d->active = 0;
    return accepted && !d->violation && d->state.fault == MUSASHI_DMA_OTC_OK;
}

static int ram_address(uint32_t address, uint32_t *physical) {
    uint32_t base = address & 0xe0000000u;
    uint32_t offset;
    if (base != 0 && base != 0x80000000u && base != 0xa0000000u) return 0;
    offset = address & 0x1fffffffu;
    if ((address & 3u) || offset >= MUSASHI_RAM_SIZE) return 0;
    *physical = offset;
    return 1;
}

static int run_otc(MusashiDmaOtc *d) {
    uint32_t count = d->state.bcr & 0xffffu;
    uint32_t addr = d->state.madr;
    uint32_t index;
    if (!(d->dma->control & DMA6_ENABLE) || !d->dma->irq ||
        (d->state.bcr >> 16) != 0 || count == 0 || count > DMA6_MAX_WORDS)
        return fault(d, MUSASHI_DMA_OTC_ACCESS);
    if (addr < (count - 1u) * 4u || addr + 4u > MUSASHI_RAM_SIZE)
        return fault(d, MUSASHI_DMA_OTC_ACCESS);
    if (d->state.words > UINT64_MAX - count || d->state.transfers == UINT64_MAX)
        return fault(d, MUSASHI_DMA_OTC_OVERFLOW);
    for (index = 0; index < count; index++) {
        uint32_t word = (index + 1u == count) ? 0x00ffffffu : ((addr - 4u) & 0x00ffffffu);
        if (!musashi_boot_write32(d->memory, 0x80000000u | addr, word))
            return fault(d, MUSASHI_DMA_OTC_ACCESS);
        addr -= 4u;
    }
    d->state.madr = addr;
    d->state.chcr = DMA6_IDLE;
    d->state.words += count;
    ++d->state.transfers;
    if (!musashi_dma_controller_complete(d->dma, 6u))
        return fault(d, MUSASHI_DMA_OTC_BACKEND);
    return 1;
}

MusashiDmaOtc *musashi_dma_otc_open(MusashiBootMemory *memory,
    MusashiDmaController *dma, const MusashiDmaOtcDevice *device, uint64_t epoch) {
    MusashiDmaOtc *d;
    if (!memory || !dma || !dma->irq || !device || !device->current_thread ||
        !device->healthy) return NULL;
    d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->memory = memory;
    d->dma = dma;
    d->device = *device;
    d->state.cycle = epoch;
    d->active = 1;
    d->thread = d->device.current_thread(d->device.userdata);
    if (!d->thread || !d->device.healthy(d->device.userdata) || d->violation) {
        free(d);
        return NULL;
    }
    d->active = 0;
    return d;
}

int musashi_dma_otc_read32(void *userdata, uint32_t address, uint32_t *out) {
    MusashiDmaOtc *d = userdata;
    uint32_t value;
    if (!enter(d, 0)) return 0;
    if (!out) return leave(d, fault(d, MUSASHI_DMA_OTC_ACCESS));
    switch (address) {
    case DMA6_MADR: value = d->state.madr; break;
    case DMA6_BCR: value = d->state.bcr; break;
    case DMA6_CHCR: value = d->state.chcr; break;
    default: return leave(d, fault(d, MUSASHI_DMA_OTC_ACCESS));
    }
    *out = value;
    return leave(d, 1);
}

int musashi_dma_otc_write32(void *userdata, uint32_t address, uint32_t value) {
    MusashiDmaOtc *d = userdata;
    uint32_t physical;
    if (!enter(d, 0)) return 0;
    if ((d->state.chcr & DMA6_BUSY) && address == DMA6_CHCR && value != 0)
        return leave(d, fault(d, MUSASHI_DMA_OTC_ACCESS));
    switch (address) {
    case DMA6_MADR:
        if (!ram_address(value, &physical))
            return leave(d, fault(d, MUSASHI_DMA_OTC_ACCESS));
        d->state.madr = physical;
        break;
    case DMA6_BCR:
        d->state.bcr = value;
        break;
    case DMA6_CHCR:
        if (value == 0) {
            d->state.chcr = 0;
            break;
        }
        if (value != DMA6_START)
            return leave(d, fault(d, MUSASHI_DMA_OTC_ACCESS));
        d->state.chcr = DMA6_BUSY | (DMA6_START & ~DMA6_BUSY);
        if (!run_otc(d)) return leave(d, 0);
        break;
    default:
        return leave(d, fault(d, MUSASHI_DMA_OTC_ACCESS));
    }
    return leave(d, 1);
}

int musashi_dma_otc_get_state(const MusashiDmaOtc *owner, MusashiDmaOtcState *out) {
    MusashiDmaOtc *d = (MusashiDmaOtc *)owner;
    if (!enter(d, 1)) return 0;
    if (!out) {
        d->active = 0;
        return 0;
    }
    *out = d->state;
    d->active = 0;
    return 1;
}

int musashi_dma_otc_close(MusashiDmaOtc *d) {
    if (!enter(d, 1)) return 0;
    free(d);
    return 1;
}
