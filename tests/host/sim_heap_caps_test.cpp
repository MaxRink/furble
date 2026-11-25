// Host coverage for the simulator heap capability model.
//
// Two things are pinned here:
//
// 1. The MALLOC_CAP_* values in sim/shim/esp_heap_caps.h. They must be the
//    exact bits the pinned ESP-IDF defines, because the model prints them in
//    the modelled allocation-failure line and a scenario compares that line
//    with what the device reports.
// 2. Which modelled pool each capability set draws from. The rule comes from
//    the priority table of the modelled ESP32-S3, not from this file:
//    components/heap/port/esp32s3/memory_layout.c lines 62 to 66 put
//    MALLOC_CAP_DEFAULT on DIRAM at priority 0 and on SPIRAM only at priority
//    2, and components/esp_psram/system_layer/esp_psram.c line 470 registers
//    the PSRAM heap as {MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT, 0,
//    MALLOC_CAP_8BIT | MALLOC_CAP_32BIT | MALLOC_CAP_SIMD}.
//
// Nothing here needs LVGL, SDL or a device: the target links the heap model and
// its clock only. The generated sdkconfig header is deliberately absent, so
// this exercises the plain host profile, where the board's PSRAM routing and
// abort policy are compiled out.

#include <cstdarg>
#include <cstring>
#include <iostream>
#include <string>

#include <esp_heap_caps.h>

#include "heap_model.h"

// The heap model logs through the simulator crash capture hook.
extern "C" void furble_sim_crash_log_capture(const char *, const char *, va_list) {}

// ---------------------------------------------------------------------------
// The pinned capability bits.
//
// Every expected value is transcribed from components/heap/include/
// esp_heap_caps.h lines 29 to 51 of framework-espidf 3.50503.0 (ESP-IDF
// 5.5.3). A static_assert fails the build rather than a run, so a drift shows
// up in every target that includes the shim.
// ---------------------------------------------------------------------------

static_assert(MALLOC_CAP_EXEC == 0x00000001, "MALLOC_CAP_EXEC is 1<<0");
static_assert(MALLOC_CAP_32BIT == 0x00000002, "MALLOC_CAP_32BIT is 1<<1");
static_assert(MALLOC_CAP_8BIT == 0x00000004, "MALLOC_CAP_8BIT is 1<<2");
static_assert(MALLOC_CAP_DMA == 0x00000008, "MALLOC_CAP_DMA is 1<<3");
static_assert(MALLOC_CAP_PID2 == 0x00000010, "MALLOC_CAP_PID2 is 1<<4");
static_assert(MALLOC_CAP_PID3 == 0x00000020, "MALLOC_CAP_PID3 is 1<<5");
static_assert(MALLOC_CAP_PID4 == 0x00000040, "MALLOC_CAP_PID4 is 1<<6");
static_assert(MALLOC_CAP_PID5 == 0x00000080, "MALLOC_CAP_PID5 is 1<<7");
static_assert(MALLOC_CAP_PID6 == 0x00000100, "MALLOC_CAP_PID6 is 1<<8");
static_assert(MALLOC_CAP_PID7 == 0x00000200, "MALLOC_CAP_PID7 is 1<<9");
static_assert(MALLOC_CAP_SPIRAM == 0x00000400, "MALLOC_CAP_SPIRAM is 1<<10");
static_assert(MALLOC_CAP_INTERNAL == 0x00000800, "MALLOC_CAP_INTERNAL is 1<<11");
static_assert(MALLOC_CAP_DEFAULT == 0x00001000, "MALLOC_CAP_DEFAULT is 1<<12");
static_assert(MALLOC_CAP_IRAM_8BIT == 0x00002000, "MALLOC_CAP_IRAM_8BIT is 1<<13");
static_assert(MALLOC_CAP_RETENTION == 0x00004000, "MALLOC_CAP_RETENTION is 1<<14");
static_assert(MALLOC_CAP_RTCRAM == 0x00008000, "MALLOC_CAP_RTCRAM is 1<<15");
static_assert(MALLOC_CAP_TCM == 0x00010000, "MALLOC_CAP_TCM is 1<<16");
static_assert(MALLOC_CAP_DMA_DESC_AHB == 0x00020000, "MALLOC_CAP_DMA_DESC_AHB is 1<<17");
static_assert(MALLOC_CAP_DMA_DESC_AXI == 0x00040000, "MALLOC_CAP_DMA_DESC_AXI is 1<<18");
static_assert(MALLOC_CAP_CACHE_ALIGNED == 0x00080000, "MALLOC_CAP_CACHE_ALIGNED is 1<<19");
static_assert(MALLOC_CAP_SIMD == 0x00100000, "MALLOC_CAP_SIMD is 1<<20");
static_assert(MALLOC_CAP_INVALID == 0x80000000, "MALLOC_CAP_INVALID is 1<<31");

