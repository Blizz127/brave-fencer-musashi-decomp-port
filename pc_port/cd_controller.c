#include "musashi_cd_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "musashi_disc_media.h"

void musashi_cd_controller_init(MusashiCdController *cd, uint8_t drive_status) {
    if (!cd) return;
    memset(cd, 0, sizeof(*cd));
    cd->drive_status = drive_status;
}

int musashi_cd_controller_irq(const MusashiCdController *cd) {
    return cd && (cd->enable & cd->interrupt) != 0;
}

int musashi_cd_controller_advance(MusashiCdController *cd, uint64_t cycle) {
    if (!cd || cycle < cd->cycle) return 0;
    cd->cycle = cycle;
    if (!cd->phase || cycle < cd->due || cd->interrupt || cd->response_ready)
        return 1;
    if (cd->phase == 1 && cd->command == 0x0a && cycle > UINT64_MAX - 4100000)
        return 0;
    cd->response = cd->drive_status;
    cd->response_ready = 1;
    cd->busy = 0;
    cd->interrupt = cd->phase == 2 ? 2 : 3;
    if (cd->phase == 1 && cd->command == 0x0a) {
        cd->muted = 0;
        cd->mode = 0x20;
        cd->phase = 2;
        cd->due = cycle + 4100000;
    } else {
        if (cd->command == 0x0c) cd->muted = 0;
        cd->phase = 0;
    }
    return 1;
}

int musashi_cd_controller_read8(void *userdata, uint32_t address, uint8_t *value) {
    MusashiCdController *cd = userdata;
    if (!cd || !value) return 0;
    switch (address) {
    case 0x1f801800u:
        *value = cd->index | 0x18 | (cd->response_ready ? 0x20 : 0) |
                 (cd->busy ? 0x80 : 0);
        return 1;
    case 0x1f801801u:
        if (!cd->response_ready) return 0;
        *value = cd->response;
        cd->response_ready = 0;
        return 1;
    case 0x1f801803u:
        *value = 0xe0 | ((cd->index & 1) ? cd->interrupt : cd->enable);
        return 1;
    default: return 0; /* Sector data reads are not implemented. */
    }
}

int musashi_cd_controller_write8(void *userdata, uint32_t address, uint8_t value) {
    MusashiCdController *cd = userdata;
    if (!cd) return 0;
    switch (address) {
    case 0x1f801800u:
        cd->index = value & 3;
        return 1;
    case 0x1f801801u:
        if (cd->index == 3) {
            cd->volume_pending[3] = value;
            return 1;
        }
        if (cd->index || cd->phase || cd->interrupt || cd->response_ready ||
            (value != 1 && value != 0x0a && value != 0x0c) ||
            cd->cycle > UINT64_MAX - 0x800) return 0;
        cd->command = value;
        cd->busy = 1;
        cd->phase = 1;
        cd->due = cd->cycle + 0x800;
        return 1;
    case 0x1f801802u:
        if (cd->index >= 2) {
            cd->volume_pending[cd->index == 2 ? 0 : 2] = value;
            return 1;
        }
        if (cd->index != 1) return 0; /* Parameter FIFO not implemented. */
        cd->enable = value & 0x1f;
        return 1;
    case 0x1f801803u:
        if (cd->index == 2) {
            cd->volume_pending[1] = value;
            return 1;
        }
        if (cd->index == 3) {
            if (value & 0x20)
                memcpy(cd->volume, cd->volume_pending, sizeof(cd->volume));
            return 1;
        }
        if (cd->index != 1) return 0;
        cd->interrupt &= (uint8_t)~value;
        return 1;
    default: return 0;
    }
}

/* Owned initialized drive, intentionally independent of the diagnostic above.
 * Source: PCSX-Redux 3e10093 cdrom.cc reset/interrupt/register methods.
 * Selected native timely-event profile; no emulator/build equivalence claim. */
#include <stdlib.h>
#include "musashi_xa_decode.h"

struct MusashiCdOwned {
    MusashiCdOwnedState state;
    const MusashiDiscMedia *media;
    MusashiCdOwnedHardware hardware;
    uint64_t owner_thread;
    uint8_t response_fifo[16], response_pos;
    uint8_t parameter_fifo[16], toc_result[2];
    uint8_t incoming_sector[2340], announced_sector[2340], data_fifo[2340];
    uint8_t pause_was_reading, first_sector_pending;
    int entered;
    MusashiXaDecode xa;
    MusashiXaPcmSink xa_sink;
    void *xa_userdata;
    uint64_t xa_stream_frames;
};

