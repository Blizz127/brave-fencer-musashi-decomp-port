#include "musashi_cd_dma3.h"
#include "musashi_boot_memory.h"
#include "musashi_dma_controller.h"
#include <stdlib.h>
#include <string.h>

#define DMA3_MADR 0x1f8010b0u
#define DMA3_BCR 0x1f8010b4u
#define DMA3_CHCR 0x1f8010b8u
#define CDROM_DELAY 0x1f801018u
#define DMA3_BUSY 0x01000000u
#define DMA3_SECTOR_BYTES 2340u

struct MusashiCdDma3 {
    MusashiBootMemory *memory;
    MusashiDmaController *dma;
    MusashiCdDma3Device device;
    MusashiCdDma3State state;
    uint64_t thread;
    unsigned active;
    unsigned violation;
};

static int fault(MusashiCdDma3 *d, MusashiCdDma3Fault why) {
    if (d->state.fault == MUSASHI_CD_DMA3_OK) d->state.fault = why;
    return 0;
}

/* Take the recursion guard before calling any external identity provider.
 * A separate violation bit detects nested cleanup even after an older fault. */
static int enter(MusashiCdDma3 *d, int diagnostic) {
    uint64_t thread;
    if (!d) return 0;
    if (d->active) {
        d->violation = 1;
        return fault(d, MUSASHI_CD_DMA3_REENTRY);
    }
    d->active = 1;
    d->violation = 0;
    thread = d->device.current_thread(d->device.userdata);
    if (!thread || thread != d->thread || d->violation) {
        if (!d->violation) fault(d, MUSASHI_CD_DMA3_THREAD);
        d->active = 0;
        return 0;
    }
    if (!diagnostic && d->state.fault != MUSASHI_CD_DMA3_OK) {
        d->active = 0;
        return 0;
    }
    if (!diagnostic && (!d->device.healthy(d->device.userdata) || d->violation)) {
        fault(d, MUSASHI_CD_DMA3_BACKEND);
        d->active = 0;
        return 0;
    }
    return 1;
}
static int leave(MusashiCdDma3 *d, int accepted) {
    d->active = 0;
    return accepted && !d->violation && d->state.fault == MUSASHI_CD_DMA3_OK;
}

/* Refuse cache mirrors outside the actual backing instead of folding them into
 * unrelated RAM. MADR register high byte is not stored by the hardware. */
static int ram_address(uint32_t address, uint32_t *physical) {
    uint32_t base = address & 0xe0000000u;
    uint32_t offset;
    if (base != 0 && base != 0x80000000u && base != 0xa0000000u) return 0;
    offset = address & 0x1fffffffu;
    if ((address & 3u) || offset >= MUSASHI_RAM_SIZE) return 0;
    *physical = offset;
    return 1;
}
static int transfer_ready(MusashiCdDma3 *d, size_t bytes) {
    size_t available = 0;
    if (!(d->dma->control & 0x8000u) || !d->dma->irq ||
        (d->state.cdrom_delay != 0x20943u && d->state.cdrom_delay != 0x21020843u) || bytes > MUSASHI_RAM_SIZE - d->state.madr)
        return fault(d, MUSASHI_CD_DMA3_ACCESS);
    if (!d->device.data_available(d->device.userdata, &available) || d->violation)
        return fault(d, MUSASHI_CD_DMA3_BACKEND);
    if (available < bytes) return fault(d, MUSASHI_CD_DMA3_ACCESS);
    return 1;
}

static int valid_bcr(uint32_t value) {
    uint32_t words = value & 0xffffu;
    /* SyncMode0 ignores BCR high16; the movie reader writes zero there. */
    return words > 0 && words <= DMA3_SECTOR_BYTES / 4u;
}

