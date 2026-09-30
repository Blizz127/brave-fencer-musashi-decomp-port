#ifndef BFM_PLAT_WATCH_H
#define BFM_PLAT_WATCH_H

/* Guest-memory watches and the events derived from them.
 *
 * A watch is a guest address range plus a callback that gets the old and
 * new bytes. Two ways to feed it:
 *
 *  write hook  The native lane / interpreter store path does
 *                if (bfm_plat_watch_hit(addr, len)) {
 *                    read old bytes; perform the store;
 *                    bfm_plat_watch_on_write(addr, len, old, new);
 *                }
 *              bfm_plat_watch_hit is a range check against the union of
 *              all watches (min/max + a 4 KiB-page bitmap), so the common
 *              case is two compares.
 *  poll        Until that hook exists, bfm_plat_watch_poll() (run at every
 *              FRAME_END when [mods] watch_poll = 1, the default) compares
 *              each watch with its last snapshot through the guest-memory
 *              binding. It sees at most one change per frame.
 *
 * Addresses are normalised to physical RAM (addr & 0x1FFFFF), so KSEG0,
 * KSEG1 and KUSEG mirrors are the same address. Each watch covers up to 64
 * bytes. */

#include "bfm_plat_types.h"

#define BFM_PLAT_WATCH_MAX 64
#define BFM_PLAT_WATCH_LEN_MAX 64

typedef void (*BfmPlatWatchFn)(void *user, uint32_t address, uint32_t len,
                               const uint8_t *old_bytes,
                               const uint8_t *new_bytes);

/* Returns an id > 0, or a negative BfmPlatResult. */
int bfm_plat_watch_add(uint32_t address, uint32_t len, BfmPlatWatchFn fn,
                       void *user);
int bfm_plat_watch_remove(int id);
size_t bfm_plat_watch_count(void);

int bfm_plat_watch_hit(uint32_t address, uint32_t len);
/* A store of `len` bytes at `address` happened (old/new as seen by the
 * store). Watches overlapping it fire with their whole range's old/new
 * contents; bytes outside the store come from the watch's snapshot. */
void bfm_plat_watch_on_write(uint32_t address, uint32_t len,
                             const void *old_bytes, const void *new_bytes);
/* Poll mode (see above). Returns how many watches fired. */
int bfm_plat_watch_poll(void);
/* Re-reads every watch's snapshot from guest RAM (after a load/restore). */
void bfm_plat_watch_resync(void);
void bfm_plat_watch_reset(void);

/* ---- Derived game events -----------------------------------------------
 * Built on the live-state addresses in bfm_plat_events.h (BFM_GUEST_*).
 * These are DERIVED: they come from data changes, not from a code site, and
 * carry BFM_EVENT_FLAG_DERIVED.
 *   damage     gauge-1 (HP) current drops         BfmEventStat
 *   bp_use     gauge-2 (BP) current drops         BfmEventStat
 *   money      money word changes                 BfmEventStat
 *   item_get   a figure's owned bit (0x40) sets   BfmEventItem (kind 1)
 * Returns the number of watches added. */
int bfm_plat_derived_events_enable(void);

#endif
