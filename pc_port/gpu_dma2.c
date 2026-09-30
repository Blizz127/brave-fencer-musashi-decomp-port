#include "musashi_gpu_dma2.h"
#include "musashi_boot_memory.h"
#include "musashi_dma_controller.h"
#include "musashi_gpu_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DMA2_MADR 0x1f8010a0u
#define DMA2_BCR 0x1f8010a4u
#define DMA2_CHCR 0x1f8010a8u
#define DMA2_BUSY 0x01000000u
#define DMA2_START 0x01000401u
#define DMA2_BLOCK_START 0x01000201u
#define DMA2_WORD_LIMIT (MUSASHI_RAM_SIZE / 4u)

struct MusashiGpuDma2 {
    MusashiBootMemory *memory;
    MusashiDmaController *dma;
    MusashiGpuController *gpu;
    MusashiGpuDma2Device device;
    MusashiGpuDma2State state;
    uint64_t thread;
    uint32_t *snapshot;
    size_t capacity, used;
    uint8_t visited[DMA2_WORD_LIMIT / 8u];
    unsigned entered, violation, pending;
};

static int fault(MusashiGpuDma2 *d, MusashiGpuDma2Fault why) {
    if (d->state.fault == MUSASHI_GPU_DMA2_OK) d->state.fault = why;
    d->pending = 0; /* An incomplete/faulted transfer must never complete. */
    return 0;
}
static int enter(MusashiGpuDma2 *d, int diagnostic) {
    uint64_t thread;
    if (!d) return 0;
    if (d->entered) {
        d->violation = 1;
        return fault(d, MUSASHI_GPU_DMA2_REENTRY);
    }
    d->entered = 1;
    d->violation = 0;
    thread = d->device.current_thread(d->device.userdata);
    if (!thread || thread != d->thread || d->violation) {
        if (!d->violation) fault(d, MUSASHI_GPU_DMA2_THREAD);
        d->entered = 0;
        return 0;
    }
    if (d->dma->gpu_owner != d) {
        fault(d, MUSASHI_GPU_DMA2_ACCESS);
        d->entered = 0;
        return 0;
    }
    if (!diagnostic && d->state.fault != MUSASHI_GPU_DMA2_OK) {
        d->entered = 0;
        return 0;
    }
    if (!diagnostic && (!d->device.healthy(d->device.userdata) || d->violation ||
                        d->gpu->faulted || !d->dma->irq)) {
        fault(d, MUSASHI_GPU_DMA2_BACKEND);
        d->entered = 0;
        return 0;
    }
    return 1;
}
static int leave(MusashiGpuDma2 *d, int accepted) {
    d->entered = 0;
    return accepted && !d->violation && d->state.fault == MUSASHI_GPU_DMA2_OK;
}
static int ram_address(uint32_t value, uint32_t *physical) {
    uint32_t base = value & 0xe0000000u;
    uint32_t address = value & 0x1fffffffu;
    if ((base != 0 && base != 0x80000000u && base != 0xa0000000u) ||
        (value & 3u) || address >= MUSASHI_RAM_SIZE) return 0;
    *physical = address;
    return 1;
}
static uint32_t ram_word(const MusashiGpuDma2 *d, uint32_t address) {
    const uint8_t *p = d->memory->bytes + address;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int reserve(MusashiGpuDma2 *d, size_t count) {
    size_t capacity;
    uint32_t *words;
    if (count > DMA2_WORD_LIMIT) return fault(d, MUSASHI_GPU_DMA2_RESOURCE);
    if (count <= d->capacity) return 1;
    capacity = d->capacity ? d->capacity : 256;
    while (capacity < count) capacity *= 2;
    words = realloc(d->snapshot, capacity * sizeof(*words));
    if (!words) return fault(d, MUSASHI_GPU_DMA2_RESOURCE);
    d->snapshot = words;
    d->capacity = capacity;
    return 1;
}
/* Independently authored from the documented header layout. No heuristic loop
 * exit, RAM wrap, host tag, or silently truncated successful transfer. */
static int snapshot_chain(MusashiGpuDma2 *d, uint32_t *nodes, uint32_t *payload) {
    uint32_t address = d->state.madr;
    *nodes = *payload = 0;
    d->used = 0;
    memset(d->visited, 0, sizeof(d->visited));
    for (;;) {
        uint32_t tag, count, next, index;
        d->state.fault_node = address;
        d->state.fault_word = address;
        if ((address & 3u) || address > MUSASHI_RAM_SIZE - 4u)
            return fault(d, MUSASHI_GPU_DMA2_CHAIN);
        index = address / 4u;
        if (d->visited[index / 8u] & (1u << (index % 8u)))
            return fault(d, MUSASHI_GPU_DMA2_CHAIN);
        d->visited[index / 8u] |= (uint8_t)(1u << (index % 8u));
        tag = ram_word(d, address);
        count = tag >> 24;
        if ((1u + count) * 4u > MUSASHI_RAM_SIZE - address)
            return fault(d, MUSASHI_GPU_DMA2_CHAIN);
        if (!reserve(d, d->used + 1u + count)) return 0;
        for (uint32_t i = 0; i <= count; ++i)
            d->snapshot[d->used++] = ram_word(d, address + i * 4u);
        ++*nodes;
        *payload += count;
        next = tag & 0xffffffu;
        if (next == 0xffffffu) return 1;
        address = next;
    }
}
static int start(MusashiGpuDma2 *d, uint32_t value) {
    uint32_t status, nodes, payload, node, offset;
    uint64_t delay;
    size_t position = 0;
    if (value != DMA2_START || !(d->dma->control & 0x800u) ||
        d->gpu->dma_direction != 2u)
        return fault(d, MUSASHI_GPU_DMA2_ACCESS);
    if (!musashi_gpu_controller_read32(d->gpu, MUSASHI_GPU_GPUSTAT, &status) ||
        d->violation || d->gpu->faulted)
        return fault(d, MUSASHI_GPU_DMA2_BACKEND);
    /* DMA feeds GP0 parameters as well as new commands. Bit26 is low while
     * a partial packet awaits its next word; direction2 requests follow28. */
    if ((status & 0x12000000u) != 0x12000000u)
        return fault(d, MUSASHI_GPU_DMA2_BACKEND);
    if (!snapshot_chain(d, &nodes, &payload)) return 0;
    delay = 1u + (uint64_t)nodes + payload;
    if (d->state.cycle > UINT64_MAX - delay || d->state.starts == UINT64_MAX ||
        d->state.transfers == UINT64_MAX ||
        d->state.fetched_words > UINT64_MAX - payload ||
        d->state.accepted_words > UINT64_MAX - payload ||
        d->gpu->accepted_gp0_words > UINT64_MAX - payload)
        return fault(d, MUSASHI_GPU_DMA2_OVERFLOW);
    d->state.start_madr = d->state.madr;
    d->state.nodes = d->state.header_words = nodes;
    d->state.payload_words = payload;
    d->state.accepted_in_transfer = 0;
    d->state.fetched_words += payload;
    d->state.due = d->state.cycle + delay;
    ++d->state.starts;
    d->dma->gpu_channel_control = value;
    d->pending = 1;
    node = d->state.madr;
    while (position < d->used) {
        uint32_t tag = d->snapshot[position++];
        uint32_t count = tag >> 24;
        d->state.madr = node;
        for (offset = 0; offset < count; ++offset) {
            uint64_t before = d->gpu->accepted_gp0_words;
            uint64_t after;
            int accepted;
            d->state.fault_node = node;
            d->state.fault_word = node + 4u + offset * 4u;
            accepted = musashi_gpu_controller_write32(d->gpu, MUSASHI_GPU_GP0,
                                                       d->snapshot[position++]);
            after = d->gpu->accepted_gp0_words;
            /* The GPU's monotonic acceptance ledger survives its own reentry
             * fault. Preserve the actual last accepted word even on return0. */
            if (after == before + 1u) {
                ++d->state.accepted_in_transfer;
                ++d->state.accepted_words;
            }
            if (!accepted || after != before + 1u || d->violation || d->gpu->faulted) {
                fprintf(stderr, "gpu_dma2: GP0 refused node=%08x word=%08x value=%08x "
                        "opcode=%02x fill_words=%u store_phase=%u accepted=%d\n",
                        node, node + 4u + offset * 4u, d->snapshot[position - 1u],
                        d->snapshot[position - 1u] >> 24, d->gpu->fill_words,
                        d->gpu->store_phase, accepted);
                return fault(d, MUSASHI_GPU_DMA2_BACKEND);
            }
        }
        node = tag & 0xffffffu;
    }
    d->state.madr = node;
    d->state.fault_node = d->state.fault_word = 0;
    return 1;
}
static int start_block(MusashiGpuDma2 *d, uint32_t value) {
    uint32_t status, block, count, words, physical, offset;
    uint64_t delay;
    if (value != DMA2_BLOCK_START || !(d->dma->control & 0x800u) ||
        d->gpu->dma_direction != 2u)
        return fault(d, MUSASHI_GPU_DMA2_ACCESS);
    block = d->state.bcr & 0xffffu;
    count = d->state.bcr >> 16;
    if (!block || !count || block > DMA2_WORD_LIMIT ||
        count > DMA2_WORD_LIMIT / block)
        return fault(d, MUSASHI_GPU_DMA2_ACCESS);
    words = block * count;
    physical = d->state.madr;
    if ((physical & 3u) || physical / 4u > DMA2_WORD_LIMIT - words)
        return fault(d, MUSASHI_GPU_DMA2_ACCESS);
    if (!musashi_gpu_controller_read32(d->gpu, MUSASHI_GPU_GPUSTAT, &status) ||
        d->violation || d->gpu->faulted)
        return fault(d, MUSASHI_GPU_DMA2_BACKEND);
    /* Block CPU-to-GP0 continues a partial packet; command-ready (bit26) is low. */
    if (!(status & 0x10000000u))
        return fault(d, MUSASHI_GPU_DMA2_BACKEND);
    if (d->capacity < words) {
        uint32_t *grown = realloc(d->snapshot, words * sizeof(*grown));
        if (!grown) return fault(d, MUSASHI_GPU_DMA2_RESOURCE);
        d->snapshot = grown;
        d->capacity = words;
    }
    for (offset = 0; offset < words; ++offset)
        d->snapshot[offset] = ram_word(d, physical + offset * 4u);
    delay = 1u + words;
    if (d->state.cycle > UINT64_MAX - delay || d->state.starts == UINT64_MAX ||
        d->state.transfers == UINT64_MAX ||
        d->state.fetched_words > UINT64_MAX - words ||
        d->state.accepted_words > UINT64_MAX - words ||
        d->gpu->accepted_gp0_words > UINT64_MAX - words)
        return fault(d, MUSASHI_GPU_DMA2_OVERFLOW);
    d->state.start_madr = d->state.madr;
    d->state.nodes = 1;
    d->state.header_words = 0;
    d->state.payload_words = words;
    d->state.accepted_in_transfer = 0;
    d->state.fetched_words += words;
    d->state.due = d->state.cycle + delay;
    ++d->state.starts;
    d->dma->gpu_channel_control = value; /* block busy until advance */
    d->pending = 1;
    d->used = words;
    for (offset = 0; offset < words; ++offset) {
        uint64_t before = d->gpu->accepted_gp0_words;
        uint64_t after;
        int accepted;
        d->state.fault_node = physical;
        d->state.fault_word = physical + offset * 4u;
        accepted = musashi_gpu_controller_write32(d->gpu, MUSASHI_GPU_GP0,
                                                   d->snapshot[offset]);
        after = d->gpu->accepted_gp0_words;
        if (after == before + 1u) {
            ++d->state.accepted_in_transfer;
            ++d->state.accepted_words;
        }
        if (!accepted || after != before + 1u || d->violation ||
            d->gpu->faulted)
            return fault(d, MUSASHI_GPU_DMA2_BACKEND);
    }
    d->state.madr = physical + words * 4u;
    d->state.fault_node = d->state.fault_word = 0;
    return 1;
}
MusashiGpuDma2 *musashi_gpu_dma2_open(MusashiBootMemory *memory,
    MusashiDmaController *dma, MusashiGpuController *gpu,
    const MusashiGpuDma2Device *device, uint64_t epoch) {
    MusashiGpuDma2 *d;
    uint32_t status;
    if (!memory || !dma || !dma->irq || dma->gpu_owner || !gpu ||
        !device || !device->current_thread || !device->healthy ||
        (dma->gpu_channel_control != 0 && dma->gpu_channel_control != 0x401u))
        return NULL;
    d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->memory = memory; d->dma = dma; d->gpu = gpu; d->device = *device;
    d->state.cycle = epoch;
    d->entered = 1;
    dma->gpu_owner = d;
    d->thread = d->device.current_thread(d->device.userdata);
    if (!d->thread || d->violation || !d->device.healthy(d->device.userdata) ||
        d->violation || dma->gpu_owner != d ||
        !musashi_gpu_controller_read32(gpu, MUSASHI_GPU_GPUSTAT, &status) ||
        gpu->faulted || d->violation || dma->gpu_owner != d) {
        if (dma->gpu_owner == d) dma->gpu_owner = NULL;
        free(d);
        return NULL;
    }
    d->entered = 0;
    return d;
}
int musashi_gpu_dma2_read32(void *userdata, uint32_t address, uint32_t *out) {
    MusashiGpuDma2 *d = userdata;
    uint32_t value;
    if (!enter(d, 0)) return 0;
    if (!out) return leave(d, fault(d, MUSASHI_GPU_DMA2_ACCESS));
    switch (address) {
    case DMA2_MADR: value = d->state.madr; break;
    case DMA2_BCR: value = d->state.bcr; break;
    case DMA2_CHCR: value = d->dma->gpu_channel_control; break;
    default: return leave(d, fault(d, MUSASHI_GPU_DMA2_ACCESS));
    }
    *out = value;
    return leave(d, 1);
}
int musashi_gpu_dma2_write32(void *userdata, uint32_t address, uint32_t value) {
    MusashiGpuDma2 *d = userdata;
    uint32_t physical;
    if (!enter(d, 0)) return 0;
    if (d->dma->gpu_channel_control & DMA2_BUSY)
        return leave(d, fault(d, MUSASHI_GPU_DMA2_ACCESS));
    switch (address) {
    case DMA2_MADR:
        if (!ram_address(value, &physical)) return leave(d, fault(d, MUSASHI_GPU_DMA2_ACCESS));
        d->state.madr = physical;
        break;
    case DMA2_BCR: d->state.bcr = value; break;
    case DMA2_CHCR:
        if (value == 0 || value == 0x401u) d->dma->gpu_channel_control = value;
        else if (value == DMA2_BLOCK_START) return leave(d, start_block(d, value));
        else return leave(d, start(d, value));
        break;
    default: return leave(d, fault(d, MUSASHI_GPU_DMA2_ACCESS));
    }
    return leave(d, 1);
}
int musashi_gpu_dma2_advance(MusashiGpuDma2 *d, uint64_t cycle) {
    if (!enter(d, 0)) return 0;
    if (cycle < d->state.cycle) return leave(d, fault(d, MUSASHI_GPU_DMA2_CLOCK));
    if (d->pending && d->dma->gpu_channel_control != DMA2_START &&
        d->dma->gpu_channel_control != DMA2_BLOCK_START)
        return leave(d, fault(d, MUSASHI_GPU_DMA2_ACCESS));
    if (d->pending && cycle >= d->state.due) {
        d->state.cycle = d->state.due;
        d->dma->gpu_channel_control &= ~DMA2_BUSY;
        d->pending = 0;
        ++d->state.transfers;
        /* No external callback between accepted completion and IRQ latch. */
        if (!musashi_dma_controller_complete(d->dma, 2))
            return leave(d, fault(d, MUSASHI_GPU_DMA2_BACKEND));
    }
    d->state.cycle = cycle;
    return leave(d, 1);
}
int musashi_gpu_dma2_get_state(const MusashiGpuDma2 *owner, MusashiGpuDma2State *out) {
    MusashiGpuDma2 *d = (MusashiGpuDma2 *)owner;
    if (!enter(d, 1)) return 0;
    if (!out) { d->entered = 0; return 0; }
    *out = d->state;
    out->chcr = d->dma->gpu_channel_control;
    d->entered = 0;
    return 1;
}
int musashi_gpu_dma2_close(MusashiGpuDma2 *d) {
    if (!enter(d, 1)) return 0;
    d->dma->gpu_owner = NULL;
    free(d->snapshot);
    free(d);
    return 1;
}