MusashiCdDma3 *musashi_cd_dma3_open(MusashiBootMemory *memory,
    MusashiDmaController *dma, const MusashiCdDma3Device *device, uint64_t epoch) {
    MusashiCdDma3 *d;
    if (!memory || !dma || !dma->irq || !device || !device->current_thread ||
        !device->healthy || !device->data_available || !device->read_data || !device->request_asserted) return NULL;
    d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->memory = memory; d->dma = dma; d->device = *device;
    d->state.cycle = epoch;
    d->active = 1;
    d->thread = d->device.current_thread(d->device.userdata);
    if (!d->thread || !d->device.healthy(d->device.userdata) || d->violation) {
        free(d); return NULL;
    }
    d->active = 0;
    return d;
}

int musashi_cd_dma3_read32(void *userdata, uint32_t address, uint32_t *out) {
    MusashiCdDma3 *d = userdata;
    uint32_t value;
    if (!enter(d, 0)) return 0;
    if (!out) return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
    switch (address) {
    case DMA3_MADR: value = d->state.madr; break;
    case DMA3_BCR: value = d->state.bcr; break;
    case DMA3_CHCR: value = d->state.chcr; break;
    case CDROM_DELAY: value = d->state.cdrom_delay; break;
    default: return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
    }
    *out = value;
    return leave(d, 1);
}

int musashi_cd_dma3_write32(void *userdata, uint32_t address, uint32_t value) {
    MusashiCdDma3 *d = userdata;
    uint32_t physical, words;
    size_t bytes;
    if (!enter(d, 0)) return 0;
    /* CDROM_DELAY is a memory-control register, not DMA channel state.
     * Retail 80046EAC can update it while a chopped transfer is paused/resuming.
     * Retail 80045090 reconfigures MADR/BCR/CHCR to start block reads while a chopped
     * background stream was active. Abort active chopped transfer on new channel setup. */
    if ((d->state.chcr & DMA3_BUSY) && !(d->state.chcr & 0x100u) && address != CDROM_DELAY)
        return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
    if ((d->state.chcr & 0x100u) && address != CDROM_DELAY) {
        d->state.chcr = 0;
        d->state.waiting_for_request = 0;
    }
    switch (address) {
    case CDROM_DELAY:
        /* Retail stream restores20843 between20943 DMA submissions. */
        if (value != 0x20943u && value != 0x20843u && value != 0x21020843u) return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
        d->state.cdrom_delay = value;
        break;
    case DMA3_MADR:
        if (!ram_address(value, &physical)) return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
        d->state.madr = physical;
        break;
    case DMA3_BCR:
        if (!valid_bcr(value))
            return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
        d->state.bcr = value;
        break;
    case DMA3_CHCR:
        if (value == 0) { d->state.chcr = 0; break; }
        if ((value != 0x11000000u && value != 0x11400100u) ||
            (value == 0x11400100u && d->state.cdrom_delay != 0x21020843u) ||
            !valid_bcr(d->state.bcr))
            return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
        words = d->state.bcr & 0xffffu;
        bytes = (size_t)words * 4u;
        if (d->state.cycle > UINT64_MAX - (value==0x11400100u ? (uint64_t)words*17u : words) || d->state.bytes > UINT64_MAX - bytes ||
            d->state.transfers == UINT64_MAX)
            return leave(d, fault(d, MUSASHI_CD_DMA3_OVERFLOW));
        if (!transfer_ready(d, bytes)) return leave(d, 0);
        /* Inspected neighbor cdrom.cc:11000000 schedules low16(BCR)*1.
         * One atomic native transaction is deliberately delayed until then;
         * this is not an emulated bus stall or physical completion claim. */
        d->state.due = d->state.cycle + (value==0x11400100u ? 1u : words);
        d->state.chcr = value & ~0x10000000u;
        break;
    default: return leave(d, fault(d, MUSASHI_CD_DMA3_ACCESS));
    }
    return leave(d, 1);
}

