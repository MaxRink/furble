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

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

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
// Those two blocks are charged as one figure and counted as two, which is what
// multi_heap_info_t.allocated_blocks reports for a live task.
constexpr size_t TASK_BLOCKS = 2;
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

/*
 * Which modelled pool a capability set may draw from.
 *
 * Read from the pinned ESP-IDF 5.5.3 capability table of the modelled ESP32-S3.
 * components/heap/port/esp32s3/memory_layout.c lines 62 to 66 put
 * MALLOC_CAP_DEFAULT on internal DIRAM at priority 0 and on SPIRAM only at
 * priority 2, so a default request is served from internal RAM first and PSRAM
 * second. components/esp_psram/system_layer/esp_psram.c line 470 registers the
 * heap esp_psram actually adds as
 * {MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT, 0, MALLOC_CAP_8BIT |
 * MALLOC_CAP_32BIT | MALLOC_CAP_SIMD}, the same union. MALLOC_CAP_SPIRAM
 * therefore names PSRAM alone, and MALLOC_CAP_INTERNAL or an internal-only bit
 * names internal RAM alone. A set naming both pools keeps the PSRAM reading,
 * the precedence heapCountedAllocCaps already used.
 *
 * The model has one internal pool and no separate IRAM or RTC pool, so
 * MALLOC_CAP_EXEC, MALLOC_CAP_IRAM_8BIT, MALLOC_CAP_RETENTION and
 * MALLOC_CAP_RTCRAM are answered from internal RAM. The S3 boards enable
 * CONFIG_ESP_SYSTEM_MEMPROT_FEATURE, which drops EXEC from DIRAM
 * (memory_layout.c lines 44 to 50), and sim/shim/... has no IRAM heap either
 * way.
 *
 * This over-reports three ways. MALLOC_CAP_RETENTION is a DIRAM capability
 * (memory_layout.c line 45), so the device answers with the DIRAM free size and
 * this answers with the whole merged internal pool. MALLOC_CAP_RTCRAM is its
 * own 8 KiB region and type row (memory_layout.c line 66 and line 106), so the
 * device answers with the RTC fast-memory free size and this answers with the
 * merged internal pool, which holds none of it. And a set no heap carries,
 * such as MALLOC_CAP_SPIRAM together with MALLOC_CAP_INTERNAL, resolves to one
 * pool here where heap_caps_get_* return zero. No caller uses any of them.
 *
 * The priority table above is the allocation order. heap_caps_get_free_size()
 * and its siblings match a different way: heap_caps_match() tests the OR of a
 * heap's whole prioritised set (heap_private.h get_ored_caps, used at
 * heap_caps.c line 73), and esp_psram.c line 470 puts MALLOC_CAP_DEFAULT in
 * PSRAM's first slot, so a DEFAULT query sums both pools on the device. The
 * model agrees with that result by a different route.
 */
constexpr uint32_t CAPS_INTERNAL_ONLY = MALLOC_CAP_EXEC | MALLOC_CAP_DMA | MALLOC_CAP_RETENTION
                                        | MALLOC_CAP_IRAM_8BIT | MALLOC_CAP_RTCRAM | MALLOC_CAP_TCM;

struct caps_pools_t {
  bool internal = true;
  bool external = false;
};

constexpr caps_pools_t capsPools(uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0) {
    return {false, true};
  }
  if ((caps & (MALLOC_CAP_INTERNAL | CAPS_INTERNAL_ONLY)) != 0) {
    return {true, false};
  }
  // Only bits both pools carry (MALLOC_CAP_DEFAULT, 32BIT, 8BIT, SIMD) remain,
  // or an empty set, which heap_caps_match() treats as every heap
  // (components/heap/heap_caps.c line 73).
  return {true, true};
}

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
  // Live charged blocks per pool, for multi_heap_info_t.allocated_blocks. A
  // task contributes two, its stack and its TCB.
  size_t liveInternalBlocks = 0;
  size_t liveExternalBlocks = 0;
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
  size_t psramMinimumFree = 0;
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
  // Placement new into static storage: nothing is allocated or freed.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
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

/*
 * The PSRAM equivalent of reserveLocked, including the pool watermark that
 * furble_sim_heap_minimum() reports for MALLOC_CAP_SPIRAM.
 *
 * It deliberately leaves heap.minimumLargest alone. That figure already mixes
 * the internal and PSRAM largest blocks, and which call sites feed it decides
 * the mem.high_water_largest numbers the memory scenarios pinned, so each
 * caller keeps its own line exactly where it was.
 */
