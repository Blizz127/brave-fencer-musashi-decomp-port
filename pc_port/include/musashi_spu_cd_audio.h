#ifndef MUSASHI_SPU_CD_AUDIO_H
#define MUSASHI_SPU_CD_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES 768u
#define MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT 8192u

typedef struct MusashiSpuCdAudio MusashiSpuCdAudio;

/* The backend is an owned, checked PCM sink. It must copy samples before the
 * queue callback returns; it never receives guest memory or SPU state. */
typedef struct MusashiSpuCdAudioBackend {
    void *userdata;
    int (*ready)(void *userdata);
    uint64_t (*thread_token)(void *userdata);
    int (*status)(void *userdata, size_t *queued_frames, int *playing);
    int (*queue)(void *userdata, const int16_t *stereo_pcm, size_t frames);
} MusashiSpuCdAudioBackend;

typedef struct MusashiSpuCdAudioSnapshot {
    int16_t main_left_configured;
    int16_t main_right_configured;
    int16_t main_left_current;
    int16_t main_right_current;
    int16_t cd_left_gain;
    int16_t cd_right_gain;
    uint16_t control;
    uint64_t cycle;
    uint64_t sample_index;
    size_t pending_frames;
    /* Last backend queue count observed by the core, not a live query. */
    size_t queued_frames;
    size_t submitted_frames;
    int faulted;
    uint32_t transfer_cursor;
    uint16_t transfer_address, transfer_control;
    int transfer_address_valid;
    uint64_t manual_halfwords;
    uint64_t bootstrap_halfwords;
} MusashiSpuCdAudioSnapshot;

/* Read-only diagnostics from the same sample transaction as queued PCM.
 * Unknown access values must not be interpreted; history counts are invalid
 * slots, not a guessed warmup deadline. Addresses are SPU RAM byte offsets. */
typedef struct MusashiSpuReverbAccess {
    uint32_t address;
    int write, known;
    int16_t value;
} MusashiSpuReverbAccess;
typedef struct MusashiSpuReverbSnapshot {
    uint16_t coefficients[32], base;
    int16_t wet_left, wet_right;
    uint32_t cursor, eon;
    unsigned next_channel;
    uint64_t frames, processed_left, processed_right;
    uint64_t ram_reads, ram_writes, unknown_reads, unsupported_steps;
    uint64_t unknown_history_left, unknown_history_right;
    int enabled;
    uint32_t first_unknown_address;
    unsigned access_count;
    MusashiSpuReverbAccess accesses[32];
} MusashiSpuReverbSnapshot;
int musashi_spu_cd_audio_reverb_snapshot(const MusashiSpuCdAudio *,
                                        MusashiSpuReverbSnapshot *);

/* Shared SPU extension: manual DA8 writes commit synchronously to owned
 * 512KiB RAM (no outstanding manual work). Physical FIFO/key subphase timing
 * is UNPROVEN. Keys apply after the next rendered frame; active ADSR support
 * includes all ADSR field encodings and live writes with filter0 ADPCM. Positive
 * current-envelope writes change the real level; negative levels refuse.
 * Other active features refuse. */
enum MusashiSpuVoicePhase {
    MUSASHI_SPU_VOICE_OFF, MUSASHI_SPU_VOICE_ATTACK, MUSASHI_SPU_VOICE_DECAY,
    MUSASHI_SPU_VOICE_SUSTAIN, MUSASHI_SPU_VOICE_RELEASE
};
typedef struct MusashiSpuVoiceSnapshot {
    uint16_t registers[8];
    int16_t gain_left, gain_right;
    unsigned phase;
    int32_t envelope;
    uint32_t cursor, repeat;
    uint16_t pitch_fraction;
    uint64_t decoded_blocks, decoded_samples;
    int pending_on, pending_off, endx;
} MusashiSpuVoiceSnapshot;
/* Checked diagnostic copies; never guest setters or writable memory views.
 * Copies remain available to the owner after fault. Unwritten RAM refuses. */
int musashi_spu_cd_audio_copy_ram(const MusashiSpuCdAudio *, uint32_t offset,
                                void *destination, size_t length);
int musashi_spu_cd_audio_voice_snapshot(const MusashiSpuCdAudio *, unsigned voice,
                                      MusashiSpuVoiceSnapshot *);

/* Returns a heap-owned opaque state object, or NULL on allocation failure. */
MusashiSpuCdAudio *musashi_spu_cd_audio_create(void);

/* Starts the explicit settled BIOS-main-mute profile at epoch. The backend
 * must already be genuinely open and remains owned by the caller. */
int musashi_spu_cd_audio_init_bios_muted(
    MusashiSpuCdAudio *audio, const MusashiSpuCdAudioBackend *backend,
    uint64_t epoch);

/* Advances the shared absolute-cycle clock and renders silent or submitted
 * CD samples into the same checked backend. Reads never advance the clock. */
int musashi_spu_cd_audio_advance(MusashiSpuCdAudio *audio,
                                 uint64_t absolute_cycles);

int musashi_spu_cd_audio_read16(void *userdata,
                                uint32_t address, uint16_t *value);
int musashi_spu_cd_audio_write16(void *userdata,
                                 uint32_t address, uint16_t value);

/* Submits copied stereo signed-16 PCM at an ordered source sample index. */
int musashi_spu_cd_audio_submit_cd_pcm(MusashiSpuCdAudio *audio,
                                       uint64_t first_sample,
                                       const int16_t *stereo_pcm,
                                       size_t frames);

int musashi_spu_cd_audio_snapshot(const MusashiSpuCdAudio *audio,
                                  MusashiSpuCdAudioSnapshot *snapshot);
/* Refuses cross-thread or callback-reentrant destruction; returns nonzero only
 * when the object was released. Uninitialized objects may be destroyed. */
int musashi_spu_cd_audio_destroy(MusashiSpuCdAudio *audio);

#endif