int musashi_cd_dma3_advance(MusashiCdDma3 *d, uint64_t cycle) {
    uint8_t staging[DMA3_SECTOR_BYTES];
    size_t bytes;
    int accepted;
    if (!enter(d, 0)) return 0;
    if (cycle < d->state.cycle) return leave(d, fault(d, MUSASHI_CD_DMA3_CLOCK));
    if (!(d->state.chcr & DMA3_BUSY) || cycle < d->state.due) {
        d->state.cycle = cycle;
        return leave(d, 1);
    }
    if (d->state.chcr&0x100u) {
        /* Observed11400100: DMA window1 word, CPU window16 cycles.
         * Native ordered deadlines; no physical bus-arbitration claim. */
        while ((d->state.chcr&DMA3_BUSY) && cycle>=d->state.due) {
            int requested = -1;
            if (!d->device.request_asserted(d->device.userdata,&requested) ||
                d->violation || (requested != 0 && requested != 1))
                return leave(d,fault(d,MUSASHI_CD_DMA3_BACKEND));
            /* CD BFRD drives DREQ. Deassertion pauses transport, not a FIFO
             * read or completion. Resume at the observation cycle rather
             * than spending the paused interval as accumulated DMA credit. */
            if (!requested) {
                if (cycle == UINT64_MAX)
                    return leave(d,fault(d,MUSASHI_CD_DMA3_OVERFLOW));
                d->state.waiting_for_request=1;
                d->state.cycle=cycle; d->state.due=cycle+1u;
                return leave(d,1);
            }
            if (d->state.waiting_for_request) {
                d->state.waiting_for_request=0;
                d->state.due=cycle;
            }
            if ((d->state.bcr&0xffffu)>1u && d->state.due>UINT64_MAX-17u)
                return leave(d,fault(d,MUSASHI_CD_DMA3_OVERFLOW));
            if (!transfer_ready(d,4u)) return leave(d,0);
            accepted=d->device.read_data(d->device.userdata,staging,4u);
            if (accepted!=1) return leave(d,fault(d,MUSASHI_CD_DMA3_BACKEND));
            memcpy(d->memory->bytes+d->state.madr,staging,4u);
            d->state.madr+=4u; d->state.bytes+=4u;
            d->state.bcr=(d->state.bcr&0xffff0000u)|((d->state.bcr-1u)&0xffffu);
            d->state.cycle=d->state.due;
            if (!(d->state.bcr&0xffffu)) {
                d->state.chcr&=~DMA3_BUSY; ++d->state.transfers;
                if (!musashi_dma_controller_complete(d->dma,3))
                    return leave(d,fault(d,MUSASHI_CD_DMA3_BACKEND));
            } else d->state.due+=17u;
            if (d->state.fault!=MUSASHI_CD_DMA3_OK) return leave(d,0);
        }
        d->state.cycle=cycle;return leave(d,1);
    }
    bytes = (size_t)(d->state.bcr & 0xffffu) * 4u;
    if (!transfer_ready(d, bytes)) return leave(d, 0);
    accepted = d->device.read_data(d->device.userdata, staging, bytes);
    if (accepted != 1) return leave(d, fault(d, MUSASHI_CD_DMA3_BACKEND));
    /* The external FIFO has really consumed these bytes. Even a nested-call
     * violation must retain that accepted transfer rather than invent rollback.
     * No fallible callback exists between this copy and the completion latch. */
    memcpy(d->memory->bytes + d->state.madr, staging, bytes);
    d->state.cycle = d->state.due;
    d->state.bytes += bytes;
    ++d->state.transfers;
    d->state.chcr = 0;
    if (!musashi_dma_controller_complete(d->dma, 3))
        return leave(d, fault(d, MUSASHI_CD_DMA3_BACKEND));
    if (d->state.fault != MUSASHI_CD_DMA3_OK) return leave(d, 0);
    d->state.cycle = cycle;
    return leave(d, 1);
}

int musashi_cd_dma3_get_state(const MusashiCdDma3 *owner, MusashiCdDma3State *out) {
    MusashiCdDma3 *d = (MusashiCdDma3 *)owner;
    if (!enter(d, 1)) return 0;
    if (!out) { d->active = 0; return 0; }
    *out = d->state;
    d->active = 0;
    return 1;
}
int musashi_cd_dma3_close(MusashiCdDma3 *d) {
    if (!enter(d, 1)) return 0;
    free(d);
    return 1;
}
