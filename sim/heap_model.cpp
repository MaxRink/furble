#include "heap_model.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace {

constexpr const char *LOG_TAG = "heap";
// Host allocation record overhead for one FreeRTOS task: TCB, stack canary and
// alignment. The real figure differs per architecture, so the model charges a
// fixed conservative cost and documents it as an approximation.
constexpr size_t TASK_TCB_BYTES = 512;
// Default host model: a roomy internal pool. Existing scenarios seed nothing,
// so every measured query and every gate stays on the same side it was before
// the model existed.
constexpr size_t DEFAULT_FREE = 200 * 1024;
constexpr size_t DEFAULT_LARGEST = 160 * 1024;

struct heap_state_t {
  std::mutex mutex;
  size_t freeBytes = DEFAULT_FREE;
  size_t largestBlock = DEFAULT_LARGEST;
  size_t minimumFree = DEFAULT_FREE;
  bool active = false;
  uint32_t allocationFailures = 0;
  std::string lastFailedTask;
  std::string taskCreateFail;
};

heap_state_t &state(void) {
  static heap_state_t instance;
  return instance;
}

void noteFailureLocked(size_t bytes, const char *task) {
  heap_state_t &heap = state();
  heap.allocationFailures++;
  heap.lastFailedTask = (task == nullptr || task[0] == '\0') ? "unknown" : task;
  // Keep this line in the shape the ESP-IDF heap component prints on hardware.
  // The caps value is MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, which is 0x804.
  ESP_LOGE(LOG_TAG, "Mem alloc fail. size 0x%04x caps 0x%08x", static_cast<unsigned>(bytes),
           static_cast<unsigned>(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

bool reserveLocked(size_t bytes) {
  heap_state_t &heap = state();
  if ((bytes > heap.freeBytes) || (bytes > heap.largestBlock)) {
    return false;
  }
  heap.freeBytes -= bytes;
  heap.largestBlock = std::min(heap.largestBlock - bytes, heap.freeBytes);
  heap.minimumFree = std::min(heap.minimumFree, heap.freeBytes);
  return true;
}

}  // namespace

namespace Furble::Sim {

void heapSet(size_t freeBytes, size_t largestBlock) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  heap.freeBytes = freeBytes;
  heap.largestBlock = std::min(largestBlock, freeBytes);
  heap.minimumFree = freeBytes;
  heap.active = false;
}

void heapActivate(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().active = true;
}

bool heapActive(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().active;
}

size_t heapFree(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().freeBytes;
}

size_t heapLargest(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().largestBlock;
}

size_t heapMinimumFree(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().minimumFree;
}

uint32_t heapAllocationFailures(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().allocationFailures;
}

const char *heapLastFailedTask(void) {
  // The caller is the scenario thread at an assertion boundary. Copying under
  // the lock keeps the pointer valid until the next failure.
  static std::string copy;
  const std::lock_guard<std::mutex> lock(state().mutex);
  copy = state().lastFailedTask;
  return copy.c_str();
}

bool heapReserve(size_t bytes) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return reserveLocked(bytes);
}

void heapRelease(size_t bytes) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  heap.freeBytes += bytes;
  // Coalescing is not modelled exactly. Adding the block back to the largest
  // figure is the best case and keeps a bounded query honest: it can never
  // report more than the total free heap.
  heap.largestBlock = std::min(heap.freeBytes, heap.largestBlock + bytes);
}

void heapSetTaskCreateFail(const std::string &name) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().taskCreateFail = name;
}

bool heapTaskCreateMustFail(const char *name) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const std::string &fail = state().taskCreateFail;
  if (fail.empty()) {
    return false;
  }
  return fail == "all" || (name != nullptr && fail == name);
}

void heapNoteAllocationFailure(size_t bytes, const char *task) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  noteFailureLocked(bytes, task);
}

bool heapTaskCreate(const char *name, size_t stackBytes, size_t *chargedBytes) {
  if (chargedBytes != nullptr) {
    *chargedBytes = 0;
  }
  const size_t bytes = stackBytes + TASK_TCB_BYTES;
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  // Fault injection is independent of activation so a boot-time task can be
  // failed deliberately too.
  const std::string &fail = heap.taskCreateFail;
  if (!fail.empty() && (fail == "all" || (name != nullptr && fail == name))) {
    // The hardware line names the stack request, not the combined stack+TCB
    // figure. Keep that shape so a log comparison is meaningful.
    noteFailureLocked(stackBytes, name);
    return false;
  }
  if (!heap.active) {
    return true;
  }
  // The seeded pair is a post-boot measurement that already includes WiFi,
  // BLE, the companion service, the display, Control and the UI. Only the
  // optional services admitted after that point consume from it.
  const bool optionalService =
      name != nullptr
      && (std::strcmp(name, "webui") == 0 || std::strcmp(name, "webui_tls") == 0
          || std::strcmp(name, "mqtt") == 0 || std::strcmp(name, "mqtt_client") == 0);
  if (!optionalService) {
    return true;
  }
  if (!reserveLocked(bytes)) {
    noteFailureLocked(stackBytes, name);
    return false;
  }
  if (chargedBytes != nullptr) {
    *chargedBytes = bytes;
  }
  return true;
}

}  // namespace Furble::Sim

extern "C" size_t furble_sim_heap_free(uint32_t capabilities) {
  (void)capabilities;
  return Furble::Sim::heapFree();
}

extern "C" size_t furble_sim_heap_largest(uint32_t capabilities) {
  (void)capabilities;
  return Furble::Sim::heapLargest();
}

extern "C" size_t furble_sim_heap_minimum(uint32_t capabilities) {
  (void)capabilities;
  return Furble::Sim::heapMinimumFree();
}