bool takeExternalLocked(size_t bytes) {
  heap_state_t &heap = state();
  if ((heap.psramState != "ready") || (bytes > heap.psramFree) || (bytes > heap.psramLargest)) {
    return false;
  }
  heap.psramFree -= bytes;
  heap.psramLargest = std::min(heap.psramLargest - bytes, heap.psramFree);
  heap.psramMinimumFree = std::min(heap.psramMinimumFree, heap.psramFree);
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

/*
 * Drop charged blocks from a pool's live count. Guarded, because a block freed
 * through a path that never charged it must not wrap the count.
 */
void releaseBlocksLocked(size_t &live, size_t count) {
  live = (live > count) ? (live - count) : 0;
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
  if (caps != 0) {
    /*
     * An explicit capability request draws only from the pools the set names,
     * in the priority order of the pinned ESP32-S3 capability table: internal
     * DIRAM at priority 0, PSRAM at priority 2. A bare MALLOC_CAP_DEFAULT is
     * therefore internal first and PSRAM second, which is heap_caps_malloc()
     * and not heap_caps_malloc_default(): the latter is the caps == 0 path
     * below and follows malloc_alwaysinternal_limit instead.
     */
    const caps_pools_t pools = capsPools(caps);
    bool ok = false;
    bool fromExternal = false;
    if (pools.internal) {
      ok = reserveLocked(bytes);
    }
    if (!ok && pools.external) {
      ok = takeExternalLocked(bytes);
      fromExternal = ok;
    }
    if (!ok) {
      noteFailureLocked(requested, owner, caps);
      return false;
    }
    *external = fromExternal;
    *charged = bytes;
    (fromExternal ? heap.countedExternal : heap.countedInternal) += bytes;
    (fromExternal ? heap.liveExternalBlocks : heap.liveInternalBlocks)++;
    sampleTimelineLocked(false);
    return true;
  }
  // The board sdkconfig decides whether malloc may reach PSRAM at all, and the
  // pool state decides whether it is ready. Reading the state here rather than
  // into a local keeps the only use of it where the constant can be true.
  const bool psram = MALLOC_MAY_USE_PSRAM && (heap.psramState == "ready");
  // Never called on a board built without PSRAM, where psram is constant false.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  auto takeExternal = [bytes]() { return takeExternalLocked(bytes); };
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
    heap.liveExternalBlocks++;
  } else {
    heap.countedInternal += bytes;
    heap.liveInternalBlocks++;
  }
  sampleTimelineLocked(false);
  return true;
}

