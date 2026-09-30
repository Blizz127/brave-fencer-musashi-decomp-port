#include "bfm_plat_audio.h"

#include "bfm_plat_timing.h"

#include <string.h>

#define MAX_BACKENDS 8
/* The null device plays nothing, but drains its queue at the audio rate of
 * the platform clock (bfm_plat_timing_now_ns, injectable in tests), so a
 * caller pacing by queue depth sees a real-rate consumer; the queue is
 * capped at one second. */
#define NULL_QUEUE_CAP BFM_PLAT_AUDIO_RATE

typedef struct NullAudio {
    BfmPlatAudioStatus st;
    int paused;
    uint64_t drained_ns;   /* platform time up to which frames were drained */
} NullAudio;

static void null_drain(NullAudio *a) {
    uint64_t now = bfm_plat_timing_now_ns(), frames;
    unsigned s;
    if (a->paused || now <= a->drained_ns) {
        if (now > a->drained_ns) a->drained_ns = now;
        return;
    }
    frames = (now - a->drained_ns) * a->st.rate / 1000000000ull;
    if (!frames) return;
    a->drained_ns += frames * 1000000000ull / a->st.rate;
    for (s = 0; s < BFM_AUDIO_STREAM_COUNT; s++)
        a->st.queued_frames[s] = frames >= a->st.queued_frames[s] ? 0
                                 : a->st.queued_frames[s] - (size_t)frames;
}

static NullAudio null_state;

static int null_open(void *self, uint32_t rate) {
    NullAudio *a = (NullAudio *)self;
    memset(a, 0, sizeof *a);
    a->st.rate = rate;
    a->st.channels = 2;
    a->st.playing = 1;
    a->st.master_volume = 1.0f;
    a->drained_ns = bfm_plat_timing_now_ns();
    return BFM_PLAT_OK;
}

static int null_queue(void *self, BfmPlatAudioStream s, const int16_t *pcm,
                      size_t frames) {
    NullAudio *a = (NullAudio *)self;
    (void)pcm;
    null_drain(a);
    if (a->st.queued_frames[s] + frames > NULL_QUEUE_CAP) return BFM_PLAT_NO_SPACE;
    a->st.queued_frames[s] += frames;
    a->st.submitted_frames[s] += frames;
    return BFM_PLAT_OK;
}

static int null_status(void *self, BfmPlatAudioStatus *out) {
    NullAudio *a = (NullAudio *)self;
    null_drain(a);
    *out = a->st;
    out->playing = !a->paused;
    return BFM_PLAT_OK;
}

static int null_volume(void *self, float v) {
    ((NullAudio *)self)->st.master_volume = v;
    return BFM_PLAT_OK;
}

static int null_pause(void *self, int p) {
    null_drain((NullAudio *)self);
    ((NullAudio *)self)->paused = p;
    return BFM_PLAT_OK;
}

static const BfmPlatAudioBackend null_backend = {
    "null", null_open, NULL, null_queue, null_status, null_volume, null_pause,
    &null_state
};

static const BfmPlatAudioBackend *backends[MAX_BACKENDS] = {&null_backend};
static unsigned backend_count = 1;
static const BfmPlatAudioBackend *active;
static int fast_forward;
static uint64_t dropped;

int bfm_plat_audio_register(const BfmPlatAudioBackend *b) {
    unsigned i;
    if (!b || !b->name || !*b->name) return BFM_PLAT_INVALID;
    for (i = 0; i < backend_count; i++)
        if (strcmp(backends[i]->name, b->name) == 0) return BFM_PLAT_INVALID;
    if (backend_count >= MAX_BACKENDS) return BFM_PLAT_NO_SPACE;
    backends[backend_count++] = b;
    return BFM_PLAT_OK;
}

const BfmPlatAudioBackend *bfm_plat_audio_find(const char *name) {
    unsigned i;
    if (!name) return NULL;
    for (i = 0; i < backend_count; i++)
        if (strcmp(backends[i]->name, name) == 0) return backends[i];
    return NULL;
}

int bfm_plat_audio_open(const char *name, const char **selected) {
    const BfmPlatAudioBackend *b = bfm_plat_audio_find(name);
    int r;
    bfm_plat_audio_close();
    if (!b) b = &null_backend;
    r = b->open ? b->open(b->self, BFM_PLAT_AUDIO_RATE) : BFM_PLAT_OK;
    if (r != BFM_PLAT_OK && b != &null_backend) {
        b = &null_backend;
        r = b->open(b->self, BFM_PLAT_AUDIO_RATE);
    }
    active = b;
    if (selected) *selected = b->name;
    return r;
}

void bfm_plat_audio_close(void) {
    if (active && active->close) active->close(active->self);
    active = NULL;
}

const char *bfm_plat_audio_active(void) { return active ? active->name : NULL; }

int bfm_plat_audio_queue(BfmPlatAudioStream s, const int16_t *pcm,
                         size_t frames) {
    if (!active) return BFM_PLAT_NOT_READY;
    if ((unsigned)s >= BFM_AUDIO_STREAM_COUNT || (frames && !pcm))
        return BFM_PLAT_INVALID;
    if (frames == 0) return BFM_PLAT_OK;
    if (fast_forward) {
        dropped += frames;
        return BFM_PLAT_OK;
    }
    return active->queue ? active->queue(active->self, s, pcm, frames)
                         : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_audio_status(BfmPlatAudioStatus *out) {
    if (!active) return BFM_PLAT_NOT_READY;
    if (!out) return BFM_PLAT_INVALID;
    memset(out, 0, sizeof *out);
    return active->status ? active->status(active->self, out)
                          : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_audio_set_volume(float v) {
    if (!active) return BFM_PLAT_NOT_READY;
    if (!(v >= 0.0f && v <= 1.0f)) return BFM_PLAT_INVALID;
    return active->set_volume ? active->set_volume(active->self, v)
                              : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_audio_pause(int p) {
    if (!active) return BFM_PLAT_NOT_READY;
    return active->pause ? active->pause(active->self, p != 0)
                         : BFM_PLAT_UNSUPPORTED;
}

void bfm_plat_audio_set_fast_forward(int a) { fast_forward = a != 0; }

void bfm_plat_audio_reset(void) {
    bfm_plat_audio_close();
    backend_count = 1;
    fast_forward = 0;
    dropped = 0;
}
