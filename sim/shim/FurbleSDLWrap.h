#ifndef FURBLE_SIM_SDL_WRAP_H
#define FURBLE_SIM_SDL_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

SDL_Thread *furble_sim_SDL_CreateThread(SDL_ThreadFunction function,
                                        const char *name,
                                        void *data);
void furble_sim_SDL_Quit(void);

#ifdef __cplusplus
}
#endif

// M5GFX starts a debugger-detection SDL thread but discards its handle. Its
// close path otherwise calls SDL_Quit while that thread can still be inside
// SDL_GetTicks, which crashes orderly simulator exits. Route only these two
// lifecycle calls through a shim that joins the worker before SDL teardown.
#define SDL_CreateThread furble_sim_SDL_CreateThread
#define SDL_Quit furble_sim_SDL_Quit

#endif
