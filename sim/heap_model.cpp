#include "heap_model.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>

#include "clock.h"

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

struct tagged_alloc_t {
  size_t bytes = 0;
  std::string owner;
  bool external = false;
  bool persistent = false;
};

struct fail_rule_t {
  size_t bytes = 0;
  std::string owner;
  uint32_t count = 0;
};

struct timeline_sample_t {
  uint32_t ms = 0;
  size_t freeBytes = 0;
  size_t largestBlock = 0;
  size_t psramFree = 0;
  size_t psramLargest = 0;
  uint32_t externalTasks = 0;
};

struct heap_state_t {
  std::mutex mutex;
  size_t freeBytes = DEFAULT_FREE;
  size_t largestBlock = DEFAULT_LARGEST;
  size_t minimumFree = DEFAULT_FREE;
  bool active = false;
  size_t minimumLargest = DEFAULT_LARGEST;
  uint32_t allocationFailures = 0;
  uint32_t allocationCount = 0;
  std::string lastFailedTask;
  std::string taskCreateFail;
  std::map<uint64_t, tagged_alloc_t> allocations;
  uint64_t nextAllocationId = 1;
  std::vector<fail_rule_t> failRules;
  std::deque<timeline_sample_t> timeline;
  uint32_t lastTimelineMs = 0;
  std::string psramMode = "absent";
  std::string psramState = "absent";
  size_t psramFree = 0;
  size_t psramLargest = 0;
  bool psramInitFailed = false;
  std::string taskStackRegion = "internal";
  uint32_t externalTasks = 0;
  uint32_t flashWrites = 0;
  bool flashUnsafe = false;
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
  heap.minimumLargest = std::min(heap.minimumLargest, heap.largestBlock);
  heap.allocationCount++;
  return true;
}

void sampleTimelineLocked(bool force) {
  heap_state_t &heap = state();
  const uint32_t now = Furble::Sim::clockMillis();
  if (!force && !heap.timeline.empty() && (now - heap.lastTimelineMs) < 1000) {
    return;
  }
  heap.lastTimelineMs = now;
  heap.timeline.push_back({now, heap.freeBytes, heap.largestBlock, heap.psramFree,
                           heap.psramLargest, heap.externalTasks});
  while (heap.timeline.size() > 256) {
    heap.timeline.pop_front();
  }
}

bool failRuleMatchesLocked(const char *owner, size_t bytes) {
  heap_state_t &heap = state();
  for (auto &rule : heap.failRules) {
    if (rule.count == 0) {
      continue;
    }
    if ((rule.bytes != 0) && (rule.bytes != bytes)) {
      continue;
    }
    if (!rule.owner.empty() && (owner == nullptr || rule.owner != owner)) {
      continue;
    }
    rule.count--;
    return true;
  }
  return false;
}

}  // namespace

namespace Furble::Sim {

void heapSet(size_t freeBytes, size_t largestBlock) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  heap.freeBytes = freeBytes;
  heap.largestBlock = std::min(largestBlock, freeBytes);
  heap.minimumFree = freeBytes;
  heap.minimumLargest = heap.largestBlock;
  heap.active = false;
  heap.timeline.clear();
  heap.lastTimelineMs = 0;
}

void heapActivate(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().active = true;
}

void heapSetPsram(const std::string &mode, size_t freeBytes, size_t largestBlock) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  heap.psramMode = mode;
  if (mode == "init_fail") {
    heap.psramState = "init_failed";
    heap.psramInitFailed = true;
    heap.psramFree = 0;
    heap.psramLargest = 0;
  } else if (mode == "quad" || mode == "octal") {
    heap.psramState = "ready";
    heap.psramInitFailed = false;
    heap.psramFree = freeBytes;
    heap.psramLargest = std::min(largestBlock, freeBytes);
  } else {
    heap.psramState = "absent";
    heap.psramInitFailed = false;
    heap.psramFree = 0;
    heap.psramLargest = 0;
  }
}

