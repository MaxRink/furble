#include <atomic>
#include <condition_variable>
#include <mutex>

#include "clock.h"

namespace Furble::Sim {
namespace {

std::atomic<uint64_t> nowMicros {0};
std::mutex schedulerLock;
std::condition_variable schedulerWake;
std::atomic<bool> stopping {false};

}  // namespace

uint32_t clockMillis(void) {
  return static_cast<uint32_t>(nowMicros.load() / 1000U);
}

uint64_t clockMicros(void) {
  return nowMicros.load();
}

void setClockMillis(uint32_t milliseconds) {
  const std::lock_guard<std::mutex> lock(schedulerLock);
  nowMicros.store(static_cast<uint64_t>(milliseconds) * 1000U);
  schedulerWake.notify_all();
}

void advanceClock(uint32_t milliseconds) {
  const std::lock_guard<std::mutex> lock(schedulerLock);
  nowMicros.fetch_add(static_cast<uint64_t>(milliseconds) * 1000U);
  schedulerWake.notify_all();
}

std::mutex &schedulerMutex(void) {
  return schedulerLock;
}

std::condition_variable &schedulerCondition(void) {
  return schedulerWake;
}

bool schedulerStopping(void) {
  return stopping.load();
}

void schedulerStop(void) {
  {
    const std::lock_guard<std::mutex> lock(schedulerLock);
    stopping = true;
  }
  schedulerWake.notify_all();
}

void schedulerReset(void) {
  const std::lock_guard<std::mutex> lock(schedulerLock);
  stopping = false;
}

}  // namespace Furble::Sim