// The two sets the firmware and its log lines actually use. The internal pair
// is what a StickS3 panic prints, and the default pair is what the display
// allocation path asks for.
static_assert((MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) == 0x804,
              "the internal 8-bit pair is the 0x804 a device panic reports");
static_assert((MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL) == 0x1800,
              "MALLOC_CAP_DEFAULT plus MALLOC_CAP_INTERNAL is 0x1800");

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

void checkSize(size_t actual, size_t expected, const char *message) {
  if (actual != expected) {
    std::cerr << "  FAIL: " << message << " (got " << actual << ", want " << expected << ")\n";
    g_Failures++;
  }
}

constexpr uint32_t CAPS_INTERNAL = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t CAPS_EXTERNAL = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

// Seed both pools and read them back through the shim entry points.
void testQueryFiltering(void) {
  using namespace Furble::Sim;

  heapSet(60000, 30000);
  heapSetPsram("octal", 1000000, 500000);

  checkSize(furble_sim_heap_free(CAPS_INTERNAL), 60000,
            "MALLOC_CAP_INTERNAL must report the internal pool only");
  checkSize(furble_sim_heap_free(MALLOC_CAP_INTERNAL), 60000,
            "MALLOC_CAP_INTERNAL alone must report the internal pool only");
  checkSize(furble_sim_heap_free(CAPS_EXTERNAL), 1000000,
            "MALLOC_CAP_SPIRAM must report the PSRAM pool only");
  checkSize(furble_sim_heap_free(MALLOC_CAP_SPIRAM), 1000000,
            "MALLOC_CAP_SPIRAM alone must report the PSRAM pool only");
  checkSize(furble_sim_heap_free(MALLOC_CAP_DEFAULT), 1060000,
            "MALLOC_CAP_DEFAULT must sum internal and PSRAM, as heap_caps_get_free_size does");

  checkSize(furble_sim_heap_largest(CAPS_INTERNAL), 30000,
            "the internal largest block is the internal figure");
  checkSize(furble_sim_heap_largest(MALLOC_CAP_SPIRAM), 500000,
            "the PSRAM largest block is the PSRAM figure");
  checkSize(furble_sim_heap_largest(MALLOC_CAP_DEFAULT), 500000,
            "MALLOC_CAP_DEFAULT must take the maximum, not the sum");

  // Spend 20000 internal and 400000 PSRAM, then check the watermarks.
  checkSize(furble_sim_heap_minimum(CAPS_INTERNAL), 60000,
            "the internal watermark starts at the seed");
  checkSize(furble_sim_heap_minimum(MALLOC_CAP_SPIRAM), 1000000,
            "the PSRAM watermark starts at the seed");
  checkSize(furble_sim_heap_minimum(MALLOC_CAP_DEFAULT), 1060000,
            "the default watermark starts at the two seeds");

  size_t charged = 0;
  bool external = false;
  check(heapCountedAllocCaps(20000, CAPS_INTERNAL, "probe", &charged, &external),
        "an internal charge with room must succeed");
  check(!external, "an internal charge must not land in PSRAM");
  check(heapCountedAllocCaps(400000, CAPS_EXTERNAL, "probe", &charged, &external),
        "a PSRAM charge with room must succeed");
  check(external, "a MALLOC_CAP_SPIRAM charge must land in PSRAM");

  const size_t internalFree = heapFree();
  const size_t psramFree = heapPsramFree();
  checkSize(furble_sim_heap_minimum(CAPS_INTERNAL), internalFree,
            "MALLOC_CAP_INTERNAL must report the internal watermark, not the PSRAM one");
  checkSize(furble_sim_heap_minimum(MALLOC_CAP_SPIRAM), psramFree,
            "MALLOC_CAP_SPIRAM must report the PSRAM watermark, not the internal one");
  checkSize(furble_sim_heap_minimum(MALLOC_CAP_DEFAULT), internalFree + psramFree,
            "MALLOC_CAP_DEFAULT must sum the two watermarks");

  // A pool that is reseeded has no deeper watermark than its new level.
  heapSet(70000, 40000);
  checkSize(furble_sim_heap_minimum(CAPS_INTERNAL), 70000,
            "heapSet() must reset the internal watermark");
  heapSetPsram("octal", 900000, 450000);
  checkSize(furble_sim_heap_minimum(MALLOC_CAP_SPIRAM), 900000,
            "heapSetPsram() must reset the PSRAM watermark");
}