bool heapPsramPresent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().psramState == "ready";
}

const char *heapPsramMode(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().psramMode.c_str();
}

const char *heapPsramState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().psramState.c_str();
}

size_t heapPsramFree(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().psramFree;
}

size_t heapPsramLargest(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().psramLargest;
}

bool heapPsramInitFailed(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().psramInitFailed;
}

void heapSetTaskStackRegion(const std::string &region) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().taskStackRegion = region;
}

const char *heapTaskStackRegion(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().taskStackRegion.c_str();
}

void heapFlashWrite(void) {
  bool unsafe = false;
  uint32_t externalTasks = 0;
  {
    const std::lock_guard<std::mutex> lock(state().mutex);
    heap_state_t &heap = state();
    heap.flashWrites++;
    if (heap.externalTasks > 0) {
      heap.flashUnsafe = true;
      unsafe = true;
      externalTasks = heap.externalTasks;
    }
  }
  if (unsafe) {
    ESP_LOGE(LOG_TAG, "SIM FAIL: flash write with %u live PSRAM task stack(s)",
             static_cast<unsigned>(externalTasks));
  }
}

uint32_t heapFlashWrites(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().flashWrites;
}

bool heapFlashUnsafe(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().flashUnsafe;
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

bool heapTaskCreate(const char *name, size_t stackBytes, size_t *chargedBytes, bool *external) {
  if (chargedBytes != nullptr) {
    *chargedBytes = 0;
  }
  if (external != nullptr) {
    *external = false;
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
  const bool usePsram = heap.taskStackRegion == "psram";
  if (!optionalService && !usePsram) {
    return true;
  }
  if (usePsram) {
    if (heap.psramState != "ready") {
      ESP_LOGE(LOG_TAG, "SIM FAIL: PSRAM task stack requested with PSRAM %s",
               heap.psramState.c_str());
      heap.psramInitFailed = true;
      return false;
    }
    if ((bytes > heap.psramFree) || (bytes > heap.psramLargest)) {
      noteFailureLocked(stackBytes, name);
      return false;
    }
    heap.psramFree -= bytes;
    heap.psramLargest = std::min(heap.psramLargest - bytes, heap.psramFree);
    heap.externalTasks++;
    heap.minimumLargest = std::min(heap.minimumLargest, heap.psramLargest);
    if (chargedBytes != nullptr) {
      *chargedBytes = bytes;
    }
    if (external != nullptr) {
      *external = true;
    }
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

void heapReleaseTask(size_t bytes, bool external) {
  if (bytes == 0) {
    return;
  }
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  if (external) {
    heap.psramFree += bytes;
    heap.psramLargest = std::min(heap.psramFree, heap.psramLargest + bytes);
    if (heap.externalTasks > 0) {
      heap.externalTasks--;
    }
    return;
  }
  heap.freeBytes += bytes;
  heap.largestBlock = std::min(heap.freeBytes, heap.largestBlock + bytes);
}

uint64_t heapAllocTagged(size_t bytes, const char *owner, bool external, bool persistent) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  const char *tag = owner == nullptr ? "unknown" : owner;
  if (failRuleMatchesLocked(tag, bytes)) {
    noteFailureLocked(bytes, tag);
    return 0;
  }
  bool charged = false;
  if (external) {
    if (heap.psramState == "ready" && bytes <= heap.psramFree && bytes <= heap.psramLargest) {
      heap.psramFree -= bytes;
      heap.psramLargest = std::min(heap.psramLargest - bytes, heap.psramFree);
      heap.minimumLargest = std::min(heap.minimumLargest, heap.psramLargest);
      charged = true;
    }
  } else if (reserveLocked(bytes)) {
    charged = true;
  }
  if (!charged) {
    noteFailureLocked(bytes, tag);
    return 0;
  }
  const uint64_t id = heap.nextAllocationId++;
  heap.allocations[id] = {bytes, tag, external, persistent};
  sampleTimelineLocked(false);
  return id;
}

void heapFreeTagged(uint64_t id) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  heap_state_t &heap = state();
  const auto found = heap.allocations.find(id);
  if (found == heap.allocations.end()) {
    return;
  }
  const tagged_alloc_t allocation = found->second;
  heap.allocations.erase(found);
  if (allocation.external) {
    heap.psramFree += allocation.bytes;
    heap.psramLargest = std::min(heap.psramFree, heap.psramLargest + allocation.bytes);
    if (heap.externalTasks > 0) {
      heap.externalTasks--;
    }
  } else {
    heap.freeBytes += allocation.bytes;
    heap.largestBlock = std::min(heap.freeBytes, heap.largestBlock + allocation.bytes);
  }
  sampleTimelineLocked(false);
}

void heapSetAllocFailure(size_t bytes, const char *owner, uint32_t count) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().failRules.push_back({bytes, owner == nullptr ? "" : owner, count});
}

void heapSetAllocFailureOnce(size_t bytes, const char *owner) {
  heapSetAllocFailure(bytes, owner, 1);
}

bool heapShouldFailAllocation(size_t bytes, const char *owner) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return failRuleMatchesLocked(owner, bytes);
}

