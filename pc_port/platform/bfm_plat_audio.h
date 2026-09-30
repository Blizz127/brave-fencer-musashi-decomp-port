#ifndef BFM_PLAT_AUDIO_H
#define BFM_PLAT_AUDIO_H

/* Audio interface: the host output device behind the in-house SPU/sequencer.
 *
 * The retail sound driver talks to the SPU; the in-house SPU core
 * (pc_port/spu_cd_audio.c and the spu_*_core.h pieces), or PsyCross's libspu,
 * produces 44.1 kHz signed 16-bit stereo. This interface only carries that
 * mixed PCM plus CD-XA/CD-DA streams to a device, so any backend
 * (SDL, OpenAL, a WAV recorder, null) can sit behind it. */

#include "bfm_plat_types.h"

#define BFM_PLAT_AUDIO_RATE 44100u

typedef enum BfmPlatAudioStream {
    BFM_AUDIO_STREAM_MAIN = 0, /* SPU mix output */
    BFM_AUDIO_STREAM_CD = 1,   /* decoded XA / CD-DA, mixed by the backend */
    BFM_AUDIO_STREAM_COUNT
} BfmPlatAudioStream;

typedef struct BfmPlatAudioStatus {
    uint32_t rate;
    uint32_t channels;
    size_t queued_frames[BFM_AUDIO_STREAM_COUNT];
    uint64_t submitted_frames[BFM_AUDIO_STREAM_COUNT];
    int playing;
    int faulted;
    float master_volume;
} BfmPlatAudioStatus;

typedef struct BfmPlatAudioBackend {
    const char *name;
    int (*open)(void *self, uint32_t rate);
    void (*close)(void *self);
    /* Interleaved stereo s16. Backends may refuse when their queue is full
     * (BFM_PLAT_NO_SPACE); callers drop or retry, never block the game. */
    int (*queue)(void *self, BfmPlatAudioStream stream, const int16_t *pcm,
                 size_t frames);
    int (*status)(void *self, BfmPlatAudioStatus *out);
    int (*set_volume)(void *self, float volume);
    int (*pause)(void *self, int paused);
    void *self;
} BfmPlatAudioBackend;

int bfm_plat_audio_register(const BfmPlatAudioBackend *backend);
const BfmPlatAudioBackend *bfm_plat_audio_find(const char *name);
int bfm_plat_audio_open(const char *name, const char **selected);
void bfm_plat_audio_close(void);
const char *bfm_plat_audio_active(void);

int bfm_plat_audio_queue(BfmPlatAudioStream stream, const int16_t *pcm,
                         size_t frames);
int bfm_plat_audio_status(BfmPlatAudioStatus *out);
int bfm_plat_audio_set_volume(float volume);   /* 0..1 */
int bfm_plat_audio_pause(int paused);

/* Fast-forward: when active the platform drops PCM instead of letting the
 * device queue grow without bound. Set by bfm_plat_timing. */
void bfm_plat_audio_set_fast_forward(int active);

void bfm_plat_audio_reset(void);

#endif
