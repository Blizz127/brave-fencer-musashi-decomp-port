#ifndef MUSASHI_AUDIO_SDL_H
#define MUSASHI_AUDIO_SDL_H

#include <stddef.h>
#include <stdint.h>

#include "musashi_spu_cd_audio.h"

typedef struct MusashiAudioSdl MusashiAudioSdl;

typedef struct MusashiAudioSdlStatus {
    uint32_t frequency;
    uint16_t format;
    uint8_t channels;
    size_t queued_frames;
    int playing;
    int faulted;
} MusashiAudioSdlStatus;

MusashiAudioSdl *musashi_audio_sdl_create(void);
int musashi_audio_sdl_open(MusashiAudioSdl *audio);
const MusashiSpuCdAudioBackend *musashi_audio_sdl_backend(
    const MusashiAudioSdl *audio);
int musashi_audio_sdl_status(const MusashiAudioSdl *audio,
                             MusashiAudioSdlStatus *status);
int musashi_audio_sdl_close(MusashiAudioSdl *audio);
int musashi_audio_sdl_destroy(MusashiAudioSdl *audio);

#endif
