#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>

#include <SDL2/SDL.h>

#undef SDL_CreateThread
#undef SDL_Quit
#undef SDL_CreateRenderer
#undef SDL_DestroyRenderer

namespace {

std::mutex debuggerThreadMutex;
SDL_Thread *debuggerThread = nullptr;
std::atomic<bool> suppressedDebugDetector {false};
std::mutex rendererMutex;
std::set<SDL_Renderer *> liveRenderers;

}  // namespace

// M5GFX starts a "dbg" thread that infers "a debugger stopped us" from a 1 ms
// SDL_Delay overshooting 64 ms, and latches Panel_sdl's step-exec mode for the
// next 512 ms. A loaded host overshoots that sleep routinely, so the inference
// is a false positive here, and its consequence is a deadlock: step-exec makes
// Panel_sdl::display() spin until the main thread pumps Panel_sdl::loop(),
// while our main thread will not pump until Platform::init() has registered
// the panel, and that registration is behind the very display() call. Both
// threads then burn a core forever. Automated runs never step in a debugger,
// so the detector is off by default and its whole effect is to reintroduce
// that deadlock. FURBLE_SIM_SDL_STEP_DETECT=1 restores it for an interactive
// debugging session, where the stall watchdog reports the deadlock if it
// happens.
static bool stepDetectEnabled(void) {
  const char *enabled = std::getenv("FURBLE_SIM_SDL_STEP_DETECT");
  return enabled != nullptr && std::strcmp(enabled, "1") == 0;
}

extern "C" SDL_Thread *furble_sim_SDL_CreateThread(SDL_ThreadFunction function,
                                                   const char *name,
                                                   void *data) {
  // The thread name is the only handle M5GFX gives us on that detector: it is a
  // file-static in Panel_sdl.cpp with no accessor. If a future M5GFX renames the
  // thread this match stops firing, the detector starts again, and the boot
  // deadlock comes back. That would no longer hang forever, because the stall
  // watchdog bounds it, but it would be a confusing regression, so the name is
  // pinned by furble_sim_check_step_detect_suppressed() below rather than left
  // to be discovered.
  const bool isDebugDetector = name != nullptr && std::strcmp(name, "dbg") == 0;
  if (isDebugDetector && !stepDetectEnabled()) {
    suppressedDebugDetector.store(true);
    return nullptr;
  }
  SDL_Thread *thread = SDL_CreateThread(function, name, data);
  if (thread != nullptr && isDebugDetector) {
    const std::lock_guard<std::mutex> lock(debuggerThreadMutex);
    debuggerThread = thread;
  }
  return thread;
}

extern "C" SDL_Renderer *furble_sim_SDL_CreateRenderer(SDL_Window *window,
                                                       int index,
                                                       Uint32 flags) {
  SDL_Renderer *renderer = SDL_CreateRenderer(window, index, flags);
  if (renderer != nullptr) {
    const std::lock_guard<std::mutex> lock(rendererMutex);
    liveRenderers.insert(renderer);
  }
  return renderer;
}

extern "C" void furble_sim_SDL_DestroyRenderer(SDL_Renderer *renderer) {
  {
    const std::lock_guard<std::mutex> lock(rendererMutex);
    liveRenderers.erase(renderer);
  }
  SDL_DestroyRenderer(renderer);
}

extern "C" void furble_sim_SDL_Quit(void) {
  // Panel_sdl::close() reaches here with its renderer still live. Destroying a
  // renderer also frees its textures, so the panel texture goes with it.
  std::set<SDL_Renderer *> renderers;
  {
    const std::lock_guard<std::mutex> lock(rendererMutex);
    renderers.swap(liveRenderers);
  }
  for (SDL_Renderer *renderer : renderers) {
    SDL_DestroyRenderer(renderer);
  }
  SDL_Thread *thread = nullptr;
  {
    const std::lock_guard<std::mutex> lock(debuggerThreadMutex);
    thread = debuggerThread;
    debuggerThread = nullptr;
  }
  if (thread != nullptr && SDL_GetThreadID(thread) != SDL_ThreadID()) {
    SDL_WaitThread(thread, nullptr);
  }
  SDL_Quit();
}

// M5GFX's Panel_sdl asks for an accelerated, vsynced renderer. A headless
// dummy or offscreen video driver has none, so SDL_CreateRenderer returns
// NULL and Panel_sdl::_update_scaling() then divides by the renderer output
// size that SDL_GetRendererOutputSize() never wrote. Those are uninitialized
// stack ints, so the window geometry it computes is garbage that changes with
// the compiler: clang 18 made an infinite float of it, clang 21 a signed
// overflow. Under a headless driver ask for the software renderer, which
// exists there and reports a real output size. An explicit SDL_RENDER_DRIVER
// in the environment is left alone rather than relying on hint priority
// rules, which differ between SDL2 and the SDL3 behind sdl2-compat. Visible
// runs are unchanged.
extern "C" void furble_sim_prefer_software_renderer(void) {
  if (std::getenv("SDL_RENDER_DRIVER") != nullptr) {
    return;
  }
  const char *driver = std::getenv("SDL_VIDEODRIVER");
  if (driver != nullptr
      && (std::strcmp(driver, "dummy") == 0 || std::strcmp(driver, "offscreen") == 0)) {
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  }
}

extern "C" void furble_sim_check_step_detect_suppressed(void) {
  if (stepDetectEnabled() || suppressedDebugDetector.load()) {
    return;
  }
  std::fprintf(stderr,
               "sim: no SDL thread named \"dbg\" was suppressed during panel setup. "
               "M5GFX may have renamed its debugger detector, which would let the "
               "Panel_sdl step-exec boot deadlock return. See sim/sdl_lifecycle.cpp.\n");
}
