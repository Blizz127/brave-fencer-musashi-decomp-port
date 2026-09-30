/* "openal" audio backend: the SPU mix (and CD stream) to an OpenAL device.
 *
 * A software FIFO takes whatever the caller queues (up to FIFO_FRAMES) and
 * feeds a ring of streaming OpenAL buffers; queued_frames reports the FIFO
 * plus the frames still in flight on the source, so a caller that paces by
 * the queue depth (the in-house SPU core does) sees a real-rate consumer.
 * The CD stream is mixed into the main stream with saturation as main frames
 * arrive; a CD stream with no main stream is not played (the port's SPU core
 * already mixes CD-XA into its output).
 *
 * OpenAL Soft selects its output from ALSOFT_DRIVERS / alsoft.conf, so a
 * headless host can use its "null" or "wave" (file) driver. */
#include "bfm_plat_audio.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <stdio.h>
#include <string.h>

#define BUFFERS 8
#define BUFFER_FRAMES 1024u
#define FIFO_FRAMES 32768u

typedef struct OpenAlAudio {
    ALCdevice *device;
    ALCcontext *context;
    ALuint source;
    ALuint buffers[BUFFERS];
    ALuint free_buffers[BUFFERS];
    unsigned free_count;
    unsigned inflight;                 /* buffers queued on the source */
    unsigned inflight_head;            /* oldest entry of inflight_frames */
    uint32_t inflight_frames[BUFFERS]; /* frames per queued buffer, queue order */
    int16_t fifo[FIFO_FRAMES * 2];
    size_t head, count;                /* main FIFO (frames) */
    int16_t cd[FIFO_FRAMES * 2];
    size_t cd_head, cd_count;
    BfmPlatAudioStatus st;
    int paused;
} OpenAlAudio;

static OpenAlAudio al_state;

static int al_ok(void) { return alGetError() == AL_NO_ERROR; }

static void oal_close(void *self) {
    OpenAlAudio *a = (OpenAlAudio *)self;
    if (a->context) {
        alSourceStop(a->source);
        alSourcei(a->source, AL_BUFFER, 0);
        alDeleteSources(1, &a->source);
        alDeleteBuffers(BUFFERS, a->buffers);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(a->context);
    }
    if (a->device) alcCloseDevice(a->device);
    memset(a, 0, sizeof *a);
}

static int oal_open(void *self, uint32_t rate) {
    OpenAlAudio *a = (OpenAlAudio *)self;
    unsigned i;
    memset(a, 0, sizeof *a);
    a->device = alcOpenDevice(NULL);
    if (!a->device) return BFM_PLAT_UNSUPPORTED;
    a->context = alcCreateContext(a->device, NULL);
    if (!a->context || !alcMakeContextCurrent(a->context)) {
        oal_close(a);
        return BFM_PLAT_UNSUPPORTED;
    }
    alGetError();
    alGenSources(1, &a->source);
    alGenBuffers(BUFFERS, a->buffers);
    if (!al_ok()) {
        oal_close(a);
        return BFM_PLAT_UNSUPPORTED;
    }
    for (i = 0; i < BUFFERS; i++) a->free_buffers[i] = a->buffers[i];
    a->free_count = BUFFERS;
    a->st.rate = rate;
    a->st.channels = 2;
    a->st.playing = 1;
    a->st.master_volume = 1.0f;
    {
        const ALCchar *name = alcGetString(a->device, ALC_DEVICE_SPECIFIER);
        fprintf(stderr, "bfm_plat: audio openal device=\"%s\" rate=%u\n",
                name ? name : "?", (unsigned)rate);
    }
    return BFM_PLAT_OK;
}

/* Reclaim played buffers and refill the source from the FIFO. */
static void pump(OpenAlAudio *a) {
    ALint processed = 0, state = 0;
    if (!a->context) return;
    alGetSourcei(a->source, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0 && a->inflight) {
        ALuint b;
        alSourceUnqueueBuffers(a->source, 1, &b);
        a->free_buffers[a->free_count++] = b;
        a->inflight_head = (a->inflight_head + 1u) % BUFFERS;
        a->inflight--;
    }
    while (a->free_count && a->count) {
        int16_t chunk[BUFFER_FRAMES * 2];
        size_t n = a->count < BUFFER_FRAMES ? a->count : BUFFER_FRAMES, i;
        ALuint b = a->free_buffers[--a->free_count];
        for (i = 0; i < n; i++) {
            size_t at = (a->head + i) % FIFO_FRAMES;
            chunk[2 * i] = a->fifo[2 * at];
            chunk[2 * i + 1] = a->fifo[2 * at + 1];
        }
        a->head = (a->head + n) % FIFO_FRAMES;
        a->count -= n;
        alBufferData(b, AL_FORMAT_STEREO16, chunk, (ALsizei)(n * 4u), (ALsizei)a->st.rate);
        alSourceQueueBuffers(a->source, 1, &b);
        a->inflight_frames[(a->inflight_head + a->inflight) % BUFFERS] = (uint32_t)n;
        a->inflight++;
    }
    alGetSourcei(a->source, AL_SOURCE_STATE, &state);
    if (!a->paused && a->inflight && state != AL_PLAYING) alSourcePlay(a->source);
    if (!al_ok()) a->st.faulted = 1;
}

