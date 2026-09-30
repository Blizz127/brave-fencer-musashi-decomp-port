#ifndef MUSASHI_DMA_OTC_H
#define MUSASHI_DMA_OTC_H

#include <stdint.h>

struct MusashiBootMemory;
struct MusashiDmaController;
typedef struct MusashiDmaOtc MusashiDmaOtc;
typedef struct MusashiDmaOtcDevice {
    void *userdata;
    uint64_t (*current_thread)(void *);
    int (*healthy)(void *);
} MusashiDmaOtcDevice;

typedef enum MusashiDmaOtcFault {
    MUSASHI_DMA_OTC_OK = 0,
    MUSASHI_DMA_OTC_ACCESS,
    MUSASHI_DMA_OTC_THREAD,
    MUSASHI_DMA_OTC_BACKEND,
    MUSASHI_DMA_OTC_OVERFLOW,
    MUSASHI_DMA_OTC_REENTRY
} MusashiDmaOtcFault;

typedef struct MusashiDmaOtcState {
    uint64_t cycle, transfers, words;
    uint32_t madr, bcr, chcr;
    MusashiDmaOtcFault fault;
} MusashiDmaOtcState;

/* DMA6 OTC owner: MADR/BCR/CHCR at 1F8010E0/E4/E8. DPCR bit27 must be enabled.
 * CHCR 0 is idle reset. CHCR 11000002 walks BCR words backwards from MADR,
 * writing the previous address (last word 00FFFFFF), then clears busy.
 * Completes synchronously at start (reference policy, not bus-cycle parity).
 * RAM and the shared DMA controller must outlive this owner. */
MusashiDmaOtc *musashi_dma_otc_open(struct MusashiBootMemory *,
    struct MusashiDmaController *, const MusashiDmaOtcDevice *, uint64_t epoch);
int musashi_dma_otc_read32(void *, uint32_t address, uint32_t *value);
int musashi_dma_otc_write32(void *, uint32_t address, uint32_t value);
int musashi_dma_otc_get_state(const MusashiDmaOtc *, MusashiDmaOtcState *);
int musashi_dma_otc_close(MusashiDmaOtc *);

#endif