void heapBaselineBlock(size_t requested, uint32_t caps, size_t *bytes, bool *external) {
  const model_lock_t lock;
  heap_state_t &heap = state();
  *bytes = blockBytes(requested);
  if (caps != 0) {
    // Baseline blocks are uncharged, so this reports the pool a later refund
    // lands in and not the pool a live charge would pick. It knows only the
    // PSRAM bit; see capsPools() before giving it a second pool.
    *external = (caps & MALLOC_CAP_SPIRAM) != 0;
  } else {
    *external =
        MALLOC_MAY_USE_PSRAM && (heap.psramState == "ready") && requested > MALLOC_ALWAYSINTERNAL;
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
    releaseBlocksLocked(heap.liveExternalBlocks, 1);
  } else {
    heap.freeBytes += charged;
    heap.largestBlock = std::min(heap.freeBytes, heap.largestBlock + charged);
    heap.countedInternal -= charged;
    releaseBlocksLocked(heap.liveInternalBlocks, 1);
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
  // A pool that has just been seeded or removed has no deeper watermark yet,
  // the same reset heapSet() does for the internal pool.
  heap.psramMinimumFree = heap.psramFree;
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

size_t heapPsramMinimumFree(void) {
  const model_lock_t lock;
  return state().psramMinimumFree;
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
      // The stack cannot come from the pool the build asked for, so the
      // allocation fails. Record it like the exhaustion path below: the
      // hardware heap_caps_malloc aborts, and a silent false would let a
      // firmware fallback pass for a configuration the device cannot run.
      noteFailureLocked(stackBytes, name, CAPS_EXTERNAL);
      return false;
    }
    if (!takeExternalLocked(bytes)) {
      noteFailureLocked(stackBytes, name, CAPS_EXTERNAL);
      return false;
    }
    if (deviceTask) {
      heap.countedExternal += bytes;
      heap.countedTasks += bytes;
      heap.liveExternalBlocks += TASK_BLOCKS;
      if (counted != nullptr) {
        *counted = true;
      }
    }
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
    heap.liveInternalBlocks += TASK_BLOCKS;
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
    releaseBlocksLocked(external ? heap.liveExternalBlocks : heap.liveInternalBlocks, TASK_BLOCKS);
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
    charged = takeExternalLocked(bytes);
    if (charged) {
      heap.minimumLargest = std::min(heap.minimumLargest, heap.psramLargest);
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

/*
 * The copy bound heap_caps_realloc needs. A host unit target links the model
 * without sim/heap_count.cpp, so this fallback reports the host allocator's
 * usable size for the block the weak furble_sim_caps_alloc made.
 */
extern "C" __attribute__((weak)) size_t furble_sim_caps_block_size(const void *pointer) {
  if (pointer == nullptr) {
    return 0;
  }
#if defined(__APPLE__)
  return malloc_size(pointer);
#else
  return malloc_usable_size(const_cast<void *>(pointer));
#endif
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
  // heap_caps_get_free_size() sums every heap whose capabilities contain the
  // whole set (components/heap/heap_caps.c lines 277 to 287), so a default
  // query is internal plus PSRAM on a board where PSRAM is ready.
  const caps_pools_t pools = capsPools(capabilities);
  size_t bytes = 0;
  if (pools.internal) {
    bytes += Furble::Sim::heapFree();
  }
  if (pools.external) {
    bytes += Furble::Sim::heapPsramFree();
  }
  return bytes;
}

extern "C" size_t furble_sim_heap_largest(uint32_t capabilities) {
  // heap_caps_get_largest_free_block() takes the maximum over the matching
  // heaps rather than summing them: heap_caps_get_largest_free_block() at
  // line 301 calls heap_caps_get_info() at line 367, which takes the MAX at
  // lines 380 and 381.
  const caps_pools_t pools = capsPools(capabilities);
  size_t bytes = 0;
  if (pools.internal) {
    bytes = Furble::Sim::heapLargest();
  }
  if (pools.external) {
    bytes = std::max(bytes, Furble::Sim::heapPsramLargest());
  }
  return bytes;
}

extern "C" bool furble_sim_heap_fits(size_t size, uint32_t capabilities) {
  const model_lock_t lock;
  const heap_state_t &heap = state();
  const size_t bytes = blockBytes(size);
  const caps_pools_t pools = capsPools(capabilities);
  if (pools.internal && (bytes <= heap.freeBytes) && (bytes <= heap.largestBlock)) {
    return true;
  }
  return pools.external && (heap.psramState == "ready") && (bytes <= heap.psramFree)
         && (bytes <= heap.psramLargest);
}

extern "C" size_t furble_sim_heap_minimum(uint32_t capabilities) {
  // heap_caps_get_minimum_free_size() sums the per-region watermarks
  // (components/heap/heap_caps.c lines 289 to 299). MALLOC_CAP_SPIRAM reports
  // the PSRAM watermark, which it did not before.
  const caps_pools_t pools = capsPools(capabilities);
  size_t bytes = 0;
  if (pools.internal) {
    bytes += Furble::Sim::heapMinimumFree();
  }
  if (pools.external) {
    bytes += Furble::Sim::heapPsramMinimumFree();
  }
  return bytes;
}

/*
 * multi_heap_info_t for the modelled pool the caps name, read under one lock so
 * the fields describe a single instant. This resolves one pool per query, which
 * is not what the per-caps getters above now do: they aggregate every pool the
 * capability set matches, and this shim cannot, because one multi_heap_info_t
 * describes one heap. A MALLOC_CAP_8BIT or MALLOC_CAP_DEFAULT read therefore
 * reports the internal pool alone.
 *
 * Every field, the watermark included, comes from that one pool, so a
 * MALLOC_CAP_SPIRAM read and furble_sim_heap_minimum(MALLOC_CAP_SPIRAM) report
 * the same watermark.
 *
 * Three fields describe the model rather than a device TLSF walk:
 * total_allocated_bytes and allocated_blocks count only the blocks the model
 * charged, so a seeded baseline that already includes boot allocations has no
 * block here and the figures are a lower bound. The model carries no
 * fragmentation, so a non-empty pool is one free block.
 */
extern "C" void furble_sim_heap_info(multi_heap_info_t *info, uint32_t capabilities) {
  if (info == nullptr) {
    return;
  }
  const model_lock_t lock;
  const heap_state_t &heap = state();
  const bool external = (capabilities & MALLOC_CAP_SPIRAM) != 0;
  const size_t freeBytes = external ? heap.psramFree : heap.freeBytes;
  info->total_free_bytes = freeBytes;
  info->largest_free_block = external ? heap.psramLargest : heap.largestBlock;
  // The watermark of the pool this query resolved, which is what
  // multi_heap_get_info() reports for one heap. furble_sim_heap_minimum()
  // aggregates the pools the whole set matches, so the two agree on a single
  // pool: a MALLOC_CAP_SPIRAM read returns the PSRAM watermark from both.
  info->minimum_free_bytes = external ? heap.psramMinimumFree : heap.minimumFree;
  info->total_allocated_bytes = external ? heap.countedExternal : heap.countedInternal;
  info->allocated_blocks = external ? heap.liveExternalBlocks : heap.liveInternalBlocks;
  info->free_blocks = (freeBytes > 0) ? 1 : 0;
  info->total_blocks = info->allocated_blocks + info->free_blocks;
}
