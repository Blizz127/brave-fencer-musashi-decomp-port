/* "sdl" audio backend: bfm_plat audio -> the existing pc_port/audio_sdl.c
 * owner (SDL2 audio queue at 44.1 kHz s16 stereo).
 *
 * MAIN and CD streams are both queued to the one device; the in-house SPU
 * core already mixes CD audio into MAIN, so the CD stream is only used by
 * backends that mix separately. Status: compile-checked; runs once SDL2 dev
 * libs are installed. */
#include "../bfm_plat.h"
#include "musashi_audio_sdl.h"

#include <string.h>

typedef struct SdlAudio {
    MusashiAudioSdl *owner;
    uint64_t submitted[BFM_AUDIO_STREAM_COUNT];
    float volume;
} SdlAudio;

static SdlAudio state;

static int sdl_open(void *self, uint32_t rate) {
    SdlAudio *a = (SdlAudio *)self;
    memset(a, 0, sizeof *a);
    if (rate != BFM_PLAT_AUDIO_RATE) return BFM_PLAT_UNSUPPORTED;
    a->owner = musashi_audio_sdl_create();
    if (!a->owner) return BFM_PLAT_ERROR;
    if (!musashi_audio_sdl_open(a->owner)) {
        musashi_audio_sdl_destroy(a->owner);
        a->owner = NULL;
        return BFM_PLAT_ERROR;
    }
    a->volume = 1.0f;
    return BFM_PLAT_OK;
}

static void sdl_close(void *self) {
    SdlAudio *a = (SdlAudio *)self;
    if (!a->owner) return;
    musashi_audio_sdl_close(a->owner);
    musashi_audio_sdl_destroy(a->owner);
    a->owner = NULL;
}

static int sdl_queue(void *self, BfmPlatAudioStream s, const int16_t *pcm,
                     size_t frames) {
    SdlAudio *a = (SdlAudio *)self;
    const MusashiSpuCdAudioBackend *b;
    if (!a->owner || !(b = musashi_audio_sdl_backend(a->owner)) || !b->queue)
        return BFM_PLAT_NOT_READY;
    if (!b->queue(b->userdata, pcm, frames)) return BFM_PLAT_NO_SPACE;
    a->submitted[s] += frames;
    return BFM_PLAT_OK;
}

static int sdl_status(void *self, BfmPlatAudioStatus *out) {
    SdlAudio *a = (SdlAudio *)self;
    MusashiAudioSdlStatus st;
    if (!a->owner || !musashi_audio_sdl_status(a->owner, &st))
        return BFM_PLAT_NOT_READY;
    out->rate = st.frequency;
    out->channels = st.channels;
    out->queued_frames[BFM_AUDIO_STREAM_MAIN] = st.queued_frames;
    memcpy(out->submitted_frames, a->submitted, sizeof a->submitted);
    out->playing = st.playing;
    out->faulted = st.faulted;
    out->master_volume = a->volume;
    return BFM_PLAT_OK;
}

static const BfmPlatAudioBackend sdl_backend = {
    "sdl", sdl_open, sdl_close, sdl_queue, sdl_status, NULL, NULL, &state
};

int bfm_plat_backend_sdl_audio_register(void);
int bfm_plat_backend_sdl_audio_register(void) {
    return bfm_plat_audio_register(&sdl_backend);
}