// A default capability request goes internal first, then PSRAM. This is
// heap_caps_malloc(size, MALLOC_CAP_DEFAULT), which walks the priority table,
// and it is not the same rule as malloc(): heap_caps_malloc_default() sends a
// large request to PSRAM first once malloc_alwaysinternal_limit is set.
void testDefaultRouting(void) {
  using namespace Furble::Sim;

  heapSet(60000, 30000);
  heapSetPsram("octal", 1000000, 500000);

  size_t charged = 0;
  bool external = false;
  check(heapCountedAllocCaps(4096, MALLOC_CAP_DEFAULT, "probe", &charged, &external),
        "a default charge must succeed with both pools empty of traffic");
  check(!external, "a default charge must come from internal RAM while internal RAM fits");
  checkSize(heapPsramFree(), 1000000, "a default charge must not touch PSRAM while internal fits");

  // Exhaust internal RAM. The same request must now be served by PSRAM, which
  // carries MALLOC_CAP_DEFAULT at priority 2.
  heapSet(0, 0);
  check(heapCountedAllocCaps(4096, MALLOC_CAP_DEFAULT, "probe", &charged, &external),
        "a default charge must fall back to PSRAM when internal RAM cannot fit it");
  check(external, "the default fallback must be charged to the PSRAM pool");
  checkSize(heapPsramFree(), 1000000 - charged, "the PSRAM fallback must be charged to PSRAM");

  // MALLOC_CAP_INTERNAL anywhere in the set pins the request to internal RAM,
  // which is 0x1800 and the set hardware_model.cpp records for the icon malloc.
  heapSet(0, 0);
  heapSetPsram("octal", 1000000, 500000);
  check(!heapCountedAllocCaps(4096, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL, "probe", &charged,
                              &external),
        "MALLOC_CAP_INTERNAL must keep a default charge out of PSRAM");
  checkSize(heapPsramFree(), 1000000, "a pinned internal request must not touch PSRAM");

  // MALLOC_CAP_SPIRAM stays PSRAM-only even when internal RAM has room.
  heapSet(60000, 30000);
  check(heapCountedAllocCaps(4096, CAPS_EXTERNAL, "probe", &charged, &external),
        "a PSRAM charge must succeed with PSRAM ready");
  check(external, "MALLOC_CAP_SPIRAM must never be served from internal RAM");
  checkSize(heapFree(), 60000, "a PSRAM charge must not consume internal RAM");
}

// With PSRAM absent the default set collapses onto internal RAM, so the model
// keeps the single-pool behaviour the internal-only boards rely on.
void testDefaultRoutingWithoutPsram(void) {
  using namespace Furble::Sim;

  heapSet(60000, 30000);
  heapSetPsram("absent", 0, 0);

  checkSize(furble_sim_heap_free(MALLOC_CAP_DEFAULT), 60000,
            "without PSRAM a default query must report internal RAM only");
  checkSize(furble_sim_heap_largest(MALLOC_CAP_DEFAULT), 30000,
            "without PSRAM a default largest query must report the internal figure");
  checkSize(furble_sim_heap_minimum(MALLOC_CAP_DEFAULT), 60000,
            "without PSRAM a default watermark must report the internal figure");

  size_t charged = 0;
  bool external = false;
  check(heapCountedAllocCaps(4096, MALLOC_CAP_DEFAULT, "probe", &charged, &external),
        "a default charge must succeed from internal RAM without PSRAM");
  check(!external, "a default charge without PSRAM must stay internal");
  checkSize(heapFree(), 60000 - charged, "the charge must come off the internal pool");

  // heapSetPsram() with an init failure must keep the pool unusable and still
  // answer a MALLOC_CAP_SPIRAM query with zero rather than the internal figure.
  heapSet(60000, 30000);
  heapSetPsram("init_fail", 0, 0);
  checkSize(furble_sim_heap_free(MALLOC_CAP_SPIRAM), 0,
            "a failed PSRAM init must report an empty PSRAM pool");
  checkSize(furble_sim_heap_minimum(MALLOC_CAP_SPIRAM), 0,
            "a failed PSRAM init must report an empty PSRAM watermark");
  checkSize(furble_sim_heap_free(MALLOC_CAP_DEFAULT), 60000,
            "a failed PSRAM init must leave the default query on internal RAM");
}

