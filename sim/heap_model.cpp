#include "heap_model.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <new>
#include <sstream>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include "clock.h"
#include "heap_count.h"

namespace {

constexpr const char *LOG_TAG = "heap";
// sizeof(StaticTask_t) in the ESP-IDF 5.5.3 Xtensa firmware, read from the
// DWARF of the m5stick-s3 image with the toolchain gdb. xTaskCreate makes two
// pvPortMalloc blocks, the stack and the TCB, each with the TLSF block cost.
constexpr size_t TASK_TCB_BYTES = 336;
// Default host model: a roomy internal pool. Existing scenarios seed nothing,
// so every measured query and every gate stays on the same side it was before
// the model existed.
constexpr size_t DEFAULT_FREE = 200 * 1024;
constexpr size_t DEFAULT_LARGEST = 160 * 1024;

// ESP-IDF 5.5 heap block cost for one allocation on a 32-bit target: the TLSF
// block header (`block_header_overhead`, one size_t) plus the comprehensive
// poisoning head (canary and size) and tail canary (`POISON_OVERHEAD`), with
// the payload rounded to the 4-byte TLSF alignment. See components/heap/tlsf
// and components/heap/multi_heap_poisoning.c in the pinned SDK.
constexpr size_t TLSF_HEADER_BYTES = 4;
constexpr size_t TLSF_ALIGN_BYTES = 4;
constexpr size_t TLSF_MIN_BLOCK_BYTES = 12;
#if defined(CONFIG_HEAP_POISONING_COMPREHENSIVE) || defined(CONFIG_HEAP_POISONING_LIGHT)
constexpr size_t POISON_BYTES = 12;
#else
constexpr size_t POISON_BYTES = 0;
#endif

size_t blockBytes(size_t requested) {
  size_t payload = requested + POISON_BYTES;
  payload = (payload + TLSF_ALIGN_BYTES - 1) & ~(TLSF_ALIGN_BYTES - 1);
  return std::max(payload, TLSF_MIN_BLOCK_BYTES) + TLSF_HEADER_BYTES;
}

// Default malloc routing on boards that let malloc use PSRAM
// (heap_caps_malloc_default). Without it, malloc is internal only.
#if defined(CONFIG_SPIRAM_USE_MALLOC) && defined(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
constexpr bool MALLOC_MAY_USE_PSRAM = true;
constexpr size_t MALLOC_ALWAYSINTERNAL = CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL;
#else
constexpr bool MALLOC_MAY_USE_PSRAM = false;
constexpr size_t MALLOC_ALWAYSINTERNAL = 0;
#endif
constexpr uint32_t CAPS_DEFAULT = 0x1000;

// Tasks that exist only on the host. Their stacks and allocations are not
// device memory.
bool hostOnlyTask(const char *name) {
  return name != nullptr
         && (std::strcmp(name, "ble-radio") == 0 || std::strcmp(name, "psram_probe") == 0);
}

// Internal and external 8-bit capability pairs, as ESP-IDF prints them.
constexpr uint32_t CAPS_INTERNAL = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t CAPS_EXTERNAL = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

// The board sdkconfig decides, as on hardware. Host unit targets that link
// the model without the generated header keep the returning behavior.
#if defined(CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS)
constexpr bool ABORT_ON_FAILURE = true;
#else
constexpr bool ABORT_ON_FAILURE = false;
#endif

// Registered once and read from whichever task fails an allocation. Both are
// constant-initialized, so a registration from another translation unit's
// dynamic initializer (sim/CrashLogSim.cpp) always sees a constructed atomic.
// Clang enforces that at compile time.
#if defined(__clang__)
#define FURBLE_CONSTANT_INIT [[clang::require_constant_initialization]]
#else
#define FURBLE_CONSTANT_INIT
#endif
FURBLE_CONSTANT_INIT std::atomic<esp_alloc_failed_hook_t> s_FailedAllocHook {nullptr};
FURBLE_CONSTANT_INIT std::atomic<Furble::Sim::panic_recorder_t> s_PanicRecorder {nullptr};

/*
 * A failure noted under the heap mutex. The abort runs after the mutex is
 * released, because the crash record and the registered hook may query the
 * heap again.
 */
struct pending_failure_t {
  bool set = false;
  size_t bytes = 0;
  uint32_t caps = 0;
  std::string task;
};
thread_local pending_failure_t t_Pending;

[[noreturn]] void abortOnAllocationFailure(const pending_failure_t &failure) {
  // The abort path allocates; it must not count against the exhausted pool.
  const Furble::Sim::HostAllocScope scope;
  // heap_caps.c calls the registered hook first, then esp_system_abort().
  const esp_alloc_failed_hook_t hook = s_FailedAllocHook.load();
  if (hook != nullptr) {
    hook(failure.bytes, failure.caps, "heap_caps_malloc");
  }
  char line[96];
  std::snprintf(line, sizeof(line), "Mem alloc fail. size 0x%04x caps 0x%08x",
                static_cast<unsigned>(failure.bytes), static_cast<unsigned>(failure.caps));
  std::fprintf(stderr, "abort() was called: %s\n", line);
  std::fprintf(stderr, "SIM PANIC: allocation failure in task %s aborts the device\n",
               failure.task.c_str());
  const Furble::Sim::panic_recorder_t recorder = s_PanicRecorder.load();
  if (recorder != nullptr) {
    recorder(ESP_RST_PANIC, failure.task.c_str(), line);
  }
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(Furble::Sim::SIM_PANIC_EXIT);
}

/*
 * Declared before the heap lock guard in every entry point that can fail, so
 * its destructor runs after the guard has released the mutex.
 */
struct abort_check_t {
  abort_check_t() = default;
  abort_check_t(const abort_check_t &) = delete;
  abort_check_t &operator=(const abort_check_t &) = delete;
  ~abort_check_t() {
    if (!t_Pending.set) {
      return;
    }
    if (ABORT_ON_FAILURE) {
      abortOnAllocationFailure(t_Pending);
    }
    t_Pending = {};
  }
};

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
  bool counting = false;
  size_t countedInternal = 0;
  size_t countedExternal = 0;
  size_t countedTasks = 0;
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

// Never destroyed: counted device allocations are released during static
// destruction too, after a function-local static would already be gone.
heap_state_t &state(void) {
  alignas(heap_state_t) static unsigned char storage[sizeof(heap_state_t)];
  static heap_state_t *instance = new (storage) heap_state_t();
  return *instance;
}

FURBLE_CONSTANT_INIT std::atomic<bool> s_CountingFast {false};

/*
 * The heap model's own bookkeeping allocates under its mutex. Every lock also
 * enters a host scope so that bookkeeping never re-enters the device counter.
 */
struct model_lock_t {
  Furble::Sim::HostAllocScope scope;
  std::lock_guard<std::mutex> guard {state().mutex};
};

void noteFailureLocked(size_t bytes, const char *task, uint32_t caps = CAPS_INTERNAL) {
  heap_state_t &heap = state();
  heap.allocationFailures++;
  heap.lastFailedTask = (task == nullptr || task[0] == '\0') ? "unknown" : task;
  // Keep this line in the shape the ESP-IDF heap component prints on hardware.
  // The internal pair is MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, which is 0x804.
  ESP_LOGE(LOG_TAG, "Mem alloc fail. size 0x%04x caps 0x%08x", static_cast<unsigned>(bytes),
           static_cast<unsigned>(caps));
  t_Pending = {true, bytes, caps, heap.lastFailedTask};
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
  const model_lock_t lock;
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
  const model_lock_t lock;
  state().active = true;
}

void heapCountingActivate(void) {
  const model_lock_t lock;
  state().active = true;
  state().counting = true;
  s_CountingFast.store(true);
}

bool heapCountingFast(void) {
  return s_CountingFast.load(std::memory_order_relaxed);
}

bool heapCounting(void) {
  const model_lock_t lock;
  return state().counting;
}

bool heapHostOnlyTask(const char *name) {
  return hostOnlyTask(name);
}

size_t heapCountedTaskBytes(void) {
  const model_lock_t lock;
  return state().countedTasks;
}

size_t heapCountedBytes(bool external) {
  const model_lock_t lock;
  return external ? state().countedExternal : state().countedInternal;
}

bool heapCountedAlloc(size_t requested, const char *owner, size_t *charged, bool *external) {
  return heapCountedAllocCaps(requested, 0, owner, charged, external);
}

bool heapCountedAllocCaps(size_t requested,
                          uint32_t caps,
                          const char *owner,
                          size_t *charged,
                          bool *external) {
  const abort_check_t abortCheck;
  const model_lock_t lock;
  heap_state_t &heap = state();
  const size_t bytes = blockBytes(requested);
  *charged = 0;
  *external = false;
  const bool psramReady = heap.psramState == "ready";
  if (caps != 0) {
    // An explicit capability request draws only from the pool it names.
    const bool wantExternal = (caps & MALLOC_CAP_SPIRAM) != 0;
    bool ok = false;
    if (wantExternal) {
      ok = psramReady && bytes <= heap.psramFree && bytes <= heap.psramLargest;
      if (ok) {
        heap.psramFree -= bytes;
        heap.psramLargest = std::min(heap.psramLargest - bytes, heap.psramFree);
      }
    } else {
      ok = reserveLocked(bytes);
    }
    if (!ok) {
      noteFailureLocked(requested, owner, caps);
      return false;
    }
    *external = wantExternal;
    *charged = bytes;
    (wantExternal ? heap.countedExternal : heap.countedInternal) += bytes;
    sampleTimelineLocked(false);
    return true;
  }
  const bool psram = MALLOC_MAY_USE_PSRAM && psramReady;
  auto takeExternal = [&heap, bytes]() {
    if (bytes > heap.psramFree || bytes > heap.psramLargest) {
      return false;
    }
    heap.psramFree -= bytes;
    heap.psramLargest = std::min(heap.psramLargest - bytes, heap.psramFree);
    return true;
  };
  bool ok = false;
  if (psram && requested > MALLOC_ALWAYSINTERNAL) {
    ok = takeExternal();
    *external = ok;
    if (!ok) {
      ok = reserveLocked(bytes);
    }
  } else {
    ok = reserveLocked(bytes);
    if (!ok && psram) {
      ok = takeExternal();
      *external = ok;
    }
  }
  if (!ok) {
    // heap_caps_malloc_default reports MALLOC_CAP_DEFAULT once PSRAM may serve
    // malloc; an internal-only board reports DEFAULT | INTERNAL.
    noteFailureLocked(requested, owner, psram ? CAPS_DEFAULT : CAPS_DEFAULT | MALLOC_CAP_INTERNAL);
    return false;
  }
  *charged = bytes;
  if (*external) {
    heap.countedExternal += bytes;
  } else {
    heap.countedInternal += bytes;
  }
  sampleTimelineLocked(false);
  return true;
}

void heapBaselineBlock(size_t requested, uint32_t caps, size_t *bytes, bool *external) {
  const model_lock_t lock;
  heap_state_t &heap = state();
  *bytes = blockBytes(requested);
  const bool psramReady = heap.psramState == "ready";
  if (caps != 0) {
    *external = (caps & MALLOC_CAP_SPIRAM) != 0;
  } else {
    *external = MALLOC_MAY_USE_PSRAM && psramReady && requested > MALLOC_ALWAYSINTERNAL;
  }
}

void heapRefundBaseline(size_t bytes, bool external) {
  const model_lock_t lock;
  heap_state_t &heap = state();
  if (external) {
    heap.psramFree += bytes;
    heap.psramLargest = std::min(heap.psramFree, heap.psramLargest + bytes);
  } else {
    heap.freeBytes += bytes;
    heap.largestBlock = std::min(heap.freeBytes, heap.largestBlock + bytes);
  }
}

void heapCountedFree(size_t charged, bool external) {
  const model_lock_t lock;
  heap_state_t &heap = state();
  if (external) {
    heap.psramFree += charged;
    heap.psramLargest = std::min(heap.psramFree, heap.psramLargest + charged);
    heap.countedExternal -= charged;
  } else {
    heap.freeBytes += charged;
    heap.largestBlock = std::min(heap.freeBytes, heap.largestBlock + charged);
    heap.countedInternal -= charged;
  }
}

void heapSetPsram(const std::string &mode, size_t freeBytes, size_t largestBlock) {
  const model_lock_t lock;
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
  const model_lock_t lock;
  return state().psramState == "ready";
}

const char *heapPsramMode(void) {
  const model_lock_t lock;
  return state().psramMode.c_str();
}

const char *heapPsramState(void) {
  const model_lock_t lock;
  return state().psramState.c_str();
}

size_t heapPsramFree(void) {
  const model_lock_t lock;
  return state().psramFree;
}

size_t heapPsramLargest(void) {
  const model_lock_t lock;
  return state().psramLargest;
}

bool heapPsramInitFailed(void) {
  const model_lock_t lock;
  return state().psramInitFailed;
}

void heapSetTaskStackRegion(const std::string &region) {
  const model_lock_t lock;
  state().taskStackRegion = region;
}

const char *heapTaskStackRegion(void) {
  const model_lock_t lock;
  return state().taskStackRegion.c_str();
}

void heapFlashWrite(void) {
  bool unsafe = false;
  uint32_t externalTasks = 0;
  {
    const model_lock_t lock;
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
  const model_lock_t lock;
  return state().flashWrites;
}

bool heapFlashUnsafe(void) {
  const model_lock_t lock;
  return state().flashUnsafe;
}

void heapSetPanicRecorder(panic_recorder_t recorder) {
  s_PanicRecorder = recorder;
}

bool heapActive(void) {
  const model_lock_t lock;
  return state().active;
}

size_t heapFree(void) {
  const model_lock_t lock;
  return state().freeBytes;
}

size_t heapLargest(void) {
  const model_lock_t lock;
  return state().largestBlock;
}

size_t heapMinimumFree(void) {
  const model_lock_t lock;
  return state().minimumFree;
}

uint32_t heapAllocationFailures(void) {
  const model_lock_t lock;
  return state().allocationFailures;
}

const char *heapLastFailedTask(void) {
  // The caller is the scenario thread at an assertion boundary. Copying under
  // the lock keeps the pointer valid until the next failure.
  static std::string copy;
  const model_lock_t lock;
  copy = state().lastFailedTask;
  return copy.c_str();
}

bool heapReserve(size_t bytes) {
  const model_lock_t lock;
  return reserveLocked(bytes);
}

void heapRelease(size_t bytes) {
  const model_lock_t lock;
  heap_state_t &heap = state();
  heap.freeBytes += bytes;
  // Coalescing is not modelled exactly. Adding the block back to the largest
  // figure is the best case and keeps a bounded query honest: it can never
  // report more than the total free heap.
  heap.largestBlock = std::min(heap.freeBytes, heap.largestBlock + bytes);
}

void heapSetTaskCreateFail(const std::string &name) {
  const model_lock_t lock;
  state().taskCreateFail = name;
}

bool heapTaskCreateMustFail(const char *name) {
  const model_lock_t lock;
  const std::string &fail = state().taskCreateFail;
  if (fail.empty()) {
    return false;
  }
  return fail == "all" || (name != nullptr && fail == name);
}

void heapNoteAllocationFailure(size_t bytes, const char *task) {
  const abort_check_t abortCheck;
  const model_lock_t lock;
  noteFailureLocked(bytes, task);
}

bool heapTaskCreate(const char *name,
                    size_t stackBytes,
                    size_t *chargedBytes,
                    bool *external,
                    bool *counted) {
  if (chargedBytes != nullptr) {
    *chargedBytes = 0;
  }
  if (counted != nullptr) {
    *counted = false;
  }
  if (external != nullptr) {
    *external = false;
  }
  const size_t bytes = blockBytes(stackBytes) + blockBytes(TASK_TCB_BYTES);
  const abort_check_t abortCheck;
  const model_lock_t lock;
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
  // Once general counting starts, every device task stack and TCB is charged.
  // Before it, only the optional services admitted after the seed are.
  const bool deviceTask = heap.counting && !hostOnlyTask(name);
  const bool usePsram = heap.taskStackRegion == "psram";
  if (!optionalService && !deviceTask && !usePsram) {
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
      noteFailureLocked(stackBytes, name, CAPS_EXTERNAL);
      return false;
    }
    heap.psramFree -= bytes;
    if (deviceTask) {
      heap.countedExternal += bytes;
      heap.countedTasks += bytes;
      if (counted != nullptr) {
        *counted = true;
      }
    }
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
  if (deviceTask) {
    heap.countedInternal += bytes;
    heap.countedTasks += bytes;
    if (counted != nullptr) {
      *counted = true;
    }
  }
  return true;
}

void heapReleaseTask(size_t bytes, bool external, bool counted) {
  if (bytes == 0) {
    return;
  }
  const model_lock_t lock;
  heap_state_t &heap = state();
  if (counted) {
    (external ? heap.countedExternal : heap.countedInternal) -= bytes;
    heap.countedTasks -= bytes;
  }
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

uint64_t heapAllocTagged(size_t bytes,
                         const char *owner,
                         bool external,
                         bool persistent,
                         uint32_t caps) {
  const abort_check_t abortCheck;
  const model_lock_t lock;
  heap_state_t &heap = state();
  const char *tag = owner == nullptr ? "unknown" : owner;
  if (caps == 0) {
    caps = external ? CAPS_EXTERNAL : CAPS_INTERNAL;
  }
  if (failRuleMatchesLocked(tag, bytes)) {
    noteFailureLocked(bytes, tag, caps);
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
    noteFailureLocked(bytes, tag, caps);
    return 0;
  }
  const uint64_t id = heap.nextAllocationId++;
  heap.allocations[id] = {bytes, tag, external, persistent};
  sampleTimelineLocked(false);
  return id;
}

void heapFreeTagged(uint64_t id) {
  const model_lock_t lock;
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
  const model_lock_t lock;
  state().failRules.push_back({bytes, owner == nullptr ? "" : owner, count});
}

void heapSetAllocFailureOnce(size_t bytes, const char *owner) {
  heapSetAllocFailure(bytes, owner, 1);
}

bool heapShouldFailAllocation(size_t bytes, const char *owner) {
  const model_lock_t lock;
  return failRuleMatchesLocked(owner, bytes);
}

uint32_t heapLeakCount(void) {
  const model_lock_t lock;
  uint32_t count = 0;
  for (const auto &entry : state().allocations) {
    if (!entry.second.persistent) {
      count++;
    }
  }
  return count;
}

std::string heapLeakReport(void) {
  const model_lock_t lock;
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
  const model_lock_t lock;
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
  const model_lock_t lock;
  sampleTimelineLocked(false);
}

std::string heapTimelineJson(void) {
  const model_lock_t lock;
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
  const model_lock_t lock;
  return state().timeline.size();
}

size_t heapMinimumLargest(void) {
  const model_lock_t lock;
  return state().minimumLargest;
}

uint32_t heapAllocationCount(void) {
  const model_lock_t lock;
  return state().allocationCount;
}

}  // namespace Furble::Sim

extern "C" bool furble_sim_heap_should_fail(size_t bytes, const char *owner) {
  return Furble::Sim::heapShouldFailAllocation(bytes, owner);
}

// Host unit targets link the model without sim/heap_count.cpp; its strong
// definitions replace these in the simulator.
extern "C" __attribute__((weak)) void *furble_sim_caps_alloc(size_t alignment,
                                                             size_t size,
                                                             uint32_t capabilities) {
  void *pointer = nullptr;
  if (posix_memalign(&pointer, alignment, size) != 0) {
    furble_sim_heap_alloc_failed(size, capabilities, "heap_caps");
    return nullptr;
  }
  return pointer;
}

extern "C" __attribute__((weak)) void furble_sim_caps_free(void *pointer) {
  free(pointer);
}

extern "C" void furble_sim_heap_alloc_failed(size_t bytes,
                                             uint32_t capabilities,
                                             const char *owner) {
  const abort_check_t abortCheck;
  const model_lock_t lock;
  noteFailureLocked(bytes, owner, capabilities);
}

extern "C" esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback) {
  if (callback == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  s_FailedAllocHook = callback;
  return ESP_OK;
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