static int owned_fault(MusashiCdOwned *c, MusashiCdOwnedFault fault) {
    if (c && fault == MUSASHI_CD_OWNED_REENTRY && c->entered) c->entered = 2;
    if (c && c->state.fault == MUSASHI_CD_OWNED_OK) c->state.fault = fault;
    return 0;
}
static int owned_enter(MusashiCdOwned *c) {
    MusashiDiscMediaInfo info;
    if (!c || c->state.fault) return 0;
    if (c->entered) return owned_fault(c, MUSASHI_CD_OWNED_REENTRY);
    c->entered = 1;
    if (c->hardware.current_thread(c->hardware.userdata) != c->owner_thread)
        owned_fault(c, MUSASHI_CD_OWNED_WRONG_THREAD);
    if (!c->state.fault && !c->hardware.healthy(c->hardware.userdata))
        owned_fault(c, MUSASHI_CD_OWNED_BACKEND_LOST);
    if (!c->state.fault && !musashi_disc_media_get_info(c->media, &info))
        owned_fault(c, MUSASHI_CD_OWNED_MEDIA_LOST);
    if (!c->state.fault) return 1;
    c->entered = 0;
    return 0;
}
static int owned_leave(MusashiCdOwned *c) {
    c->entered = 0;
    return c->state.fault == MUSASHI_CD_OWNED_OK;
}
static int owned_irq(MusashiCdOwned *c) {
    if ((c->state.interrupt & c->state.enable) &&
        !c->hardware.raise_irq4(c->hardware.userdata))
        return owned_fault(c, MUSASHI_CD_OWNED_BACKEND_LOST);
    return !c->state.fault;
}

/* PSX-SPX CDROM Drive, GetTN/GetTD: INT3(stat,first,last) and INT3(stat,mm,ss).
 * https://psx-spx.consoledev.net/cdromdrive/#gettd-command-14htrack-int3statmmss-bcd
 * INDEX01 is relative to the BIN; physical MSF adds the 150-frame lead-in.
 * GetTD truncates the frame component. The local PCSX implementation's fourth
 * result byte is not part of this documented three-byte packet. */
static uint8_t owned_bcd(unsigned value) {
    return (uint8_t)((value / 10u) * 16u + value % 10u);
}
static int owned_toc_result(MusashiCdOwned *c, uint8_t command, uint8_t result[2]) {
    MusashiDiscMediaInfo info;
    unsigned track;
    uint64_t frame;
    if (!musashi_disc_media_get_info(c->media, &info) || !info.track_count ||
        info.track_count > sizeof(info.tracks)/sizeof(info.tracks[0]))
        return owned_fault(c, MUSASHI_CD_OWNED_MEDIA_LOST);
    if (command == 0x13) {
        result[0] = owned_bcd(info.tracks[0].number);
        result[1] = owned_bcd(info.tracks[info.track_count-1].number);
        return 1;
    }
    track = c->parameter_fifo[0];
    if ((track & 15u) > 9u || (track >> 4) > 9u)
        return owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
    track = (track >> 4) * 10u + (track & 15u);
    if (track > info.track_count || (track && info.tracks[track-1].number != track))
        return owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
    frame = (uint64_t)(track ? info.tracks[track-1].index01_frame : info.total_frames) + 150u;
    if (frame / 4500u > 99u) return owned_fault(c, MUSASHI_CD_OWNED_OVERFLOW);
    result[0] = owned_bcd((unsigned)(frame / 4500u));
    result[1] = owned_bcd((unsigned)(frame / 75u % 60u));
    return 1;
}

MusashiCdOwned *musashi_cd_owned_open_bios_idle(
    const MusashiDiscMedia *media, const MusashiCdOwnedHardware *hardware,
    uint64_t epoch) {
    MusashiDiscMediaInfo info;
    MusashiCdOwned *c;
    uint8_t sector[MUSASHI_DISC_RAW_SECTOR_SIZE];
    if (!media || !hardware || !hardware->current_thread || !hardware->healthy ||
        !hardware->raise_irq4 || !musashi_disc_media_get_info(media, &info) ||
        !info.total_frames || !info.track_count ||
        info.tracks[0].mode != MUSASHI_DISC_TRACK_MODE2_2352 ||
        !musashi_disc_media_read_sector(media, 0, sector, sizeof sector)) return NULL;
    c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->media = media;
    c->hardware = *hardware;
    c->owner_thread = hardware->current_thread(hardware->userdata);
    if (!c->owner_thread) { free(c); return NULL; }
    c->state.cycle = epoch;
    c->state.enable = 0x1f;
    c->state.lid_closed = c->state.rotating = c->state.standby = 1;
    c->state.volume[0] = c->state.volume[3] = 0x80;
    /* Source reset leaves staging zero, independently of active identity. */
    if (!owned_enter(c)) { free(c); return NULL; }
    owned_leave(c);
    return c;
}

