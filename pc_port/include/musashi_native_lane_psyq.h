#ifndef MUSASHI_NATIVE_LANE_PSYQ_H
#define MUSASHI_NATIVE_LANE_PSYQ_H

/* port-plat's PsyQ compat table as native-lane entries (native_lane_psyq.c). */

#include <stddef.h>
#include <stdint.h>

#include "musashi_boot_memory.h"
#include "musashi_gte_owner.h"
#include "musashi_native_lane.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MUSASHI_PSYQ_LANE_PURE = 1u, MUSASHI_PSYQ_LANE_DEVICE = 2u };

/* Classes selected by MUSASHI_PSYQ_LANE (unset/pure, all, 0). */
unsigned musashi_native_lane_psyq_classes(void);
int musashi_native_lane_psyq_is_device(const char *name);
/* Reason a table entry is not installed (retail mismatch), or NULL. */
const char *musashi_native_lane_psyq_held_back(uint32_t pc);
/* Non-STUB entries of the given classes; returns the total (writes <= cap). */
size_t musashi_native_lane_psyq_entries(MusashiNativeLaneEntry *out, size_t cap,
                                        unsigned classes);
/* malloc'd base + PsyQ entries (the lane keeps pointers: never freed). */
MusashiNativeLaneEntry *musashi_native_lane_psyq_merge(const MusashiNativeLaneEntry *base,
                                                       size_t base_count, unsigned classes,
                                                       size_t *count);
/* Guest RAM/scratchpad for the wrappers and the GTE bridge over gte (NULL:
 * GTE-state wrappers refuse). memory NULL removes the bridge. */
void musashi_native_lane_psyq_bind(MusashiBootMemory *memory, MusashiGteOwner *gte);

#ifdef __cplusplus
}
#endif

#endif
