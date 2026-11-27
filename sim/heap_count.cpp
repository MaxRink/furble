#define FURBLE_SIM_HEAP_COUNT_IMPL
#include "heap_count.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#include <esp_heap_caps.h>

#include "heap_model.h"

namespace {

thread_local bool t_DeviceThread = false;
thread_local const char *t_DeviceTask = "unknown";
thread_local int t_HostDepth = 0;

struct counted_t {
  size_t charged = 0;
  bool external = false;
  // Allocated before counting started: part of the seeded baseline. Freeing
  // it once counting runs returns its block to the pool, as on the device.
  bool baseline = false;
};

// The table must not allocate through the operator new it serves.
template <typename T>
struct MallocAllocator {
  using value_type = T;
  MallocAllocator() = default;
  template <typename U>
  MallocAllocator(const MallocAllocator<U> &) {}
  T *allocate(size_t count) {
    void *pointer = std::malloc(count * sizeof(T));
    if (pointer == nullptr) {
      std::abort();
    }
    return static_cast<T *>(pointer);
  }
  void deallocate(T *pointer, size_t) { std::free(static_cast<void *>(pointer)); }
  template <typename U>
  bool operator==(const MallocAllocator<U> &) const {
    return true;
  }
  template <typename U>
  bool operator!=(const MallocAllocator<U> &) const {
    return false;
  }
};

using table_t = std::unordered_map<void *,
                                   counted_t,
                                   std::hash<void *>,
                                   std::equal_to<void *>,
                                   MallocAllocator<std::pair<void *const, counted_t>>>;

/*
 * Recently released device blocks, so a second free of the same address is
 * recognised instead of reaching the host allocator, which would abort with
 * "double free detected" or, worse, free a live host block silently. An
 * address leaves this set the moment any allocation hands it out again, which
 * is the tcache key the host allocator itself uses, so a reused address is
 * never reported.
 *
 * Bounded: a long run releases far more blocks than this, and an old address
 * that has since been reused is not evidence of anything. A double free is
 * normally separated by few allocations, and the runner would have to reuse
 * and re-release thousands of blocks between the two frees to hide one.
 */
constexpr size_t FREED_HISTORY_BLOCKS = 4096;

using freed_set_t = std::unordered_set<void *, std::hash<void *>, std::equal_to<void *>,
                                       MallocAllocator<void *>>;
using freed_order_t = std::deque<void *, MallocAllocator<void *>>;

struct registry_t {
  std::mutex mutex;
  table_t table;
  freed_set_t freed;
  freed_order_t freedOrder;
  std::atomic<size_t> live {0};
};

// Never destroyed: allocations are freed during static destruction too.
registry_t &registry(void) {
  alignas(registry_t) static unsigned char storage[sizeof(registry_t)];
  // Placement new into static storage: nothing is allocated or freed.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  static registry_t *instance = new (storage) registry_t();
  return *instance;
}

// Caller holds the registry mutex.
void markAllocatedLocked(registry_t &counts, void *pointer) {
  counts.freed.erase(pointer);
}

// Caller holds the registry mutex.
void markFreedLocked(registry_t &counts, void *pointer) {
  if (!counts.freed.insert(pointer).second) {
    return;
  }
  counts.freedOrder.push_back(pointer);
  while (counts.freedOrder.size() > FREED_HISTORY_BLOCKS) {
    counts.freed.erase(counts.freedOrder.front());
    counts.freedOrder.pop_front();
  }
}

size_t usableSize(void *pointer) {
#if defined(__APPLE__)
  return malloc_size(pointer);
#else
  return malloc_usable_size(pointer);
#endif
}

void *countAllocation(void *pointer, size_t size, uint32_t caps = 0) {
  if (pointer == nullptr) {
    return pointer;
  }
  {
    registry_t &counts = registry();
    const std::lock_guard<std::mutex> lock(counts.mutex);
    markAllocatedLocked(counts, pointer);
  }
  if (!t_DeviceThread || t_HostDepth != 0) {
    return pointer;
  }
  size_t charged = 0;
  bool external = false;
  if (!Furble::Sim::heapCountingFast()) {
    // Record the baseline block without charging it.
    const Furble::Sim::HostAllocScope scope;
    Furble::Sim::heapBaselineBlock(size, caps, &charged, &external);
    registry_t &counts = registry();
    const std::lock_guard<std::mutex> lock(counts.mutex);
    counts.table[pointer] = {charged, external, true};
    counts.live.fetch_add(1);
    return pointer;
  }
  {
    // The heap model logs and samples its timeline; keep that off the count.
    const Furble::Sim::HostAllocScope scope;
    if (!Furble::Sim::heapCountedAllocCaps(size, caps, t_DeviceTask, &charged, &external)) {
      // Only reached when the board has no abort policy.
      std::free(pointer);
      return nullptr;
    }
    registry_t &counts = registry();
    const std::lock_guard<std::mutex> lock(counts.mutex);
    counts.table[pointer] = {charged, external};

    counts.live.fetch_add(1);
  }
  return pointer;
}

void releaseAllocation(void *pointer) {
  if (pointer == nullptr) {
    return;
  }
  registry_t &counts = registry();
  counted_t counted;
  bool found = false;
  bool doubleFree = false;
  {
    const std::lock_guard<std::mutex> lock(counts.mutex);
    const auto entry = counts.table.find(pointer);
    if (entry != counts.table.end()) {
      counted = entry->second;
      counts.table.erase(entry);
      counts.live.fetch_sub(1);
      found = true;
      markFreedLocked(counts, pointer);
    } else {
      // Not a live device block. An address this registry released earlier is
      // a double free, which multi_heap_free aborts on once the freed block's
      // head canary no longer matches; anything else is a host block.
      doubleFree = counts.freed.erase(pointer) != 0;
    }
  }
  if (doubleFree) {
    Furble::Sim::heapNoteDoubleFree(t_DeviceTask);
  }
  if (found) {
    const Furble::Sim::HostAllocScope scope;
    if (!counted.baseline) {
      Furble::Sim::heapCountedFree(counted.charged, counted.external);
    } else if (Furble::Sim::heapCountingFast()) {
      Furble::Sim::heapRefundBaseline(counted.charged, counted.external);
    }
  }
  std::free(pointer);
}

void *allocate(size_t size, size_t alignment) {
  if (size == 0) {
    size = 1;
  }
  // Exact-size and owner-specific failure injection reaches the plain
  // malloc/new path here, the same probe heap_caps_malloc uses. It runs only
  // for an allocation that would otherwise be charged: a device thread outside
  // a host scope. The model's own bookkeeping allocates under a host scope and
  // while it holds the heap mutex, so probing there would re-enter that mutex.
  if (t_DeviceThread && (t_HostDepth == 0)
      && Furble::Sim::heapShouldFailAllocation(size, t_DeviceTask)) {
    furble_sim_heap_alloc_failed(size, 0x1000, t_DeviceTask);
    return nullptr;
  }
  void *pointer = nullptr;
  if (alignment <= alignof(std::max_align_t)) {
    pointer = std::malloc(size);
  } else if (posix_memalign(&pointer, alignment, size) != 0) {
    pointer = nullptr;
  }
  if (pointer == nullptr) {
    // A host allocation failure aborts under the board policy, like TLSF.
    furble_sim_heap_alloc_failed(size, 0x1000, t_DeviceTask);
    return nullptr;
  }
  // Any allocation of this address invalidates an earlier release of it, so the
  // registry records it before the caller can free anything.
  return countAllocation(pointer, size);
}

void *allocateOrThrow(size_t size, size_t alignment) {
  void *pointer = allocate(size, alignment);
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}

}  // namespace

