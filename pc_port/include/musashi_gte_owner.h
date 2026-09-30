#ifndef MUSASHI_GTE_OWNER_H
#define MUSASHI_GTE_OWNER_H

#include <stdint.h>

#include "musashi_cpu_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MUSASHI_GTE_DATA_COUNT 32u
#define MUSASHI_GTE_CONTROL_COUNT 32u
#define MUSASHI_GTE_CONTROL_FIRST 21u
#define MUSASHI_GTE_CONTROL_LAST 30u
#define MUSASHI_GTE_CONTROL_MATRIX_LAST 4u
#define MUSASHI_GTE_CONTROL_BACKGROUND_FIRST 13u
#define MUSASHI_GTE_CONTROL_BACKGROUND_LAST 15u
#define MUSASHI_GTE_CONTROL_LIGHTCOLOR_FIRST 16u
#define MUSASHI_GTE_CONTROL_LIGHTCOLOR_LAST 20u
#define MUSASHI_GTE_CONTROL_LIGHT_READ 11u
#define MUSASHI_CPU_STATUS_CU2 0x40000000u

typedef uint64_t (*MusashiGteThreadToken)(void *userdata);

/* Checked evidence copy of the shared PsyCross CP2 control bank.  This is a
 * read-only diagnostic value; it is not a second register bank. */
typedef struct MusashiGteSnapshot {
    uint32_t control[MUSASHI_GTE_CONTROL_COUNT];
    uint64_t write_count;
    int initialized;
    int faulted;
    uint32_t data[MUSASHI_GTE_DATA_COUNT];
    uint64_t data_write_count;
    uint64_t data_read_count;
    uint64_t command_count;
} MusashiGteSnapshot;

/* Caller-owned, fresh-zero storage.  The owner leases the process-wide
 * PsyCross gteRegs bank until close; it never clears that bank at init/close. */
typedef struct MusashiGteOwner {
    MusashiCpuStatus *cpu_status;
    MusashiGteThreadToken current_thread;
    void *thread_userdata;
    uint64_t owner_thread;
    const void *context_identity;
    uint64_t write_count;
    int initialized;
    int executing;
    int faulted;
    int reentry_violation;
    uint64_t data_write_count;
    uint64_t data_read_count;
    uint64_t command_count;
} MusashiGteOwner;

/* context_identity must remain stable for the lease.  The owner accepts only
 * source contexts with that identity, and only while CP0 Status has CU2 set. */
int musashi_gte_owner_init(MusashiGteOwner *owner,
                           MusashiCpuStatus *cpu_status,
                           MusashiGteThreadToken current_thread,
                           void *thread_userdata,
                           const void *context_identity);

/* This is the bounded CTC2 profile: rotation-matrix controls 0..4, translation 5..7,
 * background-color controls 13..15, light-color controls 16..20, plus
 * controls 21..30.  The transfer uses PsyCross's existing CTC2
 * implementation and shared gteRegs bank. */
int musashi_gte_owner_write_control(MusashiGteOwner *owner,
                                    const MusashiCpuContext *context,
                                    uint32_t selector, uint32_t value);

/* Bounded CFC2 of the same control profile: rotation-matrix controls 0..4, translation 5..7,
 * background-color controls 13..15, light-color controls 16..20, plus
 * controls 21..30, via PsyCross's existing CFC2 reader. The retail
 * 80048E98 SWC2 also reads light-matrix control 11, so reads (only reads)
 * admit that selector and FLAG31; CTC2 writes to 11 and 31 stay refused. Stores like
 * SWC2 compose this read with a RAM write at the execution layer; the owner
 * never touches RAM. Refusal leaves the output untouched. */
int musashi_gte_owner_read_control(MusashiGteOwner *owner,
                                   const MusashiCpuContext *context,
                                   uint32_t selector, uint32_t *value);

/* Bounded data transfers: write V0/V1 (0/1) and IR1/IR2/IR3 (9/10/11)
 * plainly, plus LZCS (30); read IR1/IR2/IR3 (9/10/11) and MAC1/MAC2/MAC3
 * (25/26/27) plainly, plus LZCS/LZCR (30/31).
 * LZCS retains the original word and LZCR counts leading sign bits, including
 * all-zero/all-one results of 32. All use the canonical PsyCross data bank.
 * Data contexts require SOURCE provenance, the lease identity, live GPRs and
 * a valid instruction. These calls do not implement CPU load delay or time.
 * Refusal preserves data, counters and read output; reentry faults the owner.
 * No fallible callback occurs after a register commit. */
int musashi_gte_owner_write_data(MusashiGteOwner *owner,
                                const MusashiCpuContext *context,
                                uint32_t selector, uint32_t value);
int musashi_gte_owner_read_data(MusashiGteOwner *owner,
                               const MusashiCpuContext *context,
                               uint32_t selector, uint32_t *value);

/* Bounded GTE command dispatch (MVMVA and its kin): runs PsyCross's existing
 * GTE operator against the shared bank under the same lease, thread, CU2 and
 * reentry rules as data transfers. Only exact admitted command words reach
 * here; an unhandled function code refuses without touching the bank. */
int musashi_gte_owner_command(MusashiGteOwner *owner,
                              const MusashiCpuContext *context,
                              uint32_t word);

/* Copies the whole shared control and data banks without advancing time or changing
 * GTE state.  A faulted but still-live owner remains inspectable. */
/* Native-lane GTE lease for host libgte wrappers (see gte_owner.c). */
int musashi_gte_owner_native_begin(MusashiGteOwner *owner);
void musashi_gte_owner_native_end(MusashiGteOwner *owner, uint32_t commands);

int musashi_gte_owner_snapshot(MusashiGteOwner *owner,
                               MusashiGteSnapshot *snapshot);

/* Teardown is permitted after an old fault, but not from a foreign thread or
 * a callback-reentrant operation.  The shared gteRegs values are preserved. */
int musashi_gte_owner_close(MusashiGteOwner *owner);

#ifdef __cplusplus
}
#endif

#endif
