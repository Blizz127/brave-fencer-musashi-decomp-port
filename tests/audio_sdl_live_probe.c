#include "../pc_port/include/musashi_audio_sdl.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cleanup_owned(MusashiSpuCdAudio **audio,
                         MusashiAudioSdl **backend) {
    int okay = 1;
    if (audio && *audio) {
        if (musashi_spu_cd_audio_destroy(*audio))
            *audio = NULL;
        else
            return 0; /* The still-live core retains its backend dependency. */
    }
    if (backend && *backend) {
        if (!musashi_audio_sdl_close(*backend)) {
            okay = 0;
        } else if (musashi_audio_sdl_destroy(*backend)) {
            *backend = NULL;
        } else {
            okay = 0;
        }
    }
    return okay;
}

int main(void) {
    MusashiAudioSdl *backend = musashi_audio_sdl_create();
    MusashiSpuCdAudio *audio = NULL;
    MusashiAudioSdlStatus status;
    MusashiSpuCdAudioSnapshot snapshot;
    const MusashiSpuCdAudioBackend *sink;
    int saw_nonzero = 0;

    if (!backend) return 2;
    if (!musashi_audio_sdl_open(backend)) {
        fprintf(stderr, "audio live probe: NOT_RUN (no owned real SDL device): %s\n",
                SDL_GetError());
        fprintf(stderr, "audio live probe: current_driver=%s XDG_RUNTIME_DIR=%s\n",
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)",
                getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
        for (int index = 0; index < SDL_GetNumAudioDrivers(); ++index)
            fprintf(stderr, "audio live probe: compiled_driver[%d]=%s\n", index,
                    SDL_GetAudioDriver(index));
        if (!cleanup_owned(&audio, &backend)) return 2;
        return 77;
    }
    if (!musashi_audio_sdl_status(backend, &status)) goto fail;
    fprintf(stderr, "audio live probe: driver=%s freq=%u format=%u channels=%u playing=%d\n",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)",
            status.frequency, (unsigned)status.format, status.channels,
            status.playing);
    sink = musashi_audio_sdl_backend(backend);
    audio = musashi_spu_cd_audio_create();
    if (!sink || !audio || !musashi_spu_cd_audio_init_bios_muted(audio, sink, 0) ||
        !musashi_spu_cd_audio_advance(audio, 768u * 1024u))
        goto fail;
    if (!musashi_audio_sdl_status(backend, &status) || !status.playing) goto fail;
    if (!musashi_spu_cd_audio_snapshot(audio, &snapshot)) goto fail;
    fprintf(stderr, "audio live probe: queued=%zu submitted=%zu\n",
            status.queued_frames, snapshot.submitted_frames);
    for (unsigned attempt = 0; attempt < 40u; ++attempt) {
        if (!musashi_audio_sdl_status(backend, &status)) goto fail;
        if (status.queued_frames != 0) saw_nonzero = 1;
        if (saw_nonzero && status.queued_frames == 0) break;
        SDL_Delay(25);
    }
    if (!saw_nonzero || !musashi_audio_sdl_status(backend, &status) ||
        status.queued_frames != 0)
        goto fail;
    if (!cleanup_owned(&audio, &backend)) {
        fprintf(stderr, "audio live probe: cleanup=0\n");
        return 2;
    }
    fprintf(stderr, "audio live probe: PASS drained=1 cleanup=1\n");
    return 0;

fail:
    (void)cleanup_owned(&audio, &backend);
    return 2;
}