int musashi_cd_owned_set_xa_sink(MusashiCdOwned *c, MusashiXaPcmSink sink, void *userdata) {
    if (!owned_enter(c)) return 0;
    if (!sink || c->xa_sink || c->state.reading) {
        owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);return owned_leave(c);
    }
    c->xa_sink=sink;c->xa_userdata=userdata;xa_reset(&c->xa);
    return owned_leave(c);
}
static int16_t owned_attenuate(int32_t value);

/* Data model: pinned PCSX first-read delay; regular hardware-rate cadence.
 * No PCSX BFM CPU-bias workaround or post-Setloc thirty-sector stall. Physical
 * seek, decoder FIFO latency and active-Pause radial timing remain unproven.
 * See artifacts/spu-startup-20260906/cd-sector-dma3-design.md. */
static uint8_t owned_status(const MusashiCdOwned *c) {
    return (uint8_t)((c->state.rotating ? 2 : 0) | (c->state.reading ? 0x20 : 0) |
                     (c->state.seeking ? 0x40 : 0));
}
static int owned_transfer_idle(MusashiCdOwned *c) {
    int idle = -1, accepted;
    if (!c->hardware.data_transfer_idle)
        return owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);
    accepted = c->hardware.data_transfer_idle(c->hardware.userdata,&idle);
    if (c->state.fault) return 0;
    if (!accepted || (idle != 0 && idle != 1))
        return owned_fault(c,MUSASHI_CD_OWNED_BACKEND_LOST);
    return idle || owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);
}
static int owned_seek_range(MusashiCdOwned *c, uint32_t lba, uint32_t *frame) {
    MusashiDiscMediaInfo info;
    if (!musashi_disc_media_get_info(c->media,&info))
        return owned_fault(c,MUSASHI_CD_OWNED_MEDIA_LOST);
    if (!info.track_count || info.tracks[0].mode != MUSASHI_DISC_TRACK_MODE2_2352 ||
        info.tracks[0].end_frame <= info.tracks[0].index01_frame ||
        lba >= info.tracks[0].end_frame-info.tracks[0].index01_frame)
        return owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);
    *frame = info.tracks[0].index01_frame+lba;
    return 1;
}
/* Selected seek-start buffer retirement, as DuckStation 0d33bd1
 * BeginSeeking/ClearSectorBuffers. Native policy, not hardware subphase parity.
 * Busy DMA must never lose a promised view. No bytes are counted as consumed. */
static int owned_seek_start(MusashiCdOwned *c) {
    MusashiCdOwnedState *s = &c->state;
    if (s->seek_starts == UINT64_MAX || s->buffer_retirements == UINT64_MAX)
        return owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
    if (!owned_transfer_idle(c)) return 0;
    s->incoming_valid = s->announced_valid = s->announced_claimed = 0;
    s->fifo_valid = s->data_requested = 0;
    s->fifo_size = s->fifo_cursor = 0;
    s->reading = 0;
    s->sector_due = s->sector_publish_due = 0;
    s->position_valid = 0; s->seeking = 1;
    ++s->seek_starts; ++s->buffer_retirements;
    return 1;
}
static int owned_seek_complete(MusashiCdOwned *c) {
    MusashiCdOwnedState *s = &c->state;
    uint8_t raw[MUSASHI_DISC_RAW_SECTOR_SIZE];
    uint32_t frame;
    uint64_t msf = (uint64_t)s->seek_target_lba+150u;
    if (s->seek_completions == UINT64_MAX || s->seek_sector_reads == UINT64_MAX)
        return owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
    if (!owned_seek_range(c,s->seek_target_lba,&frame)) return 0;
    if (!musashi_disc_media_read_sector(c->media,frame,raw,sizeof raw))
        return owned_fault(c,MUSASHI_CD_OWNED_MEDIA_LOST);
    if (msf/4500u > 99u || raw[15] != 2 ||
        raw[12] != owned_bcd((unsigned)(msf/4500u)) ||
        raw[13] != owned_bcd((unsigned)(msf/75u%60u)) ||
        raw[14] != owned_bcd((unsigned)(msf%75u)))
        return owned_fault(c,MUSASHI_CD_OWNED_MEDIA_LOST);
    s->position_lba = s->seek_target_lba; s->position_valid = 1;
    s->seeking = 0;
    ++s->seek_completions; ++s->seek_sector_reads;
    return 1;
}
static int owned_retry(MusashiCdOwned *c, uint64_t event, uint64_t target,
                       uint64_t *due) {
    uint64_t wait = 256 - (target - event) % 256;
    if (target > UINT64_MAX - wait) return owned_fault(c, MUSASHI_CD_OWNED_OVERFLOW);
    *due = target + wait;
    return 1;
}
/* Setmode values: speed plus 2340-byte data (A0) or XA-ADPCM (C0), either
 * with the XA filter bit 3 (A8/C8). */