uint32_t heapLeakCount(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  uint32_t count = 0;
  for (const auto &entry : state().allocations) {
    if (!entry.second.persistent) {
      count++;
    }
  }
  return count;
}

std::string heapLeakReport(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  std::ostringstream report;
  bool first = true;
  for (const auto &entry : state().allocations) {
    if (entry.second.persistent) {
      continue;
    }
    if (!first) {
      report << ",";
    }
    first = false;
    report << entry.second.owner << ":" << entry.second.bytes;
  }
  return report.str();
}

std::string heapOwnerReport(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  std::ostringstream report;
  uint64_t index = 0;
  for (const auto &entry : state().allocations) {
    if (index++ != 0) {
      report << ",";
    }
    report << entry.second.owner << ":" << entry.second.bytes
           << (entry.second.external ? ":psram" : ":internal")
           << (entry.second.persistent ? ":lifetime" : ":temporary");
  }
  return report.str();
}

void heapSampleTimeline(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  sampleTimelineLocked(false);
}

std::string heapTimelineJson(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  std::ostringstream json;
  json << "[";
  bool first = true;
  for (const auto &sample : state().timeline) {
    if (!first) {
      json << ",";
    }
    first = false;
    json << "{\"ms\":" << sample.ms << ",\"free\":" << sample.freeBytes
         << ",\"largest\":" << sample.largestBlock << ",\"psram_free\":" << sample.psramFree
         << ",\"psram_largest\":" << sample.psramLargest
         << ",\"external_tasks\":" << sample.externalTasks << "}";
  }
  json << "]";
  return json.str();
}

size_t heapTimelineCount(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().timeline.size();
}

size_t heapMinimumLargest(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().minimumLargest;
}

uint32_t heapAllocationCount(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().allocationCount;
}

}  // namespace Furble::Sim

extern "C" bool furble_sim_heap_should_fail(size_t bytes, const char *owner) {
  return Furble::Sim::heapShouldFailAllocation(bytes, owner);
}

extern "C" size_t furble_sim_heap_free(uint32_t capabilities) {
  if ((capabilities & MALLOC_CAP_SPIRAM) != 0) {
    return Furble::Sim::heapPsramFree();
  }
  return Furble::Sim::heapFree();
}

extern "C" size_t furble_sim_heap_largest(uint32_t capabilities) {
  if ((capabilities & MALLOC_CAP_SPIRAM) != 0) {
    return Furble::Sim::heapPsramLargest();
  }
  return Furble::Sim::heapLargest();
}

extern "C" size_t furble_sim_heap_minimum(uint32_t capabilities) {
  (void)capabilities;
  return Furble::Sim::heapMinimumFree();
}
