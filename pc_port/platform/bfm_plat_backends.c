/* Backends compiled into this build. The default (dependency-free) build has
 * only the built-in null/image/scripted backends. SDL/PsyCross builds define
 * BFM_PLAT_WITH_PSYCROSS / BFM_PLAT_WITH_SDL_AUDIO / BFM_PLAT_WITH_PINNED_DISC
 * / BFM_PLAT_WITH_SDL_INPUT / BFM_PLAT_WITH_GL / BFM_PLAT_WITH_OPENAL and link the matching
 * backends/ file (the gl renderer also needs backends/gl/bfm_gl_core.c and
 * bfm_gl_exec.c).
 * Texture decoders (BFM_PLAT_WITH_PNG) register in bfm_plat_init, since
 * mods reset clears decoders. */
#include "bfm_plat.h"

#ifdef BFM_PLAT_WITH_PSYCROSS
int bfm_plat_backend_psycross_register(void);
#endif
#ifdef BFM_PLAT_WITH_SDL_AUDIO
int bfm_plat_backend_sdl_audio_register(void);
#endif
#ifdef BFM_PLAT_WITH_PINNED_DISC
int bfm_plat_backend_pinned_disc_register(void);
#endif
#ifdef BFM_PLAT_WITH_OPENAL
int bfm_plat_backend_openal_audio_register(void);
#endif
#ifdef BFM_PLAT_WITH_SDL_INPUT
int bfm_plat_backend_sdl_input_register(void);
#endif
#ifdef BFM_PLAT_WITH_GL
int bfm_plat_backend_gl_register(void);
#endif

int bfm_plat_register_builtin_backends(void) {
    static int done;
    int n = 0;
    if (done) return 0;
    done = 1;
#ifdef BFM_PLAT_WITH_PSYCROSS
    n += bfm_plat_backend_psycross_register() == BFM_PLAT_OK;
#endif
#ifdef BFM_PLAT_WITH_SDL_AUDIO
    n += bfm_plat_backend_sdl_audio_register() == BFM_PLAT_OK;
#endif
#ifdef BFM_PLAT_WITH_PINNED_DISC
    n += bfm_plat_backend_pinned_disc_register() == BFM_PLAT_OK;
#endif
#ifdef BFM_PLAT_WITH_OPENAL
    n += bfm_plat_backend_openal_audio_register() == BFM_PLAT_OK;
#endif
#ifdef BFM_PLAT_WITH_SDL_INPUT
    n += bfm_plat_backend_sdl_input_register() == BFM_PLAT_OK;
#endif
#ifdef BFM_PLAT_WITH_GL
    n += bfm_plat_backend_gl_register() == BFM_PLAT_OK;
#endif
    return n;
}
