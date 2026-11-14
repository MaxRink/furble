#ifndef FURBLE_SIM_SDL_WRAP_H
#define FURBLE_SIM_SDL_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

SDL_Thread *furble_sim_SDL_CreateThread(SDL_ThreadFunction function, const char *name, void *data);
void furble_sim_SDL_Quit(void);
SDL_Renderer *furble_sim_SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void furble_sim_SDL_DestroyRenderer(SDL_Renderer *renderer);

#ifdef __cplusplus
}
#endif

// M5GFX starts a debugger-detection SDL thread but discards its handle. Its
// close path otherwise calls SDL_Quit while that thread can still be inside
// SDL_GetTicks, which crashes orderly simulator exits. Route only these two
// lifecycle calls through a shim that joins the worker before SDL teardown.
#define SDL_CreateThread furble_sim_SDL_CreateThread
#define SDL_Quit furble_sim_SDL_Quit
// M5GFX destroys its renderer only when a window is closed, never on
// Panel_sdl::close(), and SDL_Quit does not free a live renderer. The shim
// tracks renderers so SDL_Quit can destroy any that are still live.
#define SDL_CreateRenderer furble_sim_SDL_CreateRenderer
#define SDL_DestroyRenderer furble_sim_SDL_DestroyRenderer

#endif
