// Host coverage for the simulator esp_heap_caps shim.
//
// Plans/337 gap 1: heap_caps_calloc and heap_caps_realloc route through the
// same charge, caps filtering and abort policy as heap_caps_malloc, zero their
// storage and keep the copy semantics a firmware realloc depends on.
//
// Plan 337 gap 3: heap_caps_get_info reports one pool per query, with the same
// byte figures as the per-caps queries for a set that names that pool, so
// src/FurbleConsole.cpp's `perf heap` cannot disagree with `heap.internal.free`.
// The two surfaces diverge for a set that names no pool, which plan 337 records.
//
// The board policy is compiled into this target: every furble sdkconfig sets
// CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS, so a failed allocation aborts the
// device. The abort cases therefore run in a forked child and assert the panic
// exit status and the panic record the parent would otherwise only find in a
// log.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <esp_heap_caps.h>

#include "heap_count.h"
#include "heap_model.h"

// The heap model logs through the simulator crash capture hook.
extern "C" void furble_sim_crash_log_capture(const char *, const char *, va_list) {}

namespace {

using Furble::Sim::SIM_PANIC_EXIT;

constexpr uint32_t CAPS_INTERNAL = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t CAPS_PSRAM = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

int g_Failures = 0;
// Write end of the pipe the forked child reports its panic record through.
int g_ChildReport = -1;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

/*
 * A charged block costs the request plus the TLSF header, the poisoning
 * overhead and the alignment (sim/heap_model.cpp blockBytes). The window is
 * wide enough for any board sdkconfig and tight enough to catch a charge that
 * is missing, or one that charges the old and the new block together.
 */
bool plausibleCharge(size_t requested, size_t charged) {
  return (charged >= requested) && (charged <= (requested + 32));
}

// The forked child's panic recorder: one line through the pipe, no allocation.
void reportPanic(uint32_t resetReason, const char *task, const char *line) {
  if (g_ChildReport < 0) {
    return;
  }
  char buffer[192];
  const int length = std::snprintf(buffer, sizeof(buffer), "reset %u task=%s line=%s\n",
                                   static_cast<unsigned>(resetReason),
                                   task == nullptr ? "unknown" : task, line == nullptr ? "" : line);
  if (length <= 0) {
    return;
  }
  const size_t size = (static_cast<size_t>(length) < sizeof(buffer)) ? static_cast<size_t>(length)
                                                                     : (sizeof(buffer) - 1);
  const ssize_t written = write(g_ChildReport, buffer, size);
  (void)written;
}

void checkInfoMatchesQueries(void) {
  multi_heap_info_t internalInfo = {};
  multi_heap_info_t psramInfo = {};
  heap_caps_get_info(&internalInfo, CAPS_INTERNAL);
  heap_caps_get_info(&psramInfo, CAPS_PSRAM);

  check(internalInfo.total_free_bytes == heap_caps_get_free_size(CAPS_INTERNAL),
        "internal total_free_bytes matches heap_caps_get_free_size");
  check(internalInfo.largest_free_block == heap_caps_get_largest_free_block(CAPS_INTERNAL),
        "internal largest_free_block matches heap_caps_get_largest_free_block");
  check(psramInfo.total_free_bytes == heap_caps_get_free_size(CAPS_PSRAM),
        "PSRAM total_free_bytes matches heap_caps_get_free_size");
  check(psramInfo.largest_free_block == heap_caps_get_largest_free_block(CAPS_PSRAM),
        "PSRAM largest_free_block matches heap_caps_get_largest_free_block");
  // minimum_free_bytes is the watermark of the pool the query resolved, which
  // is what multi_heap_get_info() reports for one heap. A PSRAM read must agree
  // with heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM); before this it
  // returned the internal watermark for either pool.
  check(psramInfo.minimum_free_bytes == heap_caps_get_minimum_free_size(CAPS_PSRAM),
        "PSRAM minimum_free_bytes matches heap_caps_get_minimum_free_size");
  check(internalInfo.minimum_free_bytes == heap_caps_get_minimum_free_size(CAPS_INTERNAL),
        "internal minimum_free_bytes matches heap_caps_get_minimum_free_size");
  check(internalInfo.minimum_free_bytes != psramInfo.minimum_free_bytes,
        "the two pools report their own watermarks");
  // The seeded pools differ by an order of magnitude, so a caps-blind
  // implementation cannot satisfy both checks above.
  check(internalInfo.total_free_bytes < psramInfo.total_free_bytes,
        "the caps select different pools");
  check(internalInfo.free_blocks == 1 && psramInfo.free_blocks == 1,
        "a modelled pool with free bytes is one free block");
  check(internalInfo.total_blocks == internalInfo.allocated_blocks + internalInfo.free_blocks,
        "total_blocks is allocated_blocks plus free_blocks");
}

void checkCallocCharge(void) {
  constexpr size_t COUNT = 16;
  constexpr size_t SIZE = 8;
  constexpr size_t TOTAL = COUNT * SIZE;

  // Dirty a block of the same size and return it, so the calloc below reuses
  // this pattern. glibc hands the block straight back out of the tcache, which
  // is the lane that gates this: the host harness runs on ubuntu-latest. macOS
  // 14 zeroes small blocks on free, so on a macOS developer host the case
  // passes with or without the memset and proves nothing either way.
  void *dirty = heap_caps_malloc(TOTAL, CAPS_PSRAM);
  check(dirty != nullptr, "heap_caps_malloc returns a PSRAM block");
  if (dirty == nullptr) {
    return;
  }
  std::memset(dirty, 0xA5, TOTAL);
  heap_caps_free(dirty);

  const size_t freeBefore = heap_caps_get_free_size(CAPS_PSRAM);
  const size_t largestBefore = heap_caps_get_largest_free_block(CAPS_PSRAM);
  const size_t countedBefore = Furble::Sim::heapCountedBytes(true);
  auto *block = static_cast<uint8_t *>(heap_caps_calloc(COUNT, SIZE, CAPS_PSRAM));
  const size_t freeAfter = heap_caps_get_free_size(CAPS_PSRAM);
  const size_t largestAfter = heap_caps_get_largest_free_block(CAPS_PSRAM);
  const size_t countedAfter = Furble::Sim::heapCountedBytes(true);

  check(block != nullptr, "heap_caps_calloc returns a PSRAM block");
  if (block == nullptr) {
    return;
  }
  const size_t charged = freeBefore - freeAfter;
  check(plausibleCharge(TOTAL, charged), "the calloc charge is the requested block cost");
  check(countedAfter == countedBefore + charged, "the calloc charge is counted in the PSRAM total");
  check(largestBefore == largestAfter + charged,
        "the calloc charge reduces the largest free block");

  bool zeroed = true;
  for (size_t index = 0; index < TOTAL; index++) {
    zeroed = zeroed && (block[index] == 0);
  }
  check(zeroed, "heap_caps_calloc zeroes every byte of a recycled block");

  heap_caps_free(block);
  check(heap_caps_get_free_size(CAPS_PSRAM) == freeBefore,
        "heap_caps_free refunds the calloc charge");
  check(Furble::Sim::heapCountedBytes(true) == countedBefore,
        "heap_caps_free clears the counted calloc charge");
}

void checkCapsFiltering(void) {
  constexpr size_t TOTAL = 16;
  const size_t internalFree = heap_caps_get_free_size(CAPS_INTERNAL);
  const size_t psramFree = heap_caps_get_free_size(CAPS_PSRAM);
  const size_t internalCounted = Furble::Sim::heapCountedBytes(false);
  const size_t psramCounted = Furble::Sim::heapCountedBytes(true);

  void *block = heap_caps_calloc(4, 4, CAPS_INTERNAL);
  const size_t internalAfter = heap_caps_get_free_size(CAPS_INTERNAL);
  const size_t psramAfter = heap_caps_get_free_size(CAPS_PSRAM);

  check(block != nullptr, "heap_caps_calloc returns an internal block");
  if (block == nullptr) {
    return;
  }
  const size_t charged = internalFree - internalAfter;
  check(plausibleCharge(TOTAL, charged), "an INTERNAL calloc charges the internal pool");
  check(psramAfter == psramFree, "an INTERNAL calloc leaves the PSRAM pool untouched");
  check(Furble::Sim::heapCountedBytes(false) == internalCounted + charged,
        "the internal calloc charge is counted as internal");
  check(Furble::Sim::heapCountedBytes(true) == psramCounted,
        "the internal calloc charge is not counted as PSRAM");
  heap_caps_free(block);
}

void checkInfoLedger(void) {
  multi_heap_info_t before = {};
  multi_heap_info_t after = {};
  heap_caps_get_info(&before, CAPS_PSRAM);
  void *block = heap_caps_malloc(48, CAPS_PSRAM);
  heap_caps_get_info(&after, CAPS_PSRAM);

  check(block != nullptr, "heap_caps_malloc returns a PSRAM block");
  if (block == nullptr) {
    return;
  }
  const size_t charged = before.total_free_bytes - after.total_free_bytes;
  check(plausibleCharge(48, charged), "the charge shows in total_free_bytes");
  check(after.total_allocated_bytes == before.total_allocated_bytes + charged,
        "total_allocated_bytes carries the charge");
  check(after.allocated_blocks == before.allocated_blocks + 1,
        "allocated_blocks counts the live block");
  check(after.free_blocks == 1, "the charged pool still reports one free block");
  heap_caps_free(block);

  multi_heap_info_t released = {};
  heap_caps_get_info(&released, CAPS_PSRAM);
  check(released.allocated_blocks + 1 == after.allocated_blocks,
        "freeing the block drops it from allocated_blocks");
  check(released.total_allocated_bytes + charged == after.total_allocated_bytes,
        "freeing the block drops it from total_allocated_bytes");
}

void checkRealloc(void) {
  constexpr size_t OLD = 32;
  constexpr size_t NEW = 64;
  const size_t freeBefore = heap_caps_get_free_size(CAPS_INTERNAL);

  auto *first = static_cast<uint8_t *>(heap_caps_malloc(OLD, CAPS_INTERNAL));
  check(first != nullptr, "heap_caps_malloc returns an internal block");
  if (first == nullptr) {
    return;
  }
  for (size_t index = 0; index < OLD; index++) {
    first[index] = static_cast<uint8_t>(index + 1);
  }

  auto *grown = static_cast<uint8_t *>(heap_caps_realloc(first, NEW, CAPS_INTERNAL));
  check(grown != nullptr, "heap_caps_realloc grows a block");
  if (grown != nullptr) {
    bool kept = true;
    for (size_t index = 0; index < OLD; index++) {
      kept = kept && (grown[index] == static_cast<uint8_t>(index + 1));
    }
    check(kept, "heap_caps_realloc keeps the old contents");
    // The old block is refunded, so the pool carries the new block alone. A
    // realloc that leaked the old charge would show both blocks here.
    const size_t charged = freeBefore - heap_caps_get_free_size(CAPS_INTERNAL);
    check(plausibleCharge(NEW, charged), "the realloc charge is the new block, not both blocks");
    heap_caps_free(grown);
    check(heap_caps_get_free_size(CAPS_INTERNAL) == freeBefore,
          "heap_caps_free refunds the realloc charge");
  } else {
    // The board policy aborts a failed growth, so this only runs without it.
    heap_caps_free(first);
    return;
  }

  void *fresh = heap_caps_realloc(nullptr, 24, CAPS_INTERNAL);
  check(fresh != nullptr, "heap_caps_realloc(NULL, n) allocates");
  if (fresh == nullptr) {
    return;
  }
  check(heap_caps_get_free_size(CAPS_INTERNAL) < freeBefore,
        "heap_caps_realloc(NULL, n) charges the internal pool");
  check(heap_caps_realloc(fresh, 0, CAPS_INTERNAL) == nullptr,
        "heap_caps_realloc(p, 0) frees and returns NULL");
  check(heap_caps_get_free_size(CAPS_INTERNAL) == freeBefore,
        "heap_caps_realloc(p, 0) refunds the pool");
}

enum class AbortCase {
  CallocExhausted,
  CallocInjected,
  CallocOverflow,
  ReallocExhausted,
  PreferExhausted,
  MallocInjected,
  DoubleFree,
};

const char *abortCaseName(AbortCase which) {
  switch (which) {
    case AbortCase::CallocExhausted:
      return "calloc-exhausted";
    case AbortCase::CallocInjected:
      return "calloc-injected";
    case AbortCase::CallocOverflow:
      return "calloc-overflow";
    case AbortCase::ReallocExhausted:
      return "realloc-exhausted";
    case AbortCase::PreferExhausted:
      return "prefer-exhausted";
    case AbortCase::MallocInjected:
      return "malloc-injected";
    case AbortCase::DoubleFree:
      return "double-free";
  }
  return "unknown";
}

// Runs in the forked child. The abort policy ends the process inside the
// failing call, so a normal return past these cases is a defect.
void runAbortCase(AbortCase which) {
  if (which == AbortCase::CallocExhausted) {
    // A PSRAM request on a board with no PSRAM cannot be served, which is the
    // shape of the journal allocation on a StickS3 whose PSRAM is absent.
    Furble::Sim::heapSetPsram("absent", 0, 0);
    (void)heap_caps_calloc(64, 4, CAPS_PSRAM);
  } else if (which == AbortCase::CallocInjected) {
    // The exact-size fault probe must see the calloc total, not the count.
    Furble::Sim::heapSetPsram("octal", 1024 * 1024, 512 * 1024);
    Furble::Sim::heapSetAllocFailureOnce(256, "heap_caps");
    (void)heap_caps_calloc(64, 4, CAPS_PSRAM);
  } else if (which == AbortCase::CallocOverflow) {
    // A count that overflows size_t is rejected before any pool is touched,
    // and the hook is handed the wrapped total as the device hands it.
    (void)heap_caps_calloc(SIZE_MAX / 2, 4, CAPS_PSRAM);
  } else if (which == AbortCase::ReallocExhausted) {
    // Growing past the modelled pool fails, and the old block stays valid.
    Furble::Sim::heapSetPsram("octal", 8192, 4096);
    void *block = heap_caps_malloc(32, CAPS_PSRAM);
    if (block == nullptr) {
      _exit(91);
    }
    (void)heap_caps_realloc(block, 8192, CAPS_PSRAM);
  } else if (which == AbortCase::PreferExhausted) {
    // The display buffer path asks for PSRAM and then the default set. Both
    // pools are too small, so the last set's failure aborts the run.
    Furble::Sim::heapSet(64, 64);
    Furble::Sim::heapSetPsram("absent", 0, 0);
    (void)heap_caps_malloc_prefer(4096, 2, CAPS_PSRAM, MALLOC_CAP_DEFAULT);
  } else if (which == AbortCase::MallocInjected) {
    // The plain malloc/new path takes the same probe. The owner is the device
    // task, so the rule names the task the request came from.
    Furble::Sim::heapSetAllocFailureOnce(128, "heap-caps-test");
    (void)furble_sim_fw_malloc(128);
  } else {
    // A block this registry released is freed again. multi_heap_free aborts
    // on an already-freed block, so the model must too, instead of handing the
    // pointer to the host allocator.
    void *block = furble_sim_fw_malloc(64);
    if (block == nullptr) {
      _exit(93);
    }
    furble_sim_fw_free(block);
    furble_sim_fw_free(block);
  }
  _exit(92);
}

struct panic_expectation_t {
  const char *task = nullptr;
  // Substring of the line the panic record kept.
  const char *line = nullptr;
  // Substring of the recorded allocation size, or null when the failure is not
  // an allocation.
  const char *size = nullptr;
  const char *caps = nullptr;
};

void checkAbortCase(AbortCase which, const panic_expectation_t &expectation) {
  int fds[2] = {-1, -1};
  if (pipe(fds) != 0) {
    std::cerr << "  FAIL: " << abortCaseName(which) << " could not create a pipe\n";
    g_Failures++;
    return;
  }

  std::fflush(nullptr);
  const pid_t child = fork();
  if (child < 0) {
    close(fds[0]);
    close(fds[1]);
    std::cerr << "  FAIL: " << abortCaseName(which) << " could not fork\n";
    g_Failures++;
    return;
  }
  if (child == 0) {
    close(fds[0]);
    g_ChildReport = fds[1];
    Furble::Sim::heapSetPanicRecorder(reportPanic);
    runAbortCase(which);
  }

  close(fds[1]);
  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    close(fds[0]);
    std::cerr << "  FAIL: " << abortCaseName(which) << " could not be reaped\n";
    g_Failures++;
    return;
  }
  char record[512] = {};
  const ssize_t got = read(fds[0], record, sizeof(record) - 1);
  close(fds[0]);
  if (got <= 0) {
    record[0] = '\0';
  }

