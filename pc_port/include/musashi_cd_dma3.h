#ifndef MUSASHI_CD_DMA3_H
#define MUSASHI_CD_DMA3_H

#include <stddef.h>
#include <stdint.h>

struct MusashiBootMemory;
struct MusashiDmaController;
typedef struct MusashiCdDma3 MusashiCdDma3;
typedef struct MusashiCdDma3Device {
    void *userdata;
    uint64_t (*current_thread)(void *); /* Nonzero owner identity. */
    int (*healthy)(void *);
    int (*data_available)(void *, size_t *byte_count); /* Checked, non-consuming. */
    /* Atomic FIFO consume: return1 only after copying all requested bytes.
     * Refusal preserves FIFO and destination. No time advance/guest dispatch.
     * The owner supplies a private staging buffer, never a guest pointer. */
    int (*read_data)(void *, uint8_t *destination, size_t byte_count);
    /* Checked DREQ snapshot: return1 and write exactly0 or1. No side effects. */
    int (*request_asserted)(void *, int *asserted);
} MusashiCdDma3Device;

typedef enum MusashiCdDma3Fault {
    MUSASHI_CD_DMA3_OK = 0,
    MUSASHI_CD_DMA3_ACCESS,
    MUSASHI_CD_DMA3_THREAD,
    MUSASHI_CD_DMA3_BACKEND,
    MUSASHI_CD_DMA3_CLOCK,
    MUSASHI_CD_DMA3_OVERFLOW,
    MUSASHI_CD_DMA3_REENTRY
} MusashiCdDma3Fault;

typedef struct MusashiCdDma3State {
    uint64_t cycle, due, transfers, bytes;
    uint32_t madr, bcr, chcr, cdrom_delay;
    MusashiCdDma3Fault fault;
    unsigned waiting_for_request;
} MusashiCdDma3State;

/* Fresh unconfigured DELAY=0; source must write20943 before starting.
 * Checked profile: BCRlow16 is1..585 words (high16 ignored in mode0),
 * CHCR11000000, or streaming11400100 with DELAY21020843; DPCRbit15 enabled.
 * Accept physical/KSEG0/KSEG1 aliases strictly inside2MiB, aligned, no wrapping.
 * MADR reads expose low24bits; source mode0 leaves MADR/BCR unchanged.
 *
 * Native deadline policy: one cycle/word, from the inspected PCSX-Redux
 * cdrom.cc completion schedule. Actual bytes commit atomically AT the deadline,
 * unlike that model's immediate copy/deferred completion. Physical DMA bus
 * arbitration, stalls, and exact reference timing are NOT proven.
 * Chopped11400100 commits1 word then leaves16 native cycles for CPU; it updates
 * MADR/BCR and raises completion only after the last real word. Deasserted
 * CD request pauses chopped DMA without copying or completing; resumption
 * starts at the next observed cycle with no accumulated transfer credit.
 * Bit28 clears on accepted start; bit24 remains busy until the actual copy.
 * RAM, shared DMA/IRQ controller and device must outlive this owner. */
MusashiCdDma3 *musashi_cd_dma3_open(struct MusashiBootMemory *,
    struct MusashiDmaController *, const MusashiCdDma3Device *, uint64_t epoch);
int musashi_cd_dma3_read32(void *, uint32_t address, uint32_t *value);
int musashi_cd_dma3_write32(void *, uint32_t address, uint32_t value);
int musashi_cd_dma3_advance(MusashiCdDma3 *, uint64_t absolute_cycle);
/* Sticky refusal; diagnostic snapshot and teardown remain available on owner.
 * If a violating callback accepted a FIFO consume, retain its actual RAM copy
 * and completion even while returning refusal. Never retry accepted bytes.
 * Close cancels outstanding unconsumed work, without completion/guest effects. */
int musashi_cd_dma3_get_state(const MusashiCdDma3 *, MusashiCdDma3State *);
int musashi_cd_dma3_close(MusashiCdDma3 *);

#endif