static int owned_mode_ok(uint8_t mode) {
    mode &= (uint8_t)~0x08u;
    return mode == 0xa0 || mode == 0xc0;
}
static int owned_sector(MusashiCdOwned *c, uint64_t event) {
    MusashiCdOwnedState *s = &c->state;
    MusashiDiscMediaInfo info;
    uint8_t raw[MUSASHI_DISC_RAW_SECTOR_SIZE];
    uint32_t frame;
    if (s->incoming_valid) return owned_fault(c, MUSASHI_CD_OWNED_FIFO_OVERRUN);
    if (event > UINT64_MAX - 225792 || s->sectors_fetched == UINT64_MAX ||
        s->next_lba == UINT32_MAX) return owned_fault(c, MUSASHI_CD_OWNED_OVERFLOW);
    if (!musashi_disc_media_get_info(c->media, &info))
        return owned_fault(c, MUSASHI_CD_OWNED_MEDIA_LOST);
    if (s->next_lba > UINT32_MAX - info.tracks[0].index01_frame)
        return owned_fault(c, MUSASHI_CD_OWNED_OVERFLOW);
    frame = s->next_lba + info.tracks[0].index01_frame;
    if (frame >= info.tracks[0].end_frame ||
        !musashi_disc_media_read_sector(c->media, frame, raw, sizeof raw))
        return owned_fault(c, MUSASHI_CD_OWNED_MEDIA_LOST);
    /* Only actual data-track Mode2 sectors are currently produced. */
    if (raw[15] != 2) return owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
    /* XA audio goes through the decoder/matrix/SPU sink, never INT1 data. */
    if ((s->mode&0x48u)==0x48u && (raw[18]&0x44u)==0x44u &&
        (raw[16]!=s->filter_file || raw[17]!=s->filter_channel)) {
        /* Another file/channel's XA-ADPCM while filtering: the drive
         * neither plays nor delivers it. */
        if (s->xa_filtered==UINT64_MAX) return owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
        ++s->xa_filtered;
        s->position_lba=s->next_lba++;s->position_valid=1;
        ++s->sectors_fetched;c->first_sector_pending=0;
        s->sector_due=event+225792u;
        return s->fault==MUSASHI_CD_OWNED_OK;
    }
    if ((s->mode&0x40u) && (raw[18]&0x44u)==0x44u) {
        int16_t pcm[4704];unsigned i;
        if (!c->xa_sink || s->xa_sectors==UINT64_MAX ||
            s->xa_frames>UINT64_MAX-2352u || c->xa_stream_frames>UINT64_MAX-2352u ||
            !xa_decode(&c->xa,raw,pcm))
            return owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);
        for(i=0;i<2352;i++) {
            int32_t l=pcm[i*2],r=pcm[i*2+1];
            pcm[i*2]=s->muted?0:owned_attenuate(l*s->volume[0]+r*s->volume[2]);
            pcm[i*2+1]=s->muted?0:owned_attenuate(l*s->volume[1]+r*s->volume[3]);
        }
        if (!c->xa_sink(c->xa_userdata,event,c->xa_stream_frames,pcm,2352u))
            return owned_fault(c,MUSASHI_CD_OWNED_BACKEND_LOST);
        ++s->xa_sectors;s->xa_frames+=2352u;c->xa_stream_frames+=2352u;
        s->position_lba=s->next_lba++;s->position_valid=1;
        ++s->sectors_fetched;c->first_sector_pending=0;
        s->sector_due=event+225792u;
        return s->fault==MUSASHI_CD_OWNED_OK;
    }
    memcpy(c->incoming_sector, raw + 12, sizeof(c->incoming_sector));
    s->position_lba = s->next_lba; s->position_valid = 1;
    s->incoming_lba = s->next_lba++;
    s->incoming_valid = 1;
    ++s->sectors_fetched;
    c->first_sector_pending = 0;
    s->sector_due = event + 225792;
    s->sector_publish_due = event;
    return 1;
}
static int owned_publish(MusashiCdOwned *c, uint64_t event, uint64_t target) {
    MusashiCdOwnedState *s = &c->state;
    if (s->interrupt || s->response_count ||
        (s->announced_valid && !s->announced_claimed && s->command != 0x1bu))
        return owned_retry(c,event,target,&s->sector_publish_due);
    if (s->sectors_published == UINT64_MAX)
        return owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
    /* ReadS streaming may replace an acknowledged, unrequested sector.
     * The separately latched FIFO stays intact until BFRD requests new data.
     * A pending IRQ/response is still protected above. */
    /* New INT1 describes these real bytes, independently of any older locked
     * requested FIFO. Source43FCC releases that older view after this INT1. */
    memcpy(c->announced_sector,c->incoming_sector,sizeof(c->announced_sector));
    s->announced_valid = 1; s->announced_claimed = 0;
    s->announced_lba = s->incoming_lba;
    s->incoming_valid = 0; s->sector_publish_due = 0;
    ++s->sectors_published;
    s->announced_sequence = s->sectors_published;
    c->response_fifo[0] = owned_status(c); c->response_pos = 0;
    s->response_count = 1; s->interrupt = 1;
    return owned_irq(c);
}
/* DuckStation CDROM::BeginReading/BeginSeeking -> ClearSectorBuffers.
 * Preserve a promised active DMA view; clear idle old views without claiming
 * their unread bytes were consumed. The native transport has explicit ownership. */