namespace Furble::Sim {

void heapMarkDeviceThread(const char *name) {
  t_DeviceThread = true;
  t_DeviceTask = name == nullptr ? "unknown" : name;
}

DeviceAllocScope::DeviceAllocScope() : m_SavedDepth(t_HostDepth) {
  t_HostDepth = 0;
}

DeviceAllocScope::~DeviceAllocScope() {
  t_HostDepth = m_SavedDepth;
}

}  // namespace Furble::Sim

extern "C" void furble_sim_host_alloc_enter(void) {
  t_HostDepth++;
}

extern "C" void furble_sim_host_alloc_exit(void) {
  t_HostDepth--;
}

extern "C" void *furble_sim_fw_malloc(size_t size) {
  return allocate(size, alignof(std::max_align_t));
}

extern "C" void *furble_sim_caps_alloc(size_t alignment, size_t size, uint32_t capabilities) {
  void *pointer = nullptr;
  if (posix_memalign(&pointer, alignment < sizeof(void *) ? sizeof(void *) : alignment,
                     size == 0 ? 1 : size)
      != 0) {
    furble_sim_heap_alloc_failed(size, capabilities, t_DeviceTask);
    return nullptr;
  }
  // The charge draws from the pool the caps name, as heap_caps_malloc does.
  return countAllocation(pointer, size, capabilities);
}

