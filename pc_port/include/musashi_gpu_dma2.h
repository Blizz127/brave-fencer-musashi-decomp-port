#ifndef MUSASHI_GPU_DMA2_H
#define MUSASHI_GPU_DMA2_H
#include <stdint.h>

struct MusashiBootMemory;
struct MusashiDmaController;
struct MusashiGpuController;
typedef struct MusashiGpuDma2 MusashiGpuDma2;
typedef struct MusashiGpuDma2Device {
    void *userdata;
    uint64_t (*current_thread)(void *); /* Nonzero installing identity. */
    int (*healthy)(void *); /* Actual borrowed devices/context are alive. */
} MusashiGpuDma2Device;
typedef enum MusashiGpuDma2Fault {
    MUSASHI_GPU_DMA2_OK = 0,
    MUSASHI_GPU_DMA2_ACCESS,
    MUSASHI_GPU_DMA2_THREAD,
    MUSASHI_GPU_DMA2_BACKEND,
    MUSASHI_GPU_DMA2_CLOCK,
    MUSASHI_GPU_DMA2_OVERFLOW,
    MUSASHI_GPU_DMA2_REENTRY,
    MUSASHI_GPU_DMA2_CHAIN,
    MUSASHI_GPU_DMA2_RESOURCE
} MusashiGpuDma2Fault;
typedef struct MusashiGpuDma2State {
    uint64_t cycle, due, starts, transfers;
    uint64_t fetched_words, accepted_words; /* Cumulative payload, not headers. */
    uint32_t madr, bcr, chcr; /* chcr copied from the sole shared DMA register. */
    uint32_t start_madr, nodes, header_words, payload_words;
    uint32_t accepted_in_transfer, fault_node, fault_word;
    MusashiGpuDma2Fault fault;
} MusashiGpuDma2State;

/* Borrows real RAM, shared DPCR/DICR/CHCR, GPU controller and device lifetime.
 * Reserve the shared GPU lease before any external callback. No active adoption:
 * open only against idle CHCR0/401 at the original native device epoch.
 * Native/legacy routes must use this owner while its lease is held.
 * RAM and all borrowed devices must outlive checked close.
 *
 * General acyclic linked lists, physical/KSEG0/KSEG1 starts strictly inside2MiB,
 * aligned nodes/payload spans, terminalFFFFFF, header count0..255. Total fetched
 * header+payload capped at524288 words as an explicit native resource bound.
 * Structural preflight/snapshot precedes all GP0 effects. Arbitrary BCR latches;
 * linked-list length comes ONLY from RAM headers. Other modes/invalid mirrors,
 * mid-transfer reprogramming, DPCR-disabled/DREQ-unready starts refuse.
 *
 * Pinned PCSX-Redux gpu.cc model: submit actual ordered GP0 words synchronously
 * at start; schedule completion at cycle+1+headers+payload. First6-word packet
 * costs8 model cycles. This is NOT physical arbitration/stalls/FIFO/cycle parity.
 * Accepted words persist on later failure; no incomplete stream completes.
 * CHCR has ONE authority in the shared DMA controller. Completion clears only
 * busy, leaving401, MADRFFFFFF and BCR unchanged; IRQ uses shared DMA channel2.
 */
MusashiGpuDma2 *musashi_gpu_dma2_open(struct MusashiBootMemory *,
    struct MusashiDmaController *, struct MusashiGpuController *,
    const MusashiGpuDma2Device *, uint64_t epoch);
int musashi_gpu_dma2_read32(void *, uint32_t address, uint32_t *value);
int musashi_gpu_dma2_write32(void *, uint32_t address, uint32_t value);
int musashi_gpu_dma2_advance(MusashiGpuDma2 *, uint64_t absolute_cycle);
/* Snapshot/close remain possible after sticky fault, on owner and nonreentrant.
 * Close cancels only unfinished completion; it never undoes accepted GPU state
 * or clears hardware busy to manufacture success. No reads advance time; no
 * owner callback may dispatch guest code. */
int musashi_gpu_dma2_get_state(const MusashiGpuDma2 *, MusashiGpuDma2State *);
int musashi_gpu_dma2_close(MusashiGpuDma2 *);
#endif
