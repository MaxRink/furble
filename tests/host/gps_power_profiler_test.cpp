// Regression test for the simulator's GPS degraded-state power accounting.
//
// The firmware releases NO_LIGHT_SLEEP while it waits for a bounded retry.
// Keep that state visible in simulator reports and ensure a later probe can
// hold the lock only for its bounded acquisition window.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "clock.h"
#include "power_profiler.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

bool hasField(const std::string &json, const std::string &field, uint64_t value) {
  const std::string needle = "\"" + field + "\": " + std::to_string(value);
  return json.find(needle) != std::string::npos;
}

bool lockHasField(const std::string &json,
                  const std::string &lockName,
                  const std::string &field,
                  uint64_t value) {
  const size_t lock = json.find("\"" + lockName + "\": {");
  if (lock == std::string::npos) {
    return false;
  }
  const size_t end = json.find("\n      },", lock);
  const std::string needle = "\"" + field + "\": " + std::to_string(value);
  return json.find(needle, lock) != std::string::npos
         && (end == std::string::npos || json.find(needle, lock) < end);
}

}  // namespace

int main() {
  using namespace Furble::Sim;

  profilerBegin("gps-degraded-power");
  profilerSetGpsState("degraded");
  advanceClock(10000);

  // The scheduled retry probe re-acquires the lock. Its bounded hold must
  // suppress light sleep only during the probe, not during the backoff.
  profilerSetGpsState("acquiring");
  profilerPowerLockAcquire(2, "no_light_sleep", "gps");
  advanceClock(10000);
  profilerPowerLockRelease(2, "no_light_sleep", "gps");
  profilerSetGpsState("off");

  const std::filesystem::path report =
      std::filesystem::temp_directory_path() / "furble-gps-degraded-power-test.json";
  profilerWriteReport(report.string().c_str(), "gps-degraded-power");
  const std::string json = readFile(report);
  std::error_code ignored;
  std::filesystem::remove(report, ignored);

  check(json.find("\"degraded\":") != std::string::npos,
        "power reports include degraded GPS residency");
  check(hasField(json, "degraded", 10000), "the degraded backoff is accounted before the probe");
  check(hasField(json, "residency_ms", 10000),
        "light sleep is available while the degraded lock is released");
  check(lockHasField(json, "no_light_sleep", "current_count", 0),
        "the bounded probe releases its power lock");

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "GPS degraded power profiler checks passed\n";
  return 0;
}