extern "C" void furble_sim_caps_free(void *pointer) {
  releaseAllocation(pointer);
}

/*
 * The bound heap_caps_realloc may copy from the block it is replacing. The
 * registry records the charge, not the requested size, so report the host
 * allocator's usable size the way furble_sim_fw_realloc does.
 */
extern "C" size_t furble_sim_caps_block_size(const void *pointer) {
  return (pointer == nullptr) ? 0 : usableSize(const_cast<void *>(pointer));
}

extern "C" void furble_sim_fw_free(void *pointer) {
  releaseAllocation(pointer);
}

extern "C" void *furble_sim_fw_realloc(void *pointer, size_t size) {
  if (pointer == nullptr) {
    return furble_sim_fw_malloc(size);
  }
  if (size == 0) {
    furble_sim_fw_free(pointer);
    return nullptr;
  }
  // Allocate, copy and release so the model sees the new block before the old
  // one returns, the order heap_caps_realloc uses when it cannot grow in place.
  void *moved = furble_sim_fw_malloc(size);
  if (moved == nullptr) {
    return nullptr;
  }
  const size_t old = usableSize(pointer);
  std::memcpy(moved, pointer, old < size ? old : size);
  furble_sim_fw_free(pointer);
  return moved;
}

// ThreadSanitizer's static Linux runtime defines every replaceable operator
// new and delete itself, and it must own them to track races on heap memory,
// so the TSan build links its operators instead of these. C++ allocations are
// then not charged to the modelled heap; only the GPS concurrency scenarios
// run on that build, and none of them asserts a heap figure.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define FURBLE_SIM_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define FURBLE_SIM_TSAN 1
#endif
#if !defined(FURBLE_SIM_TSAN)
void *operator new(size_t size) {
  return allocateOrThrow(size, alignof(std::max_align_t));
}
void *operator new[](size_t size) {
  return allocateOrThrow(size, alignof(std::max_align_t));
}
void *operator new(size_t size, const std::nothrow_t &) noexcept {
  return allocate(size, alignof(std::max_align_t));
}
void *operator new[](size_t size, const std::nothrow_t &) noexcept {
  return allocate(size, alignof(std::max_align_t));
}
void *operator new(size_t size, std::align_val_t alignment) {
  return allocateOrThrow(size, static_cast<size_t>(alignment));
}
void *operator new[](size_t size, std::align_val_t alignment) {
  return allocateOrThrow(size, static_cast<size_t>(alignment));
}
void *operator new(size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
  return allocate(size, static_cast<size_t>(alignment));
}
void *operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
  return allocate(size, static_cast<size_t>(alignment));
}
void operator delete(void *pointer) noexcept {
  releaseAllocation(pointer);
}
void operator delete[](void *pointer) noexcept {
  releaseAllocation(pointer);
}
void operator delete(void *pointer, size_t) noexcept {
  releaseAllocation(pointer);
}
void operator delete[](void *pointer, size_t) noexcept {
  releaseAllocation(pointer);
}
void operator delete(void *pointer, const std::nothrow_t &) noexcept {
  releaseAllocation(pointer);
}
void operator delete[](void *pointer, const std::nothrow_t &) noexcept {
  releaseAllocation(pointer);
}
void operator delete(void *pointer, std::align_val_t) noexcept {
  releaseAllocation(pointer);
}
void operator delete[](void *pointer, std::align_val_t) noexcept {
  releaseAllocation(pointer);
}
void operator delete(void *pointer, size_t, std::align_val_t) noexcept {
  releaseAllocation(pointer);
}
void operator delete[](void *pointer, size_t, std::align_val_t) noexcept {
  releaseAllocation(pointer);
}
void operator delete(void *pointer, std::align_val_t, const std::nothrow_t &) noexcept {
  releaseAllocation(pointer);
}
void operator delete[](void *pointer, std::align_val_t, const std::nothrow_t &) noexcept {
  releaseAllocation(pointer);
}
#endif  // !FURBLE_SIM_TSAN
