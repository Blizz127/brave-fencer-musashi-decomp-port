/* bfm_plat audio sinks: the null backend drains at the audio rate of the
 * (injected) platform clock, and, when built with -DPROBE_OPENAL, the
 * "openal" backend accepts a stream and drains it in real time.
 * Prints "OK <name>" per passing section; exits 1 on the first failure. */
#include "bfm_plat_audio.h"
#include "bfm_plat_timing.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static uint64_t fake_ns;
static uint64_t fake_now(void *u) { (void)u; return fake_ns; }
static void fake_sleep(void *u, uint64_t ns) { (void)u; fake_ns += ns; }

static size_t queued(BfmPlatAudioStream s) {
    BfmPlatAudioStatus st;
    if (bfm_plat_audio_status(&st) != BFM_PLAT_OK) return (size_t)-1;
    return st.queued_frames[s];
}

static int null_sink(void) {
    static int16_t pcm[BFM_PLAT_AUDIO_RATE * 2];
    const BfmPlatClock clock = {fake_now, fake_sleep, NULL};
    const char *sel = NULL;
    BfmPlatAudioStatus st;
    fake_ns = 1000000000ull;
    bfm_plat_timing_set_clock(&clock);
    CHECK(bfm_plat_audio_open("null", &sel) == BFM_PLAT_OK && sel && !strcmp(sel, "null"));
    /* Capacity is one second; nothing drains while the clock stands still. */
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, 22050) == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, BFM_PLAT_AUDIO_RATE) == BFM_PLAT_NO_SPACE);
    CHECK(queued(BFM_AUDIO_STREAM_MAIN) == 22050);
    /* A quarter second of platform time drains a quarter second of PCM. */
    fake_ns += 250000000ull;
    CHECK(queued(BFM_AUDIO_STREAM_MAIN) == 22050 - 11025);
    /* A paused device holds its queue. */
    CHECK(bfm_plat_audio_pause(1) == BFM_PLAT_OK);
    fake_ns += 1000000000ull;
    CHECK(queued(BFM_AUDIO_STREAM_MAIN) == 11025);
    CHECK(bfm_plat_audio_pause(0) == BFM_PLAT_OK);
    fake_ns += 1000000000ull;
    CHECK(queued(BFM_AUDIO_STREAM_MAIN) == 0);
    /* A continuous real-rate stream never fills up (the SPU core faults on a
     * refused queue): 10 s of 735-frame fields, one field per 1/60 s. */
    {
        unsigned i;
        for (i = 0; i < 600; i++) {
            CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, 735) == BFM_PLAT_OK);
            CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_CD, pcm, 735) == BFM_PLAT_OK);
            fake_ns += 1000000000ull / 60u;
        }
    }
    CHECK(bfm_plat_audio_status(&st) == BFM_PLAT_OK);
    CHECK(st.queued_frames[BFM_AUDIO_STREAM_MAIN] <= 735u + 1u);
    CHECK(st.submitted_frames[BFM_AUDIO_STREAM_MAIN] == 22050u + 600u * 735u);
    bfm_plat_audio_close();
    bfm_plat_timing_set_clock(NULL);
    puts("OK null");
    return 0;
}

#ifdef PROBE_OPENAL
int bfm_plat_backend_openal_audio_register(void);

static void sleep_ms(unsigned ms) {
    struct timespec t = {0, (long)ms * 1000000L};
    nanosleep(&t, NULL);
}

static int openal_sink(void) {
    static int16_t tone[4410 * 2];
    const char *sel = NULL;
    size_t before, after;
    unsigned i;
    CHECK(bfm_plat_backend_openal_audio_register() == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_open("openal", &sel) == BFM_PLAT_OK && sel);
    if (strcmp(sel, "openal") != 0) {
        puts("SKIP openal (no device)");
        bfm_plat_audio_close();
        return 0;
    }
    for (i = 0; i < 4410; i++) tone[2 * i] = tone[2 * i + 1] = (int16_t)((i % 100) < 50 ? 8000 : -8000);
    for (i = 0; i < 4; i++)
        CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, tone, 4410) == BFM_PLAT_OK);
    before = queued(BFM_AUDIO_STREAM_MAIN);
    CHECK(before > 0 && before <= 4u * 4410u);
    sleep_ms(250);
    after = queued(BFM_AUDIO_STREAM_MAIN);
    CHECK(after < before);            /* the device consumed PCM in real time */
    CHECK(bfm_plat_audio_set_volume(0.5f) == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_pause(1) == BFM_PLAT_OK && bfm_plat_audio_pause(0) == BFM_PLAT_OK);
    bfm_plat_audio_close();
    printf("OK openal queued %zu -> %zu\n", before, after);
    return 0;
}
#endif

int main(void) {
    if (null_sink()) return 1;
#ifdef PROBE_OPENAL
    if (openal_sink()) return 1;
#endif
    return 0;
}