static int owned_read_retire(MusashiCdOwned *c, int apply) {
    MusashiCdOwnedState *s=&c->state;
    if (!s->incoming_valid && !s->announced_valid && !s->fifo_valid) return 1;
    if (s->buffer_retirements==UINT64_MAX)
        return owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
    if (!owned_transfer_idle(c)) return 0;
    if (apply) {
        s->incoming_valid=s->announced_valid=s->announced_claimed=0;
        s->fifo_valid=s->data_requested=0; s->fifo_size=s->fifo_cursor=0;
        s->sector_publish_due=0; ++s->buffer_retirements;
    }
    return 1;
}
static int owned_command_event(MusashiCdOwned *c, uint64_t event, uint64_t target) {
    MusashiCdOwnedState *s = &c->state;
    uint64_t delay = 0;
    if (s->interrupt || s->response_count) return owned_retry(c,event,target,&s->due);
    if (s->phase == 1) {
        if (s->command == 0x0a) delay = 4100000;
        else if (s->command == 9) delay = c->pause_was_reading ? 1000000 : 7400;
        else if ((s->command == 6 || s->command == 0x1b)) delay = 451584;
        else if (s->command == 0x15) delay = 1806336;
    }
    if (event > UINT64_MAX - delay) return owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
    if (!s->lid_closed || !s->standby || !s->rotating)
        return owned_fault(c,MUSASHI_CD_OWNED_MEDIA_LOST);
    if (s->phase == 1 && (s->command == 6 || s->command == 0x1b)) {
        if (!owned_read_retire(c,1)) return 0;
        xa_reset(&c->xa);c->xa_stream_frames=0;
        s->reading = 1; s->next_lba = s->pending_lba; c->first_sector_pending = 1;
        s->sector_due = event + delay;
    }
    /* PCSX interrupt snapshots status before SeekL marks seeking. */
    if (s->command == 0x15 && s->phase == 2 && !owned_seek_complete(c)) return 0;
    c->response_fifo[0] = owned_status(c);
    if (s->command == 0x15 && s->phase == 1 && !owned_seek_start(c)) return 0;
    if (s->phase == 1 && s->command == 9 && c->pause_was_reading)
        c->response_fifo[0] |= 0x20;
    c->response_pos = 0; s->response_count = 1;
    if (s->command == 0x13 || s->command == 0x14) {
        memcpy(c->response_fifo + 1,c->toc_result,sizeof(c->toc_result));
        s->response_count = 3;
    }
    s->busy = 0; s->interrupt = s->phase == 2 ? 2 : 3;
    if (s->phase == 1 && (s->command == 0x0a || s->command == 9 || s->command == 0x15)) {
        if (s->command == 0x0a) { s->muted = 0; s->mode = 0x20; }
        s->phase = 2; s->due = event + delay;
        if (s->command == 9) s->busy = 1;
    } else {
        if (s->command == 0x0c) s->muted = 0;
        s->phase = 0;
    }
    return owned_irq(c);
}
static int owned_advance_kinds(MusashiCdOwned *c, uint64_t cycle, int allow_sector) {
    MusashiCdOwnedState *s;
    if (!owned_enter(c)) return 0;
    s = &c->state;
    if (cycle < s->cycle) {
        owned_fault(c,MUSASHI_CD_OWNED_CLOCK_BACKWARDS); return owned_leave(c);
    }
    for (;;) {
        uint64_t event = 0;
        int kind = 0;
        if (s->phase && s->due <= cycle) { event = s->due; kind = 1; }
        if (allow_sector && s->reading && s->sector_due <= cycle &&
            (!kind || s->sector_due < event)) {
            event = s->sector_due; kind = 2;
        }
        if (s->incoming_valid && s->sector_publish_due <= cycle &&
            (!kind || s->sector_publish_due < event)) {
            event = s->sector_publish_due; kind = 3;
        }
        if (!kind) break;
        s->cycle = event;
        if (kind == 1 ? !owned_command_event(c,event,cycle) :
            kind == 2 ? !owned_sector(c,event) : !owned_publish(c,event,cycle)) break;
    }
    if (!s->fault) s->cycle = cycle;
    return owned_leave(c);
}
int musashi_cd_owned_advance(MusashiCdOwned *c, uint64_t cycle) {
    return owned_advance_kinds(c, cycle, 1);
}
int musashi_cd_owned_advance_without_fetch(MusashiCdOwned *c, uint64_t cycle) {
    return owned_advance_kinds(c, cycle, 0);
}