// heap_caps_malloc_prefer() checks each set's pool before charging it and
// aborts only when none fits. With PSRAM absent the PSRAM set must be skipped
// and the default set must serve the request from internal RAM.
void testPreferOrder(void) {
  using namespace Furble::Sim;

  heapSet(60000, 30000);
  heapSetPsram("octal", 1000000, 500000);
  check(furble_sim_heap_fits(4096, CAPS_EXTERNAL), "the PSRAM set must fit a seeded PSRAM pool");
  check(furble_sim_heap_fits(4096, MALLOC_CAP_DEFAULT),
        "the default set must fit a seeded internal pool");

  heapSet(0, 0);
  check(!furble_sim_heap_fits(4096, CAPS_INTERNAL),
        "the internal set must not fit an exhausted internal pool");
  check(furble_sim_heap_fits(4096, MALLOC_CAP_DEFAULT),
        "the default set must still fit through PSRAM when internal RAM is exhausted");

  heapSet(60000, 30000);
  heapSetPsram("absent", 0, 0);
  check(!furble_sim_heap_fits(4096, CAPS_EXTERNAL), "the PSRAM set must not fit without PSRAM");
  check(furble_sim_heap_fits(4096, MALLOC_CAP_DEFAULT),
        "the default set must fit through internal RAM without PSRAM");
  check(!furble_sim_heap_fits(200000, MALLOC_CAP_DEFAULT),
        "the default set must not fit a block larger than the only live pool");
}

/*
 * The model over-reports one shape of capability set, and this pins it so the
 * disclosure cannot rot. No heap on the modelled ESP32-S3 carries both
 * MALLOC_CAP_SPIRAM and MALLOC_CAP_INTERNAL (memory_layout.c lines 62 to 66),
 * so heap_caps_get_free_size() returns 0 for that set on hardware. capsPools()
 * resolves it to the PSRAM pool instead. Correcting the model must change this
 * assertion and the disclosure in plans/314-memory-resilience.md and
 * sim/CLAUDE.md together.
 */
void testOverReportedCaps(void) {
  using namespace Furble::Sim;

  heapSet(60000, 30000);
  heapSetPsram("octal", 1000000, 500000);

  constexpr uint32_t CAPS_IMPOSSIBLE = MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL;
  checkSize(furble_sim_heap_free(CAPS_IMPOSSIBLE), 1000000,
            "the impossible SPIRAM plus INTERNAL set is over-reported as the PSRAM pool");
  checkSize(furble_sim_heap_minimum(CAPS_IMPOSSIBLE), 1000000,
            "the impossible set's watermark is over-reported from PSRAM too");
}

// Two shapes with no caller of their own, both reachable from the shim.
void testBaselineAndEmptyCaps(void) {
  using namespace Furble::Sim;

  heapSet(60000, 30000);
  heapSetPsram("octal", 1000000, 500000);

  // A baseline block is uncharged, so the flag only says where a later refund
  // lands. An explicit MALLOC_CAP_DEFAULT block stays internal, which is the
  // documented limit of heapBaselineBlock(): it knows only the PSRAM bit.
  size_t bytes = 0;
  bool external = false;
  heapBaselineBlock(1000, MALLOC_CAP_DEFAULT, &bytes, &external);
  check(!external, "a MALLOC_CAP_DEFAULT baseline block must stay internal");
  check(bytes > 1000, "a baseline block must include the TLSF block cost");
  heapBaselineBlock(1000, CAPS_EXTERNAL, &bytes, &external);
  check(external, "a MALLOC_CAP_SPIRAM baseline block must be reported external");

  // An empty caps set matches every heap, because heap_caps_match() compares
  // (all caps & 0) == 0 (components/heap/heap_caps.c lines 73 to 75).
  checkSize(furble_sim_heap_free(0), 1060000, "an empty caps set must sum both pools");
  checkSize(furble_sim_heap_largest(0), 500000, "an empty caps set must take the larger pool");
  checkSize(furble_sim_heap_minimum(0), 1060000, "an empty caps set must sum both pool watermarks");
}

}  // namespace

int main(void) {
  testQueryFiltering();
  testDefaultRouting();
  testDefaultRoutingWithoutPsram();
  testPreferOrder();
  testOverReportedCaps();
  testBaselineAndEmptyCaps();

  if (g_Failures != 0) {
    std::cerr << "sim heap caps: " << g_Failures << " FAILED\n";
    return 1;
  }
  std::cout << "sim heap caps: PASS\n";
  return 0;
}
