#ifndef MUSASHI_CD_CONTROLLER_H
#define MUSASHI_CD_CONTROLLER_H
#include <stdint.h>
#include <stddef.h>
struct MusashiDiscMedia;

/* Startup command subset. Timing follows the locally inspected PCSX-Redux
 * cdrom.cc model; retail timing parity and disc reads are not established. */
typedef struct MusashiCdController {
    uint64_t cycle, due;
    uint8_t index, enable, interrupt, response, response_ready;
    uint8_t drive_status, command, phase, busy, muted, mode;
    /* LL, LR, RL, RR: source channel followed by destination channel. */
    uint8_t volume_pending[4], volume[4];
} MusashiCdController;

/* drive_status must come from the caller's media/drive state. */
void musashi_cd_controller_init(MusashiCdController *cd, uint8_t drive_status);
int musashi_cd_controller_advance(MusashiCdController *cd, uint64_t cycle);
int musashi_cd_controller_read8(void *userdata, uint32_t address, uint8_t *value);
int musashi_cd_controller_write8(void *userdata, uint32_t address, uint8_t value);
int musashi_cd_controller_irq(const MusashiCdController *cd);

/* Separately owned native profile. Media is an immutable pinned snapshot and
 * must outlive this object. Fresh native policy: closed lid, standby/rotating,
 * empty FIFOs, BIOS internal enable 1f. This is not captured BIOS phase/state. */
typedef struct MusashiCdOwned MusashiCdOwned;
typedef struct MusashiCdOwnedHardware {
    void *userdata;
    uint64_t (*current_thread)(void *userdata); /* Zero is invalid/unavailable. */
    int (*healthy)(void *userdata);
    /* Latch CPU I_STAT mask 0x0004 (bit 2, CD-ROM) only.
     * Must not dispatch guest code or acknowledge. */
    int (*raise_irq4)(void *userdata);
    /* Required only for SeekL: query the actual DMA3 owner, without dispatch.
     * Return 1 with idle=0/1; refusal or reentry faults this owner. */
    int (*data_transfer_idle)(void *userdata, int *idle);
} MusashiCdOwnedHardware;

typedef enum MusashiCdOwnedFault {
    MUSASHI_CD_OWNED_OK = 0,
    MUSASHI_CD_OWNED_BAD_ACCESS,
    MUSASHI_CD_OWNED_WRONG_THREAD,
    MUSASHI_CD_OWNED_BACKEND_LOST,
    MUSASHI_CD_OWNED_MEDIA_LOST,
    MUSASHI_CD_OWNED_CLOCK_BACKWARDS,
    MUSASHI_CD_OWNED_OVERFLOW,
    MUSASHI_CD_OWNED_REENTRY,
    MUSASHI_CD_OWNED_FIFO_OVERRUN
} MusashiCdOwnedFault;

typedef struct MusashiCdOwnedState {
    uint64_t cycle, due;
    uint8_t index, enable, interrupt, response_count;
    uint8_t parameter_count;
    uint8_t command, phase, busy, muted, mode;
    uint8_t lid_closed, rotating, standby;
    uint8_t volume_pending[4], volume[4]; /* LL, LR, RL, RR */
    /* All sector times share cycle's original epoch; zero means unscheduled. */
    uint64_t sector_due, sector_publish_due;
    uint64_t sectors_fetched, sectors_published, data_bytes_read;
    uint32_t pending_lba, next_lba, fifo_lba, incoming_lba;
    uint16_t fifo_size, fifo_cursor;
    uint8_t location_valid, reading, data_requested, fifo_valid, incoming_valid;
    MusashiCdOwnedFault fault;
    /* Announcement is independent of the BFRD-locked requested FIFO above. */
    uint64_t announced_sequence, requested_sequence, fifo_latches;
    uint64_t seek_starts, seek_completions, seek_sector_reads, buffer_retirements;
    uint32_t announced_lba, seek_target_lba, position_lba;
    uint8_t announced_valid, announced_claimed, seeking, position_valid;
    uint64_t xa_sectors, xa_frames;
    /* Setfilter (0D) file/channel; applied while mode bit 3 is set. */
    uint8_t filter_file, filter_channel;
    uint64_t xa_filtered;
} MusashiCdOwnedState;

MusashiCdOwned *musashi_cd_owned_open_bios_idle(
    const struct MusashiDiscMedia *media, const MusashiCdOwnedHardware *hardware,
    uint64_t epoch);
/* Bind a copied44100Hz stereo PCM sink before streaming. first_frame is
 * relative to the current read, reset to0 on a new read. No callback may reenter
 * the drive. Observed4-bit/stereo37800Hz XA only; other coding values refuse. */