static int owned_data_copy(MusashiCdOwned *c, void *destination, size_t bytes) {
    MusashiCdOwnedState *s = &c->state;
    uintptr_t out = (uintptr_t)destination, object = (uintptr_t)c;
    if (!destination || !bytes || out > UINTPTR_MAX - bytes ||
        (out < object + sizeof(*c) && object < out + bytes) ||
        !s->data_requested || !s->fifo_valid || bytes > (size_t)(s->fifo_size-s->fifo_cursor))
        return owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);
    if (bytes > UINT64_MAX - s->data_bytes_read)
        return owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
    memcpy(destination,c->data_fifo+s->fifo_cursor,bytes);
    s->fifo_cursor = (uint16_t)(s->fifo_cursor + bytes);
    s->data_bytes_read += bytes;
    return 1;
}
int musashi_cd_owned_read_data(MusashiCdOwned *c, void *destination, size_t bytes) {
    if (!owned_enter(c)) return 0;
    owned_data_copy(c,destination,bytes);
    return owned_leave(c);
}

int musashi_cd_owned_read8(void *userdata, uint32_t address, uint8_t *value) {
    MusashiCdOwned *c = userdata;
    uint8_t result = 0;
    if (!owned_enter(c)) return 0;
    if (!value) owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
    else switch (address) {
    case 0x1f801800u:
        result = c->state.index | (c->state.parameter_count ? 0 : 8) |
                 (c->state.parameter_count < sizeof(c->parameter_fifo) ? 0x10 : 0) |
                 (c->state.response_count ? 0x20 : 0) |
                 (c->state.busy ? 0x80 : 0) |
                 (c->state.data_requested && c->state.fifo_valid &&
                  c->state.fifo_cursor < c->state.fifo_size ? 0x40 : 0);
        break;
    case 0x1f801801u:
        if (!c->state.response_count) owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
        else {
            result = c->response_fifo[c->response_pos++];
            --c->state.response_count;
        }
        break;
    case 0x1f801802u:
        owned_data_copy(c,&result,1);
        break;
    case 0x1f801803u:
        result = 0xe0 | ((c->state.index & 1) ? c->state.interrupt : c->state.enable);
        break;
    default: owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS); break;
    }
    if (!c->state.fault) *value = result;
    return owned_leave(c);
}

