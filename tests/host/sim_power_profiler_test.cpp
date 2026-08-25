#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "clock.h"
#include "power_profiler.h"

namespace {

bool reportContains(const std::filesystem::path &path, const std::string &text) {
  std::ifstream report(path);
  return std::string((std::istreambuf_iterator<char>(report)), std::istreambuf_iterator<char>())
             .find(text)
         != std::string::npos;
}

}  // namespace

int main() {
  using namespace Furble::Sim;
  const auto path = std::filesystem::temp_directory_path() / "furble-sim-wrap-report.json";

  setClockMillis(std::numeric_limits<uint32_t>::max() - 500);
  profilerBegin("clock-wrap");
  profilerPowerLockAcquire(0, "cpu", "wrap-test");
  advanceClock(1500);
  profilerWriteReport(path.c_str(), "clock-wrap");

  // The report window crosses the uint32 boundary. Raw uint64 comparisons used
  // to report a zero window and underflow active durations.
  if (!reportContains(path, "\"duration_ms\": 1500") || !reportContains(path, "\"on\": 2000")
      || !reportContains(path, "\"total_hold_ms\": 2000")) {
    std::filesystem::remove(path);
    return 1;
  }

  profilerResetWindow();
  advanceClock(500);
  profilerWriteReport(path.c_str(), "clock-wrap-reset");
  const bool resetWindowIsMeasured = reportContains(path, "\"duration_ms\": 500");
  std::filesystem::remove(path);
  return resetWindowIsMeasured ? 0 : 1;
}
