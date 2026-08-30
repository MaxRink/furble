#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include "clock.h"
#include "power_profiler.h"

namespace Furble::Sim {

std::atomic<int> requestedExit {-1};

void requestExit(int result) {
  requestedExit.store(result);
}

}  // namespace Furble::Sim

namespace {

class TemporaryReportDirectory {
 public:
  TemporaryReportDirectory() {
    const auto root = std::filesystem::temp_directory_path();
    static std::atomic<uint64_t> sequence {0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
      const auto suffix = std::to_string(timestamp) + "-" + std::to_string(sequence.fetch_add(1));
      const auto candidate = root / ("furble-sim-wrap-" + suffix);
      std::error_code error;
      if (std::filesystem::create_directory(candidate, error)) {
        directory_ = candidate;
        return;
      }
      if (error) {
        throw std::filesystem::filesystem_error("create temporary report directory", candidate,
                                                error);
      }
    }

    throw std::runtime_error("could not create a unique temporary report directory");
  }

  ~TemporaryReportDirectory() {
    std::error_code error;
    std::filesystem::remove_all(directory_, error);
  }

  const std::filesystem::path &path() const { return directory_; }

 private:
  std::filesystem::path directory_;
};

bool reportContains(const std::filesystem::path &path, const std::string &text) {
  std::ifstream report(path);
  return std::string((std::istreambuf_iterator<char>(report)), std::istreambuf_iterator<char>())
             .find(text)
         != std::string::npos;
}

}  // namespace

int main() {
  using namespace Furble::Sim;
  const TemporaryReportDirectory reportDirectory;
  const auto path = reportDirectory.path() / "report.json";

  setClockMillis(std::numeric_limits<uint32_t>::max() - 500);
  profilerBegin("clock-wrap");
  profilerPowerLockAcquire(0, "cpu", "wrap-test");
  advanceClock(1500);
  profilerWriteReport(path.c_str(), "clock-wrap");

  // The report window crosses the uint32 boundary. Raw uint64 comparisons used
  // to report a zero window and underflow active durations.
  if (!reportContains(path, "\"duration_ms\": 1500") || !reportContains(path, "\"on\": 2000")
      || !reportContains(path, "\"total_hold_ms\": 2000")) {
    return 1;
  }

  profilerResetWindow();
  advanceClock(500);
  profilerWriteReport(path.c_str(), "clock-wrap-reset");
  const bool resetWindowIsMeasured = reportContains(path, "\"duration_ms\": 500");
  return resetWindowIsMeasured && requestedExit.load() == -1 ? 0 : 1;
}
