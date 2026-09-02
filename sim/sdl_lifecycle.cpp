#include <cstdlib>
#include <cstring>
#include <mutex>

#include <SDL2/SDL.h>

#undef SDL_CreateThread
#undef SDL_Quit

namespace {

std::mutex debuggerThreadMutex;
SDL_Thread *debuggerThread = nullptr;

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
  const bool isDebugDetector = name != nullptr && std::strcmp(name, "dbg") == 0;
  if (isDebugDetector && !stepDetectEnabled()) {
    return nullptr;
  }
  SDL_Thread *thread = SDL_CreateThread(function, name, data);
  if (thread != nullptr && isDebugDetector) {
    const std::lock_guard<std::mutex> lock(debuggerThreadMutex);
    debuggerThread = thread;
  }
  return thread;
}

extern "C" void furble_sim_SDL_Quit(void) {
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