typedef int (*MusashiXaPcmSink)(void *, uint64_t cycle, uint64_t first_frame,
                               const int16_t *pcm, size_t frames);
int musashi_cd_owned_set_xa_sink(MusashiCdOwned *, MusashiXaPcmSink, void *);
/* Calls are owner-thread-only, with no recursive calls from hardware callbacks.
 * Refusal is sticky and preserves read/PCM output. Reads never advance time.
 * Advance services deadlines chronologically; blocked events retry every256
 * cycles, including an unclaimed announcement (bounded no-overwrite policy).
 * GetTN/GetTD return three-byte INT3 packets from the pinned media's INDEX01
 * and lead-out metadata. Invalid parameters refuse before command acceptance;
 * hardware error packets, media-changing and audio-producing commands remain
 * unsupported. Command latency remains the declared 0x800-cycle source model.
 * Data subset: Setloc02, Setfilter0D and Setmode0e(A0/C0, each optionally with the
 * XA filter bit 08) including during reads, ReadN06/ReadS1B,
 * Pause09 and SeekL15 (SeekL retires the ReadN buffers). Setloc/Setmode do
 * not cancel an active ReadN. A new read retires idle prior buffers at ACK;
 * an active DMA view refuses retirement. Unread bytes are not consumption.
 * A0 exposes2340 bytes; C0 data sectors expose2048 payload bytes.
 * C0 XA audio/realtime sectors use the bound PCM sink for4-bit stereo37800Hz;
 * missing sinks and other XA coding formats refuse.
 * With the filter bit, XA audio sectors of another file/channel are skipped at
 * sector timing: neither played nor delivered (xa_filtered counts them).
 * Seek requires Setloc/A0-or-C0 and actual data_transfer_idle capability. At ACK it
 * retires all old data views without consumption; completion validates the
 * actual mounted target header at ACK+1806336, then commits position/INT2.
 * ACK snapshots pre-seek status; BUSYSTS clears while seeking remains set.
 * First sector is
 * ACK+451584, then225792 cycles; active/idle Pause completion ACK+1000000/7400.
 * These are declared native timings, not physical seek/bus-cycle parity.
 * One arriving, one announced and one requested real-byte buffer: INT1 may
 * announce a newer sector while requested bytes remain locked. Request00
 * releases that view; the next80 copies the announced bytes and resets cursor.
 * Repeated80 preserves the requested identity/cursor. Announcement alone does
 * not set fifo_valid/DRQ. An unclaimed announcement is retained across ACK;
 * required incoming overwrite faults rather than emulating hardware skipping.
 * Pause retains announced/requested views. Physical seek/FIFO latency and
 * capacity parity remain unproven; recovery/other modes refuse. */
int musashi_cd_owned_advance(MusashiCdOwned *, uint64_t absolute_cycle);
/* Direct advance still faults on incoming overwrite. Host epoch cuts can jump
 * past more than one sector deadline; clamp to the last cycle that keeps the
 * one-incoming buffer instead of asking owned_sector to overwrite. */
uint64_t musashi_cd_owned_bounded_cycle(const MusashiCdOwnedState *, uint64_t requested);
/* Service command/publish deadlines without fetching a sector. Direct
 * owned_advance still faults on incoming overwrite. */
int musashi_cd_owned_advance_without_fetch(MusashiCdOwned *, uint64_t absolute_cycle);
int musashi_cd_owned_read8(void *, uint32_t address, uint8_t *value);
int musashi_cd_owned_write8(void *, uint32_t address, uint8_t value);
/* Consume only real requested sector bytes, without advancing time. Caller
 * supplies stable valid nonoverlapping storage. Complete validation precedes
 * output/cursor commit; refusal preserves both and faults the owner. Checked
 * get_state exposes data_requested/fifo_valid/fifo_size/fifo_cursor for DMA
 * preflight. No internal buffer pointer or callback is exposed. */
int musashi_cd_owned_read_data(MusashiCdOwned *, void *destination, size_t byte_count);
/* Production matrix operation for future CD producers: stereo signed PCM,
 * original L/R used for both outputs; in-place allowed, partial overlap refused.
 * Does not apply SPU CD/main gains, generate samples, or advance device time. */
int musashi_cd_owned_mix_pcm(MusashiCdOwned *, const int16_t *input_lr,
                             int16_t *output_lr, size_t frames);
/* Diagnostic snapshot, callable on owner thread even after a sticky fault. */
int musashi_cd_owned_get_state(const MusashiCdOwned *, MusashiCdOwnedState *);
/* Cleanup remains available after faults, but refuses wrong thread/reentry. */
int musashi_cd_owned_close(MusashiCdOwned *);
#endif
