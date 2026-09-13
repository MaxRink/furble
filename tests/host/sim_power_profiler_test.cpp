#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "clock.h"
#include "power_profiler.h"

namespace Furble::Sim {

std::atomic<int> requestedExit {-1};

void requestExit(int result) {
  int unset = -1;
  requestedExit.compare_exchange_strong(unset, result);
}

void requestFailureExit(void) {
  int result = requestedExit.load();
  while (result == -1 || result == 0) {
    if (requestedExit.compare_exchange_weak(result, 1)) {
      return;
    }
  }
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

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

void writeFile(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream output(path);
  output << contents;
  if (!output) {
    throw std::runtime_error("could not write fixture " + path.string());
  }
}

std::string modelContents(void) {
  const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
                    / "tools/power-model/board-currents.yaml";
  return readFile(path);
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

std::string accountingModel(uint64_t poll_us,
                            uint64_t battery_timer_us,
                            uint64_t diagnostics_timer_us) {
  std::ostringstream model;
  model << modelContents() << "\naccounting:\n"
        << "  version: 1\n"
        << "  calibration_status: uncalibrated\n"
        << "  ui_poll_active_us_per_cycle:\n"
        << "    value_us: " << poll_us << "\n"
        << "    source: \"test-model\"\n"
        << "    confidence: estimated\n"
        << "  timer_active_us_per_fire:\n"
        << "    battery_timer:\n"
        << "      value_us: " << battery_timer_us << "\n"
        << "      source: \"test-model\"\n"
        << "      confidence: estimated\n"
        << "    diagnostics_timer:\n"
        << "      value_us: " << diagnostics_timer_us << "\n"
        << "      source: \"test-model\"\n"
        << "      confidence: estimated\n";
  return model.str();
}

bool replaceFirst(std::string &contents,
                  const std::string &needle,
                  const std::string &replacement,
                  size_t offset = 0) {
  const size_t found = contents.find(needle, offset);
  if (found == std::string::npos) {
    return false;
  }
  contents.replace(found, needle.size(), replacement);
  return true;
}

bool contains(const std::string &contents, const std::string &needle) {
  return contents.find(needle) != std::string::npos;
}

int failure(int line) {
  std::cerr << "sim power profiler check failed at line " << line << '\n';
  return EXIT_FAILURE;
}

bool containsJsonNumber(const std::string &contents, const std::string &field, uint64_t value) {
  const std::string prefix = "\"" + field + "\": " + std::to_string(value);
  return contains(contents, prefix + ",") || contains(contents, prefix + "\n");
}

std::string jsonStringField(const std::string &contents, const std::string &field) {
  const std::string prefix = "\"" + field + "\": \"";
  const size_t start = contents.find(prefix);
  if (start == std::string::npos) {
    return {};
  }
  const size_t value_start = start + prefix.size();
  const size_t value_end = contents.find('"', value_start);
  return value_end == std::string::npos ? std::string()
                                        : contents.substr(value_start, value_end - value_start);
}

bool nestedAfter(const std::string &contents,
                 const std::string &parent,
                 const std::string &child,
                 size_t &parent_position,
                 size_t &child_position) {
  parent_position = contents.find(parent);
  child_position = parent_position == std::string::npos
                       ? std::string::npos
                       : contents.find(child, parent_position + parent.size());
  return parent_position != std::string::npos && child_position != std::string::npos;
}

class ScopedEnvironment {
 public:
  ScopedEnvironment(const char *name, const std::filesystem::path &value) : name_(name) {
    const char *old = std::getenv(name_);
    if (old != nullptr) {
      old_value_ = old;
    }
    setenv(name_, value.c_str(), 1);
  }

  ~ScopedEnvironment() {
    if (old_value_.has_value()) {
      setenv(name_, old_value_->c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }

 private:
  const char *name_;
  std::optional<std::string> old_value_;
};

void resetExit(void) {
  Furble::Sim::requestedExit.store(-1);
}

bool reportExists(const std::filesystem::path &path) {
  return std::filesystem::exists(path);
}

bool expectRejected(const std::filesystem::path &model,
                    const std::filesystem::path &report,
                    const std::string &scenario,
                    bool prior_success = false) {
  resetExit();
  ScopedEnvironment selected_model("FURBLE_POWER_MODEL", model);
  if (prior_success) {
    Furble::Sim::requestExit(0);
  }
  Furble::Sim::profilerBegin(scenario.c_str(), true);
  Furble::Sim::profilerWriteReport(report.c_str(), scenario.c_str());
  return Furble::Sim::requestedExit.load() == 1 && !reportExists(report);
}

}  // namespace

int main() {
  using namespace Furble::Sim;
  const TemporaryReportDirectory reportDirectory;

  // Preserve the wrap-safe and legacy unaccounted regressions. These windows
  // use the full current table without the optional accounting section.
  const auto base_model_path = reportDirectory.path() / "base-model.yaml";
  writeFile(base_model_path, modelContents());
  {
    ScopedEnvironment base_model("FURBLE_POWER_MODEL", base_model_path);
    resetExit();
    setClockMillis(std::numeric_limits<uint32_t>::max() - 500);
    profilerBegin("clock-wrap", false);
    profilerPowerLockAcquire(0, "cpu_freq_max", "wrap-test");
    advanceClock(1500);
    const auto wrap_report = reportDirectory.path() / "clock-wrap.json";
    profilerWriteReport(wrap_report.c_str(), "clock-wrap");
    const std::string wrap_json = readFile(wrap_report);
    if (requestedExit.load() != -1 || !contains(wrap_json, "\"duration_ms\": 1500")
        || !contains(wrap_json, "\"on\": 2000")
        || !contains(wrap_json, "\"total_hold_ms\": 2000")) {
      return failure(__LINE__);
    }

    profilerResetWindow();
    advanceClock(500);
    const auto wrap_reset_report = reportDirectory.path() / "clock-wrap-reset.json";
    profilerWriteReport(wrap_reset_report.c_str(), "clock-wrap-reset");
    if (requestedExit.load() != -1
        || !contains(readFile(wrap_reset_report), "\"duration_ms\": 500")) {
      return failure(__LINE__);
    }
    profilerPowerLockRelease(0, "cpu_freq_max", "wrap-test");

    profilerResetWindow();
    advanceClock(1);
    const auto short_report = reportDirectory.path() / "short-duration-energy.json";
    profilerWriteReport(short_report.c_str(), "short-duration-energy");
    const std::string short_json = readFile(short_report);
    if (requestedExit.load() != -1 || !contains(short_json, "\"duration_ms\": 1")
        || !contains(short_json, "\"estimated_mA\": 41.295970")
        || !contains(short_json, "\"accounting_inputs\": {\n      \"duration_ms\": 1")
        || !contains(short_json, "\"light_sleep_in_80\": 1")
        || !contains(short_json, "\"model_valid\": true")
        || !contains(short_json, "\"model_digest\": \"sha256:")) {
      return failure(__LINE__);
    }
  }

  // A normal model coefficient change is also an explicit reload boundary.
  // Mutating the file after begin(true) cannot alter the frozen report.
  const auto coefficient_model_path = reportDirectory.path() / "coefficient-model.yaml";
  writeFile(coefficient_model_path, modelContents());
  {
    ScopedEnvironment coefficient_model("FURBLE_POWER_MODEL", coefficient_model_path);
    resetExit();
    setClockMillis(0);
    profilerBegin("base-model", true);
    profilerPowerConfig(80, 40, true);
    profilerPowerLockAcquire(0, "cpu_freq_max", "model-test");
    profilerResetWindow();
    advanceClock(10);
    const auto base_report = reportDirectory.path() / "base-model-report.json";
    profilerWriteReport(base_report.c_str(), "base-model");
    if (requestedExit.load() != -1
        || !contains(readFile(base_report), "\"estimated_mA\": 81.255970")) {
      return failure(__LINE__);
    }

    std::string changed_coefficient_model = modelContents();
    if (!replaceS3ModelValue(changed_coefficient_model, "value_ma: 80.4")) {
      return failure(__LINE__);
    }
    writeFile(coefficient_model_path, changed_coefficient_model);
    resetExit();
    setClockMillis(10);
    profilerBegin("changed-model", true);
    profilerPowerConfig(80, 40, true);
    profilerPowerLockAcquire(0, "cpu_freq_max", "model-test");
    advanceClock(10);
    const auto changed_report = reportDirectory.path() / "changed-model-report.json";
    profilerWriteReport(changed_report.c_str(), "changed-model");
    if (requestedExit.load() != -1
        || !contains(readFile(changed_report), "\"estimated_mA\": 121.455970")) {
      return failure(__LINE__);
    }
  }

  // Reporting must opt in before the model is loaded. This fixture is the full
  // board model plus the exact accounting schema consumed by the profiler.
  const auto frozen_model_path = reportDirectory.path() / "frozen-model.yaml";
  writeFile(frozen_model_path, accountingModel(700, 1100, 300));
  ScopedEnvironment selected_model("FURBLE_POWER_MODEL", frozen_model_path);

  resetExit();
  setClockMicros(0);
  profilerBegin("microsecond-accounting", true);
  profilerPowerConfig(240, 40, true);
  profilerPowerLockAcquire(0, "cpu_freq_max", "test");
  profilerBeginUiCycle();
  profilerTimerFire("battery_timer");
  profilerEndUiCycle();

  // At the first 1 ms clock boundary, only part of the 1,800 us queued work is
  // consumed. The remaining 800 us carries into the next 1 ms slice, all while
  // the 240 MHz lock is held.
  advanceClockMicros(1500);
  profilerSetDisplayState("dim");
  advanceClockMicros(500);
  profilerPowerLockRelease(0, "cpu_freq_max", "test");

  // The model is frozen for this reporting window. Mutating the selected file
  // before the report changes neither the queued cost nor the provenance used
  // by this window.
  writeFile(frozen_model_path, accountingModel(900, 1100, 300));
  const auto first_report = reportDirectory.path() / "microsecond-report.json";
  profilerWriteReport(first_report.c_str(), "microsecond-accounting");
  const std::string first_json = readFile(first_report);
  if (requestedExit.load() != -1 || !reportExists(first_report)
      || !containsJsonNumber(first_json, "duration_ms", 2)
      || !containsJsonNumber(first_json, "poll_work_us", 700)
      || !containsJsonNumber(first_json, "timer_work_us", 1100)
      || !containsJsonNumber(first_json, "battery_timer", 1100)
      || !containsJsonNumber(first_json, "work_240_us", 1800)
      || !containsJsonNumber(first_json, "eligible_light_sleep_us", 0)
      || !containsJsonNumber(first_json, "adjusted_light_sleep_us", 0)
      || !containsJsonNumber(first_json, "pending_work_us", 0)
      || !containsJsonNumber(first_json, "current_count", 0)
      || !containsJsonNumber(first_json, "total_hold_ms", 0)) {
    return failure(__LINE__);
  }

  const std::string frozen_fingerprint = jsonStringField(first_json, "accounting_fingerprint");
  if (!contains(first_json, "\"accounting_version\": 1")
      || !contains(first_json, "\"calibration_status\": \"uncalibrated\"")
      || !contains(first_json, "\"accounting_mode\": \"synthetic-virtual-work\"")
      || frozen_fingerprint.empty()) {
    return failure(__LINE__);
  }

  // A balanced window can be reset without carrying old work or lock state
  // into the next report.
  profilerResetWindow();
  advanceClock(1);
  const auto reset_report = reportDirectory.path() / "reset-report.json";
  profilerWriteReport(reset_report.c_str(), "reset-accounting");
  const std::string reset_json = readFile(reset_report);
  if (requestedExit.load() != -1 || !contains(reset_json, "\"duration_ms\": 1")
      || !contains(reset_json, "\"poll_work_us\": 0")
      || !contains(reset_json, "\"timer_work_us\": 0")
      || !contains(reset_json, "\"pending_work_us\": 0")) {
    return failure(__LINE__);
  }

  // A new explicit reporting begin is the reload boundary. The changed poll
  // cost is observed only after that boundary, and 2,000 us lands in the 160
  // MHz bucket rather than the previous 240 MHz bucket.
  resetExit();
  setClockMicros(3000 * 1000);
  profilerBegin("reload-accounting", true);
  profilerPowerConfig(160, 40, true);
  profilerPowerLockAcquire(0, "cpu_freq_max", "reload");
  profilerBeginUiCycle();
  profilerTimerFire("diagnostics_timer");
  profilerEndUiCycle();
  advanceClockMicros(2000);
  profilerPowerLockRelease(0, "cpu_freq_max", "reload");
  const auto reload_report = reportDirectory.path() / "reload-report.json";
  profilerWriteReport(reload_report.c_str(), "reload-accounting");
  const std::string reload_json = readFile(reload_report);
  const std::string reload_fingerprint = jsonStringField(reload_json, "accounting_fingerprint");
  if (requestedExit.load() != -1 || !contains(reload_json, "\"poll_work_us\": 900")
      || !contains(reload_json, "\"timer_work_us\": 300")
      || !contains(reload_json, "\"work_160_us\": 1200")
      || !contains(reload_json, "\"work_240_us\": 0") || reload_fingerprint.empty()
      || reload_fingerprint == frozen_fingerprint) {
    return failure(__LINE__);
  }

  // Unlocked work debits the eligible sleep interval instead of charging the
  // whole slice as active CPU time. A later lock must not retrocharge that
  // earlier unlocked slice.
  resetExit();
  setClockMillis(5000);
  profilerBegin("unlocked-accounting", true);
  profilerPowerConfig(160, 40, true);
  profilerBeginUiCycle();
  profilerEndUiCycle();
  advanceClock(2);
  const auto unlocked_report = reportDirectory.path() / "unlocked-report.json";
  profilerWriteReport(unlocked_report.c_str(), "unlocked-accounting");
  const std::string unlocked_json = readFile(unlocked_report);
  if (requestedExit.load() != -1 || !contains(unlocked_json, "\"work_80_us\": 900")
      || !contains(unlocked_json, "\"eligible_light_sleep_us\": 2000")
      || !contains(unlocked_json, "\"light_sleep_work_us\": 900")
      || !contains(unlocked_json, "\"adjusted_light_sleep_us\": 1100")
      || !contains(unlocked_json, "\"estimated_mA\": 59.277970")) {
    return failure(__LINE__);
  }

  resetExit();
  setClockMillis(7000);
  profilerBegin("no-retrocharge", true);
  profilerPowerConfig(160, 40, true);
  advanceClock(1);
  profilerPowerLockAcquire(0, "cpu_freq_max", "retrocharge");
  profilerBeginUiCycle();
  profilerTimerFire("diagnostics_timer");
  profilerEndUiCycle();
  advanceClock(2);
  profilerPowerLockRelease(0, "cpu_freq_max", "retrocharge");
  const auto no_retrocharge_report = reportDirectory.path() / "no-retrocharge-report.json";
  profilerWriteReport(no_retrocharge_report.c_str(), "no-retrocharge");
  const std::string no_retrocharge_json = readFile(no_retrocharge_report);
  if (requestedExit.load() != -1 || !containsJsonNumber(no_retrocharge_json, "work_80_us", 0)
      || !containsJsonNumber(no_retrocharge_json, "work_160_us", 1200)
      || !containsJsonNumber(no_retrocharge_json, "frequency_80", 1)
      || !containsJsonNumber(no_retrocharge_json, "frequency_160", 2)
      || !containsJsonNumber(no_retrocharge_json, "eligible_light_sleep_us", 1000)) {
    return failure(__LINE__);
  }

  // Zero is a valid measured cost. It must not be confused with a missing or
  // malformed value in the same full-model fixture.
  const auto zero_model_path = reportDirectory.path() / "zero-model.yaml";
  writeFile(zero_model_path, accountingModel(0, 0, 0));
  const auto zero_report = reportDirectory.path() / "zero-report.json";
  resetExit();
  {
    ScopedEnvironment zero_model("FURBLE_POWER_MODEL", zero_model_path);
    profilerBegin("zero-accounting", true);
    profilerWriteReport(zero_report.c_str(), "zero-accounting");
  }
  if (requestedExit.load() != -1 || !reportExists(zero_report)
      || !contains(readFile(zero_report), "\"accounting_valid\": true")) {
    return failure(__LINE__);
  }

  // The selected model is authoritative and accounting input errors fail
  // closed, including a missing value, an invalid value, duplicate timer key,
  // and missing provenance.
  std::string missing_cost = accountingModel(700, 1100, 300);
  if (!replaceFirst(missing_cost, "      value_us: 1100\n", "")) {
    return failure(__LINE__);
  }
  const auto missing_cost_path = reportDirectory.path() / "missing-cost.yaml";
  writeFile(missing_cost_path, missing_cost);
  if (!expectRejected(missing_cost_path, reportDirectory.path() / "missing-cost.json",
                      "missing-cost")) {
    return failure(__LINE__);
  }

  std::string invalid_cost = accountingModel(700, 1100, 300);
  if (!replaceFirst(invalid_cost, "      value_us: 1100\n", "      value_us: -1\n")) {
    return failure(__LINE__);
  }
  const auto invalid_cost_path = reportDirectory.path() / "invalid-cost.yaml";
  writeFile(invalid_cost_path, invalid_cost);
  if (!expectRejected(invalid_cost_path, reportDirectory.path() / "invalid-cost.json",
                      "invalid-cost")) {
    return failure(__LINE__);
  }

  std::string duplicate_timer = accountingModel(700, 1100, 300);
  const std::string duplicate_entry =
      "    battery_timer:\n"
      "      value_us: 1100\n"
      "      source: \"test-model\"\n"
      "      confidence: estimated\n";
  if (!replaceFirst(duplicate_timer, "  timer_active_us_per_fire:\n",
                    "  timer_active_us_per_fire:\n" + duplicate_entry)) {
    return failure(__LINE__);
  }
  const auto duplicate_timer_path = reportDirectory.path() / "duplicate-timer.yaml";
  writeFile(duplicate_timer_path, duplicate_timer);
  if (!expectRejected(duplicate_timer_path, reportDirectory.path() / "duplicate-timer.json",
                      "duplicate-timer")) {
    return failure(__LINE__);
  }

  std::string missing_provenance = accountingModel(700, 1100, 300);
  const size_t poll_section = missing_provenance.find("  ui_poll_active_us_per_cycle:\n");
  if (poll_section == std::string::npos
      || !replaceFirst(missing_provenance, "    source: \"test-model\"\n", "", poll_section)) {
    return failure(__LINE__);
  }
  const auto missing_provenance_path = reportDirectory.path() / "missing-provenance.yaml";
  writeFile(missing_provenance_path, missing_provenance);
  if (!expectRejected(missing_provenance_path, reportDirectory.path() / "missing-provenance.json",
                      "missing-provenance")) {
    return failure(__LINE__);
  }

  std::string invalid_confidence = accountingModel(700, 1100, 300);
  const size_t confidence_section = invalid_confidence.find("  ui_poll_active_us_per_cycle:\n");
  if (confidence_section == std::string::npos
      || !replaceFirst(invalid_confidence, "    confidence: estimated\n",
                       "    confidence: synthetic\n", confidence_section)) {
    return failure(__LINE__);
  }
  const auto invalid_confidence_path = reportDirectory.path() / "invalid-confidence.yaml";
  writeFile(invalid_confidence_path, invalid_confidence);
  if (!expectRejected(invalid_confidence_path, reportDirectory.path() / "invalid-confidence.json",
                      "invalid-confidence")) {
    return failure(__LINE__);
  }

  // Keep the original fail-closed checks for the required board coefficients.
  std::string malformed_current = modelContents();
  if (!replaceS3ModelValue(malformed_current, "value_ma: not-a-number")) {
    return failure(__LINE__);
  }
  const auto malformed_current_path = reportDirectory.path() / "malformed-current.yaml";
  writeFile(malformed_current_path, malformed_current);
  if (!expectRejected(malformed_current_path, reportDirectory.path() / "malformed-current.json",
                      "malformed-current")) {
    return failure(__LINE__);
  }

  std::string negative_current = modelContents();
  if (!replaceS3ModelValue(negative_current, "value_ma: -1.0")) {
    return failure(__LINE__);
  }
  const auto negative_current_path = reportDirectory.path() / "negative-current.yaml";
  writeFile(negative_current_path, negative_current);
  if (!expectRejected(negative_current_path, reportDirectory.path() / "negative-current.json",
                      "negative-current")) {
    return failure(__LINE__);
  }

  std::string duplicate_current = modelContents();
  const size_t s3_anchor = duplicate_current.find("esp32s3_mcu: &esp32s3_mcu");
  const size_t first_entry = duplicate_current.find("    active_cpu_80mhz:", s3_anchor);
  if (s3_anchor == std::string::npos || first_entry == std::string::npos) {
    return failure(__LINE__);
  }
  duplicate_current.insert(first_entry, "    active_cpu_80mhz:\n      value_ma: 40.2\n");
  const auto duplicate_current_path = reportDirectory.path() / "duplicate-current.yaml";
  writeFile(duplicate_current_path, duplicate_current);
  if (!expectRejected(duplicate_current_path, reportDirectory.path() / "duplicate-current.json",
                      "duplicate-current")) {
    return failure(__LINE__);
  }

  const auto missing_model_path = reportDirectory.path() / "missing-model.yaml";
  if (!expectRejected(missing_model_path, reportDirectory.path() / "missing-model.json",
                      "missing-model")) {
    return failure(__LINE__);
  }

  // Resetting with unresolved work is the same fail-closed boundary as a
  // final report. It must not silently discard the queued microseconds.
  const auto reset_pending_model_path = reportDirectory.path() / "reset-pending-model.yaml";
  writeFile(reset_pending_model_path, accountingModel(1, 0, 0));
  resetExit();
  {
    ScopedEnvironment reset_pending_selected("FURBLE_POWER_MODEL", reset_pending_model_path);
    profilerBegin("reset-pending", true);
    profilerBeginUiCycle();
    profilerResetWindow();
  }
  if (requestedExit.load() != 1) {
    return failure(__LINE__);
  }

  // A pending cost at final report is an error. requestFailureExit must also
  // upgrade a prior successful exit request, rather than preserving zero.
  const auto pending_model_path = reportDirectory.path() / "pending-model.yaml";
  writeFile(pending_model_path, accountingModel(1, 0, 0));
  const auto pending_report = reportDirectory.path() / "pending-report.json";
  resetExit();
  {
    ScopedEnvironment pending_selected("FURBLE_POWER_MODEL", pending_model_path);
    requestExit(0);
    profilerBegin("pending-accounting", true);
    profilerBeginUiCycle();
    profilerWriteReport(pending_report.c_str(), "pending-accounting");
  }
  if (requestedExit.load() != 1 || reportExists(pending_report)) {
    return failure(__LINE__);
  }

  // Ordinary profiler windows do not depend on an external accounting model.
  // A late accounting model selection after begin(false) is rejected, not
  // mislabeled as a valid unaccounted report.
  const auto ordinary_model = reportDirectory.path() / "ordinary-model.yaml";
  writeFile(ordinary_model, modelContents());
  const auto ordinary_report = reportDirectory.path() / "ordinary-report.json";
  resetExit();
  {
    ScopedEnvironment ordinary_selected("FURBLE_POWER_MODEL", ordinary_model);
    profilerBegin("ordinary-window", false);
    advanceClock(2);
    profilerWriteReport(ordinary_report.c_str(), "ordinary-window");
  }
  if (requestedExit.load() != -1 || !reportExists(ordinary_report)
      || !contains(readFile(ordinary_report), "\"accounting_mode\": \"legacy-unaccounted\"")) {
    return failure(__LINE__);
  }

  const auto late_report = reportDirectory.path() / "late-accounting-report.json";
  resetExit();
  {
    ScopedEnvironment late_selected("FURBLE_POWER_MODEL", frozen_model_path);
    requestExit(0);
    profilerBegin("late-accounting", false);
    profilerWriteReport(late_report.c_str(), "late-accounting");
  }
  if (requestedExit.load() != 1 || reportExists(late_report)) {
    return failure(__LINE__);
  }

  // Keep the production JSON identity nested under energy. This catches a
  // flattened accounting_inputs object that happens to retain the same keys.
  resetExit();
  {
    ScopedEnvironment shape_selected("FURBLE_POWER_MODEL", frozen_model_path);
    profilerBegin("json-shape", true);
    advanceClock(2);
    const auto shape_report = reportDirectory.path() / "json-shape.json";
    profilerWriteReport(shape_report.c_str(), "json-shape");
    const std::string shape_json = readFile(shape_report);
    size_t energy = 0;
    size_t accounting_inputs = 0;
    size_t top_estimate = 0;
    if (!nestedAfter(shape_json, "\n  \"energy\": {", "\n    \"accounting_inputs\": {", energy,
                     accounting_inputs)
        || !contains(shape_json, "\n      \"accounting_mode\": \"synthetic-virtual-work\",")
        || shape_json.find("\n  \"estimated_mA\": ", accounting_inputs) == std::string::npos) {
      return failure(__LINE__);
    }
    top_estimate = shape_json.find("\n  \"estimated_mA\": ", accounting_inputs);
    if (!(energy < accounting_inputs && accounting_inputs < top_estimate)) {
      return failure(__LINE__);
    }
  }

  std::cout << "sim power profiler behavioral checks passed\n";
  return 0;
}
