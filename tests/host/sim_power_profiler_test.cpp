#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
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

std::string modelContents(void) {
  const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
                    / "tools/power-model/board-currents.yaml";
  std::ifstream input(path);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

bool replaceS3ModelValue(std::string &contents,
                         const std::string &replacement,
                         const std::string &needle = "value_ma: 40.2") {
  const size_t anchor = contents.find("esp32s3_mcu: &esp32s3_mcu");
  const size_t value = contents.find(needle, anchor);
  if (anchor == std::string::npos || value == std::string::npos) {
    return false;
  }
  contents.replace(value, needle.size(), replacement);
  return true;
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
  if (!resetWindowIsMeasured || requestedExit.load() != -1) {
    return 1;
  }
  profilerPowerLockRelease(0, "cpu", "wrap-test");

  // A sub-second window must retain its raw duration for energy arithmetic;
  // only the presentation fields stay quantized for stable reports.
  profilerResetWindow();
  advanceClock(1);
  profilerWriteReport(path.c_str(), "short-duration-energy");
  if (!reportContains(path, "\"duration_ms\": 1")
      || !reportContains(path, "\"estimated_mA\": 41.295970")
      || !reportContains(path, "\"accounting_inputs\": {\n      \"duration_ms\": 1")
      || !reportContains(path, "\"light_sleep_in_80\": 1")
      || !reportContains(path, "\"model_valid\": true")
      || !reportContains(path, "\"model_digest\": \"sha256:")) {
    return 1;
  }

  // A consumed current change must affect the duration-weighted estimate.
  profilerPowerConfig(80, 40, true);
  profilerPowerLockAcquire(0, "cpu", "model-test");
  profilerResetWindow();
  advanceClock(10);
  const auto base_report = reportDirectory.path() / "base-model-report.json";
  profilerWriteReport(base_report.c_str(), "base-model");
  if (!reportContains(base_report, "\"estimated_mA\": 81.255970")) {
    return 1;
  }

  const std::string complete_model = modelContents();
  if (complete_model.empty()) {
    return 1;
  }
  std::string changed_model = complete_model;
  if (!replaceS3ModelValue(changed_model, "value_ma: 80.4")) {
    return 1;
  }
  const auto changed_model_path = reportDirectory.path() / "changed-model.yaml";
  std::ofstream(changed_model_path) << changed_model;
  const auto changed_report = reportDirectory.path() / "changed-model-report.json";
  setenv("FURBLE_POWER_MODEL", changed_model_path.c_str(), 1);
  profilerWriteReport(changed_report.c_str(), "changed-model");
  unsetenv("FURBLE_POWER_MODEL");
  profilerPowerLockRelease(0, "cpu", "model-test");
  if (!reportContains(changed_report, "\"estimated_mA\": 121.455970")) {
    return 1;
  }

  auto expectRejected = [&](const std::filesystem::path &model, const std::filesystem::path &report,
                            const std::string &scenario) {
    setenv("FURBLE_POWER_MODEL", model.c_str(), 1);
    profilerWriteReport(report.c_str(), scenario.c_str());
    unsetenv("FURBLE_POWER_MODEL");
    return !std::filesystem::exists(report);
  };

  // Malformed, negative, and duplicate consumed values must be rejected even
  // when every other required model entry is present.
  std::string malformed_model = complete_model;
  if (!replaceS3ModelValue(malformed_model, "value_ma: not-a-number")) {
    return 1;
  }
  const auto malformed_model_path = reportDirectory.path() / "malformed-model.yaml";
  std::ofstream(malformed_model_path) << malformed_model;
  if (!expectRejected(malformed_model_path, reportDirectory.path() / "malformed-report.json",
                      "malformed-model")) {
    return 1;
  }

  std::string negative_model = complete_model;
  if (!replaceS3ModelValue(negative_model, "value_ma: -1.0")) {
    return 1;
  }
  const auto negative_model_path = reportDirectory.path() / "negative-model.yaml";
  std::ofstream(negative_model_path) << negative_model;
  if (!expectRejected(negative_model_path, reportDirectory.path() / "negative-report.json",
                      "negative-model")) {
    return 1;
  }

  std::string duplicate_model = complete_model;
  const size_t s3_anchor = duplicate_model.find("esp32s3_mcu: &esp32s3_mcu");
  const size_t first_entry = duplicate_model.find("    active_cpu_80mhz:", s3_anchor);
  if (s3_anchor == std::string::npos || first_entry == std::string::npos) {
    return 1;
  }
  duplicate_model.insert(first_entry, "    active_cpu_80mhz:\n      value_ma: 40.2\n");
  const auto duplicate_model_path = reportDirectory.path() / "duplicate-model.yaml";
  std::ofstream(duplicate_model_path) << duplicate_model;
  if (!expectRejected(duplicate_model_path, reportDirectory.path() / "duplicate-report.json",
                      "duplicate-model")) {
    return 1;
  }

  const auto missing_model = reportDirectory.path() / "missing-model.yaml";
  const auto missing_report = reportDirectory.path() / "missing-report.json";
  setenv("FURBLE_POWER_MODEL", missing_model.c_str(), 1);
  profilerWriteReport(missing_report.c_str(), "missing-model");
  unsetenv("FURBLE_POWER_MODEL");
  return requestedExit.load() == 1 && !std::filesystem::exists(missing_report) ? 0 : 1;
}
