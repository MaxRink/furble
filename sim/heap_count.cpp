#define FURBLE_SIM_HEAP_COUNT_IMPL
#include "heap_count.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>

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

struct registry_t {
  std::mutex mutex;
  table_t table;
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

size_t usableSize(void *pointer) {
#if defined(__APPLE__)
  return malloc_size(pointer);
#else
  return malloc_usable_size(pointer);
#endif
}

void *countAllocation(void *pointer, size_t size, uint32_t caps = 0) {
  if (pointer == nullptr || !t_DeviceThread || t_HostDepth != 0) {
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
  if (counts.live.load() != 0) {
    counted_t counted;
    bool found = false;
    {
      const std::lock_guard<std::mutex> lock(counts.mutex);
      const auto entry = counts.table.find(pointer);
      if (entry != counts.table.end()) {
        counted = entry->second;
        counts.table.erase(entry);
        counts.live.fetch_sub(1);
        found = true;
      }
    }
    if (found) {
      const Furble::Sim::HostAllocScope scope;
      if (!counted.baseline) {
        Furble::Sim::heapCountedFree(counted.charged, counted.external);
      } else if (Furble::Sim::heapCountingFast()) {
        Furble::Sim::heapRefundBaseline(counted.charged, counted.external);
      }
    }
  }
  std::free(pointer);
}

void *allocate(size_t size, size_t alignment) {
  if (size == 0) {
    size = 1;
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