static size_t inflight_frames(OpenAlAudio *a) {
    ALint processed = 0, offset = 0;
    size_t frames = 0;
    unsigned i;
    if (!a->inflight) return 0;
    alGetSourcei(a->source, AL_BUFFERS_PROCESSED, &processed);
    alGetSourcei(a->source, AL_SAMPLE_OFFSET, &offset);  /* into the current buffer */
    for (i = (unsigned)(processed > 0 ? processed : 0); i < a->inflight; i++)
        frames += a->inflight_frames[(a->inflight_head + i) % BUFFERS];
    return frames > (size_t)offset ? frames - (size_t)offset : 0;
}

static int16_t sat(int v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static int oal_queue(void *self, BfmPlatAudioStream s, const int16_t *pcm, size_t frames) {
    OpenAlAudio *a = (OpenAlAudio *)self;
    size_t i;
    if (!a->context || a->st.faulted) return BFM_PLAT_NOT_READY;
    pump(a);
    if (s == BFM_AUDIO_STREAM_CD) {
        if (frames > FIFO_FRAMES - a->cd_count) return BFM_PLAT_NO_SPACE;
        for (i = 0; i < frames; i++) {
            size_t at = (a->cd_head + a->cd_count + i) % FIFO_FRAMES;
            a->cd[2 * at] = pcm[2 * i];
            a->cd[2 * at + 1] = pcm[2 * i + 1];
        }
        a->cd_count += frames;
        a->st.submitted_frames[s] += frames;
        return BFM_PLAT_OK;
    }
    if (frames > FIFO_FRAMES - a->count) return BFM_PLAT_NO_SPACE;
    for (i = 0; i < frames; i++) {
        size_t at = (a->head + a->count + i) % FIFO_FRAMES;
        int l = pcm[2 * i], r = pcm[2 * i + 1];
        if (a->cd_count) {
            l += a->cd[2 * a->cd_head];
            r += a->cd[2 * a->cd_head + 1];
            a->cd_head = (a->cd_head + 1) % FIFO_FRAMES;
            a->cd_count--;
        }
        a->fifo[2 * at] = sat(l);
        a->fifo[2 * at + 1] = sat(r);
    }
    a->count += frames;
    a->st.submitted_frames[s] += frames;
    pump(a);
    return a->st.faulted ? BFM_PLAT_NOT_READY : BFM_PLAT_OK;
}

static int oal_status(void *self, BfmPlatAudioStatus *out) {
    OpenAlAudio *a = (OpenAlAudio *)self;
    pump(a);
    *out = a->st;
    out->queued_frames[BFM_AUDIO_STREAM_MAIN] = a->count + inflight_frames(a);
    out->queued_frames[BFM_AUDIO_STREAM_CD] = a->cd_count;
    out->playing = a->context && !a->paused;
    return BFM_PLAT_OK;
}

static int oal_volume(void *self, float v) {
    OpenAlAudio *a = (OpenAlAudio *)self;
    if (!a->context) return BFM_PLAT_NOT_READY;
    alListenerf(AL_GAIN, v);
    a->st.master_volume = v;
    return al_ok() ? BFM_PLAT_OK : BFM_PLAT_INVALID;
}

static int oal_pause(void *self, int p) {
    OpenAlAudio *a = (OpenAlAudio *)self;
    if (!a->context) return BFM_PLAT_NOT_READY;
    a->paused = p;
    if (p) alSourcePause(a->source);
    else if (a->inflight) alSourcePlay(a->source);
    return al_ok() ? BFM_PLAT_OK : BFM_PLAT_INVALID;
}

static const BfmPlatAudioBackend openal_backend = {
    "openal", oal_open, oal_close, oal_queue, oal_status, oal_volume, oal_pause, &al_state
};

int bfm_plat_backend_openal_audio_register(void) {
    return bfm_plat_audio_register(&openal_backend);
}