int musashi_cd_owned_write8(void *userdata, uint32_t address, uint8_t value) {
    MusashiCdOwned *c = userdata;
    MusashiCdOwnedState *s;
    if (!owned_enter(c)) return 0;
    s = &c->state;
    switch (address) {
    case 0x1f801800u: s->index = value & 3; break;
    case 0x1f801801u:
        if (s->index == 3) { s->volume_pending[3] = value; break; }
        if (s->index || s->phase || s->interrupt || s->response_count ||
            (value != 1 && value != 2 && value != 6 && value != 0x1b && value != 9 && value != 0x0e &&
             value != 0x0a && value != 0x0c && value != 0x0d && value != 0x13 && value != 0x14 &&
             value != 0x15) ||
            s->parameter_count != (value == 2 ? 3 : value == 0x0d ? 2 :
                                   (value == 0x14 || value == 0x0e) ? 1 : 0)) {
            if (getenv("MUSASHI_TRACE_REFUSAL"))
                fprintf(stderr, "native_boot: CD_CMD_REFUSED value=%02x index=%u phase=%u interrupt=%u response_count=%u params=%u cycle=%llu\n",
                        value, s->index, s->phase, s->interrupt, s->response_count, s->parameter_count,
                        (unsigned long long)s->cycle);
            owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS); break;
        }
        if (s->cycle > UINT64_MAX - 0x800) {
            owned_fault(c, MUSASHI_CD_OWNED_OVERFLOW); break;
        }
        if (value == 0x13 || value == 0x14) {
            uint8_t result[2];
            if (!owned_toc_result(c, value, result)) break;
            memcpy(c->toc_result, result, sizeof(result));
        }
        if ((value == 6 || value == 0x1b || value == 0x0a) && s->reading) {
            owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS); break;
        }
        if (value == 2) {
            MusashiDiscMediaInfo info;
            uint32_t fields[3], msf, lba;
            unsigned i;
            for (i=0;i<3;++i) {
                unsigned byte = c->parameter_fifo[i];
                if ((byte & 15u) > 9u || (byte >> 4) > 9u) break;
                fields[i] = (byte >> 4)*10u + (byte & 15u);
            }
            if (i != 3 || fields[1] >= 60 || fields[2] >= 75) {
                owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS); break;
            }
            msf = fields[0]*4500u + fields[1]*75u + fields[2];
            if (!musashi_disc_media_get_info(c->media,&info)) {
                owned_fault(c,MUSASHI_CD_OWNED_MEDIA_LOST); break;
            }
            lba = msf >= 150 ? msf-150 : UINT32_MAX;
            if (lba >= info.tracks[0].end_frame-info.tracks[0].index01_frame) {
                owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS); break;
            }
            s->pending_lba = lba; s->location_valid = 1;
        }
        if (value == 0x0e) {
            if (!owned_mode_ok(c->parameter_fifo[0])) {
                owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS); break;
            }
            s->mode = c->parameter_fifo[0];
        }
        if (value == 0x0d) {
            s->filter_file = c->parameter_fifo[0];
            s->filter_channel = c->parameter_fifo[1];
        }
        if (value == 6 || value == 0x1b) {
            if (!s->location_valid || !owned_mode_ok(s->mode)) {
                owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS); break;
            }
            if (!owned_read_retire(c,0)) break;
        }
        if (value == 0x15) {
            uint32_t frame;
            if (!s->location_valid || !owned_mode_ok(s->mode)) {
                owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS); break;
            }
            if (!owned_seek_range(c,s->pending_lba,&frame) || !owned_transfer_idle(c)) break;
            s->seek_target_lba = s->pending_lba;
        }
        if (value == 9) {
            if (s->reading && c->first_sector_pending) { owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS); break; }
            c->pause_was_reading = s->reading;
            s->reading = 0; s->sector_due = s->sector_publish_due = 0;
            s->incoming_valid = 0;
        }
        s->command = value; s->busy = 1; s->phase = 1;
        s->parameter_count = 0;
        s->due = s->cycle + 0x800;
        break;
    case 0x1f801802u:
        if (s->index >= 2) s->volume_pending[s->index == 2 ? 0 : 2] = value;
        else if (s->index == 1) { s->enable = value & 0x1f; owned_irq(c); }
        else if (s->phase || s->interrupt || s->response_count ||
                 s->parameter_count == sizeof(c->parameter_fifo))
            owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
        else c->parameter_fifo[s->parameter_count++] = value;
        break;
    case 0x1f801803u:
        if (s->index == 2) s->volume_pending[1] = value;
        else if (s->index == 3) {
            /* Only apply is implemented; ADPCM mute/other controls refused. */
            if (value & (uint8_t)~0x20) owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
            else if (value & 0x20) memcpy(s->volume, s->volume_pending, 4);
        } else if (s->index == 1) {
            if (value & 0xa0) owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS);
            else {
                s->interrupt &= (uint8_t)~value;
                if (value & 0x40) s->parameter_count = 0;
            }
        } else if (value == 0x80) {
            if (!s->data_requested) {
                if (!s->announced_valid) owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);
                else if (s->fifo_latches == UINT64_MAX) owned_fault(c,MUSASHI_CD_OWNED_OVERFLOW);
                else {
                    s->fifo_size = (s->mode&0x20u) ? 2340u : 2048u;
                    memcpy(c->data_fifo,c->announced_sector+((s->mode&0x20u)?0u:12u),s->fifo_size);
                    s->fifo_valid = 1; s->fifo_cursor = 0;
                    s->fifo_lba = s->announced_lba; s->requested_sequence = s->announced_sequence;
                    s->data_requested = 1; s->announced_claimed = 1; ++s->fifo_latches;
                }
            } /* Already requested: retain identity and cursor, even after a new INT1. */
        } else if (!value) {
            s->data_requested = 0; s->fifo_cursor = 0;
        } else owned_fault(c,MUSASHI_CD_OWNED_BAD_ACCESS);
        break;
    default: owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS); break;
    }
    return owned_leave(c);
}