  if (!WIFEXITED(status)) {
    std::cerr << "  FAIL: " << abortCaseName(which) << " did not exit normally\n";
    g_Failures++;
    return;
  }
  const int code = WEXITSTATUS(status);
  if (code != SIM_PANIC_EXIT) {
    std::cerr << "  FAIL: " << abortCaseName(which) << " exited " << code << ", expected the panic "
              << "status " << SIM_PANIC_EXIT << '\n';
    g_Failures++;
    return;
  }
  if (expectation.line != nullptr) {
    check(std::strstr(record, expectation.line) != nullptr,
          "the abort writes the modelled failure line to the panic record");
  }
  if (expectation.size != nullptr) {
    check(std::strstr(record, expectation.size) != nullptr,
          "the panic record carries the requested block size");
  }
  if (expectation.caps != nullptr) {
    check(std::strstr(record, expectation.caps) != nullptr,
          "the panic record carries the requested capability pair");
  }
  if (expectation.task != nullptr && std::strstr(record, expectation.task) == nullptr) {
    std::cerr << "  FAIL: " << abortCaseName(which) << " did not name task " << expectation.task
              << ": " << record;
    g_Failures++;
  }
}

}  // namespace

int main(void) {
  std::cout << "test: simulator heap_caps shim charge, caps, realloc and get_info\n";

  // Seeded pools that differ by an order of magnitude, so a caps-blind
  // implementation fails the agreement checks below.
  Furble::Sim::heapSet(60000, 30000);
  Furble::Sim::heapSetPsram("octal", 1024 * 1024, 512 * 1024);
  checkInfoMatchesQueries();

  // Charge accounting only exists once the device counter runs, and only for
  // the thread it counts.
  Furble::Sim::heapCountingActivate();
  Furble::Sim::heapMarkDeviceThread("heap-caps-test");
  checkCallocCharge();
  checkCapsFiltering();
  checkRealloc();
  checkInfoLedger();

  // Under the board policy the failing calls abort. The window (64 * 4 = 256)
  // is the requested block; the fault probe reports the same total.
  checkAbortCase(AbortCase::CallocExhausted,
                 {"task=heap-caps-test", "line=Mem alloc fail.", "size 0x0100",
                  "caps 0x00000404"});
  checkAbortCase(AbortCase::CallocInjected,
                 {"task=heap_caps", "line=Mem alloc fail.", "size 0x0100", "caps 0x00000404"});
  checkAbortCase(AbortCase::ReallocExhausted,
                 {"task=heap-caps-test", "line=Mem alloc fail.", "size 0x2000",
                  "caps 0x00000404"});
  // A count that overflows the block size aborts before any pool is charged,
  // and the hook sees the wrapped total the device reports: the 32-bit
  // truncation of (SIZE_MAX / 2) * 4.
  checkAbortCase(AbortCase::CallocOverflow,
                 {"task=heap_caps_calloc", "line=Mem alloc fail.", "size 0xfffffffc",
                  "caps 0x00000404"});
  // The display buffer's prefer list fails on its last set, which is the
  // variadic path no host test reached before.
  checkAbortCase(AbortCase::PreferExhausted,
                 {"task=heap_caps_malloc_prefer", "line=Mem alloc fail.", "size 0x1000",
                  "caps 0x00001000"});
  // The plain malloc/new path aborts through the same probe, and reports the
  // MALLOC_CAP_DEFAULT pair a firmware `new` prints on the device.
  checkAbortCase(AbortCase::MallocInjected,
                 {"task=heap-caps-test", "line=Mem alloc fail.", "size 0x0080",
                  "caps 0x00001000"});
  // A block freed twice aborts like multi_heap_free does, and the record names
  // the modelled line instead of an allocation size.
  checkAbortCase(AbortCase::DoubleFree,
                 {"task=heap-caps-test", "line=Heap block freed twice"});

  if (g_Failures != 0) {
    std::cerr << "FAIL: " << g_Failures << " heap_caps shim check(s)\n";
    return 1;
  }
  std::cout << "sim-heap-caps-shim: PASS\n";
  return 0;
}
