#include <cstring>
#include <mutex>

#include <SDL2/SDL.h>

#undef SDL_CreateThread
#undef SDL_Quit

namespace {

std::mutex debuggerThreadMutex;
SDL_Thread *debuggerThread = nullptr;

}  // namespace

extern "C" SDL_Thread *furble_sim_SDL_CreateThread(SDL_ThreadFunction function,
                                                   const char *name,
                                                   void *data) {
  SDL_Thread *thread = SDL_CreateThread(function, name, data);
  if (thread != nullptr && name != nullptr && std::strcmp(name, "dbg") == 0) {
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
