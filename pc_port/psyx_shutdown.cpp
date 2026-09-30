// Ordered PsyCross shutdown.
//
// PsyCross's PsyX_Shutdown (pinned e56e4cd) destroys the SDL window, which
// takes the GL context (and, under Mesa EGL, the driver library) with it,
// and only then runs GR_Shutdown, whose glDelete* calls jump into unmapped
// code: every exit segfaulted (exit 139) and at-exit work was lost. This
// performs the same steps with the GL teardown first, while the context is
// still current. The vendored PsyCross checkout stays unmodified.
#include <SDL.h>

extern SDL_Window *g_window;
extern SDL_Thread *g_intrThread;
extern SDL_mutex *g_intrMutex;
extern volatile char g_stopIntrThread;
void GR_Shutdown();
void UnInstallExceptionHandler();
void PsyX_Log_Finalise();

extern "C" void musashi_psyx_shutdown(void) {
    if (!g_window)
        return;
    if (g_intrThread) {
        int value;
        g_stopIntrThread = 1;
        SDL_WaitThread(g_intrThread, &value);
        SDL_DestroyMutex(g_intrMutex);
        g_intrThread = NULL;
    }
    GR_Shutdown();                 // context still current
    SDL_DestroyWindow(g_window);
    g_window = NULL;
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_Quit();
    UnInstallExceptionHandler();
    PsyX_Log_Finalise();
}
