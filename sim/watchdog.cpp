#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clock.h"
#include "watchdog.h"

namespace Furble::Sim {
namespace {

// A real-time signal on Linux, where the standard ones are all spoken for by
// SDL and the C++ runtime. macOS has no real-time signals, so the dump rides
// on SIGUSR2 there. Neither is used anywhere else in the simulator.
#if defined(SIGRTMIN)
const int DUMP_SIGNAL = SIGRTMIN + 4;
#else
const int DUMP_SIGNAL = SIGUSR2;
#endif

constexpr unsigned DEFAULT_BOUND_SECONDS = 120;
// Host budget for one thread to enter the dump handler and write its frames.
constexpr auto DUMP_ACK_TIMEOUT = std::chrono::milliseconds(500);
constexpr size_t MAX_FRAMES = 64;

struct WatchedThread {
  pthread_t handle;
  std::string name;
};

std::mutex registryMutex;
std::vector<WatchedThread> registry;

std::mutex runMutex;
std::condition_variable runWake;
std::thread runner;
bool running = false;

std::atomic<unsigned> phaseCounter {0};
std::mutex phaseMutex;
std::string phaseName {"start"};

// Read only by the signal handler, so it must not touch the registry lock.
thread_local const char *handlerThreadName = nullptr;
std::atomic<unsigned> dumpAcks {0};

void writeRaw(const char *text) {
  if (text == nullptr) {
    return;
  }
  const ssize_t written = ::write(STDERR_FILENO, text, std::strlen(text));
  static_cast<void>(written);
}

void dumpHandler(int) {
  void *frames[MAX_FRAMES];
  const int depth = backtrace(frames, MAX_FRAMES);
  writeRaw("\n-- thread ");
  writeRaw(handlerThreadName != nullptr ? handlerThreadName : "unregistered");
  writeRaw(" --\n");
  backtrace_symbols_fd(frames, depth, STDERR_FILENO);
  dumpAcks.fetch_add(1, std::memory_order_release);
}

void installHandler(void) {
  struct sigaction action {};
  action.sa_handler = dumpHandler;
  sigemptyset(&action.sa_mask);
  // SA_RESTART keeps an interrupted sleep or semaphore wait from surfacing
  // EINTR in code that never expects it. The dump only observes the stall.
  action.sa_flags = SA_RESTART;
  sigaction(DUMP_SIGNAL, &action, nullptr);
}

unsigned boundSeconds(void) {
  const char *override = std::getenv("FURBLE_SIM_WATCHDOG_SECONDS");
  if (override == nullptr || override[0] == '\0') {
    return DEFAULT_BOUND_SECONDS;
  }
  return static_cast<unsigned>(std::strtoul(override, nullptr, 10));
}

std::string currentPhase(void) {
  const std::lock_guard<std::mutex> lock(phaseMutex);
  return phaseName;
}

uint64_t progressToken(void) {
  return clockMicros() + furble_sim_scheduler_progress()
         + phaseCounter.load(std::memory_order_acquire);
}

void dumpThread(const WatchedThread &thread) {
  if (pthread_equal(thread.handle, pthread_self())) {
    return;
  }
  const unsigned before = dumpAcks.load(std::memory_order_acquire);
  if (pthread_kill(thread.handle, DUMP_SIGNAL) != 0) {
    std::fprintf(stderr, "\n-- thread %s (gone) --\n", thread.name.c_str());
    return;
  }
  const auto deadline = std::chrono::steady_clock::now() + DUMP_ACK_TIMEOUT;
  while (dumpAcks.load(std::memory_order_acquire) == before) {
    if (std::chrono::steady_clock::now() >= deadline) {
      std::fprintf(stderr, "\n-- thread %s (no response) --\n", thread.name.c_str());
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void reportStall(unsigned bound) {
  std::fflush(stdout);
  std::fprintf(stderr,
               "\nSIM WATCHDOG: no progress for %u host seconds.\n"
               "SIM WATCHDOG: phase=%s virtual_clock_ms=%u scheduler_progress=%llu\n",
               bound, currentPhase().c_str(), clockMillis(),
               static_cast<unsigned long long>(furble_sim_scheduler_progress()));

  std::string tasks;
  furble_sim_report_tasks(tasks);
  std::fputs(tasks.c_str(), stderr);
  std::fflush(stderr);

  std::vector<WatchedThread> snapshot;
  if (registryMutex.try_lock()) {
    snapshot = registry;
    registryMutex.unlock();
  } else {
    std::fprintf(stderr, "SIM WATCHDOG: thread registry is locked, dumping nothing.\n");
  }
  for (const auto &thread : snapshot) {
    dumpThread(thread);
  }
  std::fflush(stderr);
  writeRaw("SIM WATCHDOG: failing the run.\n");
  // The stall is by definition somewhere no orderly shutdown can reach, so do
  // not run one. _Exit skips static destructors that would themselves block.
  std::_Exit(3);
}

void watchdogLoop(unsigned bound) {
  watchdogRegisterThread("watchdog");
  uint64_t last = progressToken();
  auto lastChange = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(runMutex);
  while (running) {
    runWake.wait_for(lock, std::chrono::milliseconds(500), []() { return !running; });
    if (!running) {
      break;
    }
    lock.unlock();
    const uint64_t token = progressToken();
    const auto now = std::chrono::steady_clock::now();
    if (token != last) {
      last = token;
      lastChange = now;
    } else if (now - lastChange >= std::chrono::seconds(bound)) {
      reportStall(bound);
    }
    lock.lock();
  }
  lock.unlock();
  watchdogUnregisterThread();
}

}  // namespace

void watchdogRegisterThread(const char *name) {
  handlerThreadName = name;
  const std::lock_guard<std::mutex> lock(registryMutex);
  registry.push_back({pthread_self(), name == nullptr ? "task" : name});
}

void watchdogUnregisterThread(void) {
  const std::lock_guard<std::mutex> lock(registryMutex);
  for (auto it = registry.begin(); it != registry.end(); ++it) {
    if (pthread_equal(it->handle, pthread_self())) {
      registry.erase(it);
      return;
    }
  }
}

void watchdogStart(void) {
  const unsigned bound = boundSeconds();
  if (bound == 0) {
    return;
  }
  installHandler();
  const std::lock_guard<std::mutex> lock(runMutex);
  if (running) {
    return;
  }
  running = true;
  runner = std::thread(watchdogLoop, bound);
}

void watchdogPhase(const char *phase) {
  {
    const std::lock_guard<std::mutex> lock(phaseMutex);
    phaseName = phase == nullptr ? "unnamed" : phase;
  }
  phaseCounter.fetch_add(1, std::memory_order_release);
}

void watchdogStop(void) {
  {
    const std::lock_guard<std::mutex> lock(runMutex);
    if (!running) {
      return;
    }
    running = false;
  }
  runWake.notify_all();
  if (runner.joinable()) {
    runner.join();
  }
}

}  // namespace Furble::Sim