static int16_t owned_attenuate(int32_t value) {
    int32_t result = value >= 0 ? value / 128 : -((-value + 127) / 128);
    return (int16_t)(result > 32767 ? 32767 : result < -32768 ? -32768 : result);
}
int musashi_cd_owned_mix_pcm(MusashiCdOwned *c, const int16_t *input,
                             int16_t *output, size_t frames) {
    size_t i, bytes;
    uintptr_t in_addr = (uintptr_t)input, out_addr = (uintptr_t)output;
    if (!owned_enter(c)) return 0;
    if (frames > SIZE_MAX / (2 * sizeof(int16_t)) ||
        (frames && (in_addr % sizeof(int16_t) || out_addr % sizeof(int16_t))) ||
        (frames && (!input || !output))) {
        owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS); return owned_leave(c);
    }
    bytes = frames * 2 * sizeof(int16_t);
    if (bytes && (in_addr > UINTPTR_MAX - bytes || out_addr > UINTPTR_MAX - bytes ||
        (in_addr != out_addr && in_addr < out_addr + bytes && out_addr < in_addr + bytes))) {
        owned_fault(c, MUSASHI_CD_OWNED_BAD_ACCESS); return owned_leave(c);
    }
    for (i = 0; i < frames; ++i) {
        int32_t l = input[2*i], r = input[2*i+1];
        output[2*i] = c->state.muted ? 0 :
            owned_attenuate(l*c->state.volume[0] + r*c->state.volume[2]);
        output[2*i+1] = c->state.muted ? 0 :
            owned_attenuate(l*c->state.volume[1] + r*c->state.volume[3]);
    }
    return owned_leave(c);
}
uint64_t musashi_cd_owned_bounded_cycle(const MusashiCdOwnedState *s,
                                        uint64_t requested) {
    uint64_t one_fetch;
    if (!s || !s->reading || !s->sector_due)
        return requested;
    if (s->incoming_valid) {
        if (requested < s->sector_due)
            return requested;
        if (s->cycle >= s->sector_due)
            return s->cycle;
        return s->sector_due - 1u;
    }
    /* One fetch fills incoming; a second fetch in the same cut overruns. */
    if (s->sector_due > UINT64_MAX - 225791u)
        return requested;
    one_fetch = s->sector_due + 225791u;
    return requested > one_fetch ? one_fetch : requested;
}

int musashi_cd_owned_get_state(const MusashiCdOwned *c, MusashiCdOwnedState *out) {
    MusashiCdOwned *owner = (MusashiCdOwned *)c;
    uint64_t thread;
    if (!c || !out) return 0;
    if (c->entered) return owned_fault(owner, MUSASHI_CD_OWNED_REENTRY);
    owner->entered = 1;
    thread = c->hardware.current_thread(c->hardware.userdata);
    if (thread != c->owner_thread || c->entered != 1) {
        if (thread != c->owner_thread) owned_fault(owner, MUSASHI_CD_OWNED_WRONG_THREAD);
        owner->entered = 0;
        return 0;
    }
    *out = c->state;
    owner->entered = 0;
    return 1;
}
int musashi_cd_owned_close(MusashiCdOwned *c) {
    uint64_t thread;
    if (!c) return 1;
    if (c->entered) return owned_fault(c, MUSASHI_CD_OWNED_REENTRY);
    c->entered = 1;
    thread = c->hardware.current_thread(c->hardware.userdata);
    if (thread != c->owner_thread) {
        owned_fault(c, MUSASHI_CD_OWNED_WRONG_THREAD); c->entered = 0; return 0;
    }
    if (c->entered != 1) { c->entered = 0; return 0; }
    free(c);
    return 1;
}
