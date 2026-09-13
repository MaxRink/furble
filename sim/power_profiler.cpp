#include "power_profiler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>
#include <cerrno>

#include "clock.h"
#include "driver.h"

namespace Furble::Sim {
namespace {

constexpr int CPU_FREQ_LOCK = 0;
constexpr int APB_FREQ_LOCK = 1;
constexpr int NO_LIGHT_SLEEP_LOCK = 2;
// Report fields retain one-second presentation rounding for stable snapshots;
// energy integration below always uses the raw virtual-clock durations.
constexpr uint64_t REPORT_TIME_QUANTUM_MS = 1000;

const char *const TIMER_NAMES[] = {
    "inactivity_timer",
    "battery_timer",
    "diagnostics_timer",
    "icon_timer",
    "connect_timer",
    "intervalometer_timer",
    "gps_service_timer",
    "gps_data_timer",
    "nmea_timer",
    "interval_page_refresh",
    "bulb_timer",
    "bulb_page_refresh",
    "companion_pairing_timer",
    "gesture_timer",
};

struct OwnerData {
  uint64_t acquire_count = 0;
  uint64_t release_count = 0;
  uint64_t total_hold_ms = 0;
  std::map<std::string, uint64_t> histogram;
  std::deque<uint32_t> active_starts;
};

struct LockData {
  std::string name;
  uint32_t count = 0;
  uint64_t acquire_count = 0;
  uint64_t release_count = 0;
  uint64_t unbalanced_release_count = 0;
  uint64_t total_hold_ms = 0;
  uint32_t active_start_ms = 0;
  std::map<std::string, uint64_t> histogram;
  std::map<std::string, OwnerData> owners;
};

struct CurrentModel {
  double mcu_80 = 40.2;
  double mcu_160 = 56.9;
  double mcu_240 = 73.8;
  double light_sleep = 0.240;
  double radio_tx = 176.0;
  double connected_idle = 3.3;
  double display_panel_on = 6.0;
  double display_panel_sleep = 0.015;
  double display_backlight = 35.0;
  double gps_acquisition = 25.0;
  double gps_tracking = 23.0;
  double gps_standby = 0.5;
  double pmic = 0.05247;
  double peripheral = 0.0035;
  bool accounting_enabled = false;
  int accounting_version = 0;
  std::string calibration_status;
  uint64_t ui_poll_active_us = 0;
  struct AccountingCost {
    uint64_t value_us = 0;
    std::string source;
    std::string confidence;
  };
  std::map<std::string, AccountingCost> timer_active_us;
  std::string poll_source;
  std::string poll_confidence;
  std::string accounting_fingerprint;
};

struct ModelLoadResult {
  CurrentModel model;
  std::filesystem::path source;
  std::string digest;
  bool valid = false;
};

struct ProfilerState {
  std::mutex mutex;
  bool started = false;
  std::string scenario;
  uint32_t window_start_ms = 0;
  uint32_t last_time_ms = 0;

  std::map<std::string, uint64_t> timer_fires;
  uint64_t invalidated_area_pixels = 0;
  uint64_t flushed_pixels = 0;
  // Redraw-storm probe. Reset and read on its own span, not tied to the report
  // window, so a scenario can measure invalidations over a steady page.
  uint32_t invalidation_probe_count = 0;

  uint64_t ui_cycles = 0;
  bool cycle_timer_fired = false;
  bool cycle_task_woke = false;
  bool timer_queue_idle = true;
  bool task_idle = true;
  bool reporting_enabled = false;
  bool model_loaded = false;
  bool accounting_invalid = false;
  CurrentModel model;
  std::string model_source;
  std::string model_digest;
  std::string accounting_fingerprint;
  uint64_t pending_work_us = 0;
  uint64_t work_80_us = 0;
  uint64_t work_160_us = 0;
  uint64_t work_240_us = 0;
  uint64_t light_sleep_work_us = 0;
  uint64_t eligible_light_sleep_us = 0;
  uint64_t poll_work_us = 0;
  uint64_t timer_work_us = 0;
  std::map<std::string, uint64_t> timer_work_us_by_name;
  uint64_t timer_idle_ms = 0;
  uint64_t task_idle_ms = 0;
  uint64_t lock_free_ms = 0;
  uint64_t light_sleep_ms = 0;
  std::map<std::string, uint64_t> queue_receives;
  std::map<std::string, uint64_t> queue_empty_receives;
  std::map<std::string, uint64_t> task_delay_count;
  std::map<std::string, uint64_t> task_delay_ms;

  std::map<int, LockData> locks;
  int configured_max_frequency_mhz = 160;
  int configured_min_frequency_mhz = 40;
  bool light_sleep_enabled = true;
  std::map<int, uint64_t> frequency_ms;

  std::string display_state = "on";
  std::map<std::string, uint64_t> display_ms {
      {"dim", 0},
      {"off", 0},
      {"on",  0}
  };
  bool radio_connected = false;
  uint64_t radio_connected_ms = 0;
  std::map<std::string, uint64_t> radio_events;
  std::string gps_state = "off";
  std::map<std::string, uint64_t> gps_ms {
      {"acquiring", 0},
      {"degraded",  0},
      {"off",       0},
      {"standby",   0},
      {"tracking",  0}
  };
};

ProfilerState state;

std::string trim(std::string value) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string jsonEscape(const std::string &value) {
  std::string result;
  result.reserve(value.size() + 2);
  for (const char character : value) {
    switch (character) {
      case '\\':
        result += "\\\\";
        break;
      case '"':
        result += "\\\"";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result += character;
        break;
    }
  }
  return result;
}

void addHistogram(std::map<std::string, uint64_t> &histogram, uint64_t milliseconds) {
  const char *bucket = nullptr;
  if (milliseconds < 1) {
    bucket = "0-1";
  } else if (milliseconds < 5) {
    bucket = "1-5";
  } else if (milliseconds < 10) {
    bucket = "5-10";
  } else if (milliseconds < 50) {
    bucket = "10-50";
  } else if (milliseconds < 100) {
    bucket = "50-100";
  } else if (milliseconds < 500) {
    bucket = "100-500";
  } else if (milliseconds < 1000) {
    bucket = "500-1000";
  } else {
    bucket = "1000+";
  }
  histogram[bucket]++;
}

void resetHistogram(std::map<std::string, uint64_t> &histogram) {
  histogram = {
      {"0-1",      0},
      {"1-5",      0},
      {"5-10",     0},
      {"10-50",    0},
      {"50-100",   0},
      {"100-500",  0},
      {"500-1000", 0},
      {"1000+",    0},
  };
}

uint64_t lockCount(void) {
  uint64_t count = 0;
  for (const auto &entry : state.locks) {
    count += entry.second.count;
  }
  return count;
}

uint64_t cpuLockCount(void) {
  const auto found = state.locks.find(CPU_FREQ_LOCK);
  return found == state.locks.end() ? 0 : found->second.count;
}

int currentFrequency(void) {
  if (cpuLockCount() > 0) {
    return state.configured_max_frequency_mhz >= 240   ? 240
           : state.configured_max_frequency_mhz >= 160 ? 160
                                                       : 80;
  }
  return 80;
}

bool addChecked(uint64_t left, uint64_t right, uint64_t &result) {
  if (right > std::numeric_limits<uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

bool accountWorkLocked(uint64_t work_us) {
  uint64_t updated = 0;
  if (!addChecked(state.pending_work_us, work_us, updated)) {
    state.accounting_invalid = true;
    requestFailureExit();
    return false;
  }
  state.pending_work_us = updated;
  return true;
}

bool addWorkTotalLocked(uint64_t &total, uint64_t work_us) {
  uint64_t updated = 0;
  if (!addChecked(total, work_us, updated)) {
    state.accounting_invalid = true;
    requestFailureExit();
    return false;
  }
  total = updated;
  return true;
}

void ensureLock(int lock_type, const char *lock_name) {
  auto &lock = state.locks[lock_type];
  if (lock.name.empty() && lock_name != nullptr) {
    lock.name = lock_name;
  }
  if (lock.histogram.empty()) {
    resetHistogram(lock.histogram);
  }
}

void integrateLocked(uint32_t now) {
  if (!state.started) {
    return;
  }
  const uint32_t elapsed = clockElapsed(now, state.last_time_ms);
  if (elapsed == 0) {
    return;
  }

  if (state.model.accounting_enabled) {
    uint64_t elapsed_us = 0;
    if (elapsed > std::numeric_limits<uint64_t>::max() / 1000
        || !addChecked(0, static_cast<uint64_t>(elapsed) * 1000, elapsed_us)) {
      state.accounting_invalid = true;
      requestFailureExit();
      return;
    }
    const uint64_t work_us = std::min(state.pending_work_us, elapsed_us);
    state.pending_work_us -= work_us;
    const int frequency = currentFrequency();
    uint64_t *bucket = frequency >= 240   ? &state.work_240_us
                       : frequency >= 160 ? &state.work_160_us
                                          : &state.work_80_us;
    uint64_t updated = 0;
    if (!addChecked(*bucket, work_us, updated)) {
      state.accounting_invalid = true;
      requestFailureExit();
      return;
    }
    *bucket = updated;
    const bool sleepEligible = state.light_sleep_enabled && lockCount() == 0 && state.task_idle;
    if (sleepEligible) {
      if (!addChecked(state.eligible_light_sleep_us, elapsed_us, updated)) {
        state.accounting_invalid = true;
        requestFailureExit();
        return;
      }
      state.eligible_light_sleep_us = updated;
      if (!addChecked(state.light_sleep_work_us, work_us, updated)) {
        state.accounting_invalid = true;
        requestFailureExit();
        return;
      }
      state.light_sleep_work_us = updated;
    }
  }

  state.display_ms[state.display_state] += elapsed;
  if (state.radio_connected) {
    state.radio_connected_ms += elapsed;
  }
  state.gps_ms[state.gps_state] += elapsed;
  state.frequency_ms[currentFrequency()] += elapsed;

  if (state.timer_queue_idle) {
    state.timer_idle_ms += elapsed;
  }
  if (state.task_idle) {
    state.task_idle_ms += elapsed;
  }
  if (lockCount() == 0) {
    state.lock_free_ms += elapsed;
    if (state.light_sleep_enabled && state.task_idle
        && (state.model.accounting_enabled || state.timer_queue_idle)) {
      state.light_sleep_ms += elapsed;
    }
  }

  state.last_time_ms = now;
}

void resetCountersLocked(uint32_t now) {
  state.window_start_ms = now;
  state.last_time_ms = now;
  state.timer_fires.clear();
  for (const char *name : TIMER_NAMES) {
    state.timer_fires.emplace(name, 0);
  }
  state.invalidated_area_pixels = 0;
  state.flushed_pixels = 0;
  state.ui_cycles = 0;
  state.cycle_timer_fired = false;
  state.cycle_task_woke = false;
  state.timer_queue_idle = true;
  state.task_idle = true;
  state.pending_work_us = 0;
  state.work_80_us = 0;
  state.work_160_us = 0;
  state.work_240_us = 0;
  state.light_sleep_work_us = 0;
  state.eligible_light_sleep_us = 0;
  state.poll_work_us = 0;
  state.timer_work_us = 0;
  state.timer_work_us_by_name.clear();
  state.timer_idle_ms = 0;
  state.task_idle_ms = 0;
  state.lock_free_ms = 0;
  state.light_sleep_ms = 0;
  state.queue_receives.clear();
  state.queue_empty_receives.clear();
  state.task_delay_count.clear();
  state.task_delay_ms.clear();
  state.frequency_ms.clear();
  state.display_ms = {
      {"dim", 0},
      {"off", 0},
      {"on",  0}
  };
  state.radio_connected_ms = 0;
  state.radio_events.clear();
  state.gps_ms = {
      {"acquiring", 0},
      {"degraded",  0},
      {"off",       0},
      {"standby",   0},
      {"tracking",  0}
  };

  for (auto &entry : state.locks) {
    auto &lock = entry.second;
    lock.acquire_count = 0;
    lock.release_count = 0;
    lock.unbalanced_release_count = 0;
    lock.total_hold_ms = 0;
    resetHistogram(lock.histogram);
    lock.active_start_ms = lock.count > 0 ? now : 0;
    for (auto &owner : lock.owners) {
      auto &data = owner.second;
      data.acquire_count = 0;
      data.release_count = 0;
      data.total_hold_ms = 0;
      resetHistogram(data.histogram);
      for (auto &start : data.active_starts) {
        start = now;
      }
    }
  }
}

void ensureStartedLocked(void) {
  if (!state.started) {
    state.started = true;
    state.scenario = "sim";
    state.display_state = "on";
    state.gps_state = "off";
    resetCountersLocked(clockMillis());
  }
}

uint64_t activeHold(const LockData &lock, uint32_t now) {
  if (lock.count == 0) {
    return lock.total_hold_ms;
  }
  return lock.total_hold_ms + clockElapsed(now, lock.active_start_ms);
}

uint64_t activeOwnerHold(const OwnerData &owner, uint32_t now) {
  uint64_t total = owner.total_hold_ms;
  for (const uint32_t start : owner.active_starts) {
    total += clockElapsed(now, start);
  }
  return total;
}

uint64_t reportDuration(uint64_t milliseconds) {
  return ((milliseconds + REPORT_TIME_QUANTUM_MS / 2) / REPORT_TIME_QUANTUM_MS)
         * REPORT_TIME_QUANTUM_MS;
}

std::map<std::string, uint64_t> currentHistogram(const std::map<std::string, uint64_t> &histogram,
                                                 uint32_t active_start,
                                                 uint32_t count,
                                                 uint32_t now) {
  auto result = histogram;
  if (count > 0) {
    addHistogram(result, reportDuration(clockElapsed(now, active_start)));
  }
  return result;
}

std::map<std::string, uint64_t> currentOwnerHistogram(const OwnerData &owner, uint32_t now) {
  auto result = owner.histogram;
  for (const uint32_t start : owner.active_starts) {
    addHistogram(result, reportDuration(clockElapsed(now, start)));
  }
  return result;
}

bool parseNumber(const std::string &value, double &number) {
  const std::string text = trim(value);
  if (text.empty() || text == "null") {
    return false;
  }
  char *end = nullptr;
  number = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && std::isfinite(number) && number >= 0.0;
}

bool assignModelValue(CurrentModel &model,
                      const std::string &anchor,
                      const std::string &entry,
                      double value) {
  if (anchor == "esp32s3_mcu" && entry == "active_cpu_80mhz") {
    model.mcu_80 = value;
  } else if (anchor == "esp32s3_mcu" && entry == "active_cpu_160mhz") {
    model.mcu_160 = value;
  } else if (anchor == "esp32s3_mcu" && entry == "active_cpu_240mhz") {
    model.mcu_240 = value;
  } else if (anchor == "esp32s3_mcu" && entry == "light_sleep") {
    model.light_sleep = value;
  } else if (anchor == "esp32s3_radio" && entry == "ble_tx_0dbm") {
    model.radio_tx = value;
  } else if (anchor == "esp32s3_radio" && entry == "ble_connected_idle_floor") {
    model.connected_idle = value;
  } else if (anchor == "st7789_display" && entry == "panel_on") {
    model.display_panel_on = value;
  } else if (anchor == "st7789_display" && entry == "panel_sleep_in") {
    model.display_panel_sleep = value;
  } else if (anchor == "gps_unit_v11" && entry == "module_acquisition_3v3") {
    model.gps_acquisition = value;
  } else if (anchor == "gps_unit_v11" && entry == "module_tracking_3v3") {
    model.gps_tracking = value;
  } else if (anchor == "gps_unit_v11" && entry == "standby_pcas12_module") {
    model.gps_standby = value;
  } else {
    return false;
  }
  return true;
}

std::string shellQuote(const std::string &value) {
  std::string quoted = "'";
  for (const char character : value) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted += character;
    }
  }
  quoted += "'";
  return quoted;
}

std::filesystem::path resolvedPath(const std::filesystem::path &path) {
  std::error_code error;
  const std::filesystem::path absolute = std::filesystem::absolute(path, error);
  return error ? std::filesystem::path() : absolute.lexically_normal();
}

std::string digestPath(const std::filesystem::path &path) {
  const std::string quoted_path = shellQuote(path.string());
  for (const std::string command : {"sha256sum -- ", "shasum -a 256 "}) {
    const std::string invocation = command + quoted_path;
    FILE *pipe = popen(invocation.c_str(), "r");
    if (pipe == nullptr) {
      continue;
    }
    std::string output;
    char buffer[128] = {};
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      output += buffer;
    }
    const int status = pclose(pipe);
    if (status != 0) {
      continue;
    }
    const size_t separator = output.find_first_of(" \t\r\n");
    const std::string digest = output.substr(0, separator);
    if (digest.size() == 64
        && std::all_of(digest.begin(), digest.end(),
                       [](unsigned char character) { return std::isxdigit(character) != 0; })) {
      return digest;
    }
  }
  return {};
}

std::string digestBytes(const std::string &contents) {
  std::error_code error;
  const std::filesystem::path temporary_directory = std::filesystem::temp_directory_path(error);
  if (error) {
    return {};
  }
  std::string temporary = (temporary_directory / "furble-power-model-XXXXXX").string();
  std::vector<char> temporary_name(temporary.begin(), temporary.end());
  temporary_name.push_back('\0');
  const int descriptor = mkstemp(temporary_name.data());
  if (descriptor < 0) {
    return {};
  }

  size_t offset = 0;
  while (offset < contents.size()) {
    const ssize_t written = write(descriptor, contents.data() + offset, contents.size() - offset);
    if (written > 0) {
      offset += static_cast<size_t>(written);
    } else if (written < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  close(descriptor);
  const std::filesystem::path path(temporary_name.data());
  const std::string digest = offset == contents.size() ? digestPath(path) : std::string();
  unlink(temporary_name.data());
  return digest;
}

bool parseUnsignedMicroseconds(const std::string &text, uint64_t &value) {
  const std::string trimmed = trim(text);
  if (trimmed.empty() || trimmed.find_first_not_of("0123456789") != std::string::npos) {
    return false;
  }
  try {
    size_t parsed = 0;
    value = std::stoull(trimmed, &parsed);
    return parsed == trimmed.size();
  } catch (const std::exception &) {
    return false;
  }
}

std::string unquote(std::string value) {
  value = trim(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool validConfidence(const std::string &value) {
  return value == "datasheet" || value == "published-measurement" || value == "estimated"
         || value == "measured-local";
}

bool parseAccountingModel(const std::string &contents, CurrentModel &model) {
  bool accountingBlockSeen = false;
  {
    std::istringstream scan(contents);
    std::string scanLine;
    while (std::getline(scan, scanLine)) {
      const std::string scanText = trim(scanLine);
      if (scanText.empty() || scanText.front() == '#') {
        continue;
      }
      const size_t scanFirst = scanLine.find_first_not_of(' ');
      const size_t scanColon = scanText.find(':');
      const bool scanAccountingKey =
          scanColon != std::string::npos && trim(scanText.substr(0, scanColon)) == "accounting";
      if (scanFirst == 0 && scanAccountingKey) {
        if (accountingBlockSeen || scanText != "accounting:") {
          return false;
        }
        accountingBlockSeen = true;
      }
    }
  }
  std::istringstream input(contents);
  std::string line;
  bool inAccounting = false;
  bool accountingSeen = false;
  bool inTimers = false;
  bool inPoll = false;
  bool pollValue = false;
  bool pollSource = false;
  bool pollConfidence = false;
  bool pollValueSeen = false;
  bool pollSourceSeen = false;
  bool pollConfidenceSeen = false;
  bool versionSeen = false;
  bool calibrationSeen = false;
  bool pollSectionSeen = false;
  bool timerSectionSeen = false;
  std::string timer;
  std::set<std::string> timerValues;
  std::set<std::string> timerSources;
  std::set<std::string> timerConfidence;
  while (std::getline(input, line)) {
    const std::string text = trim(line);
    if (text.empty() || text.front() == '#') {
      continue;
    }
    const size_t first = line.find_first_not_of(' ');
    const int indent = first == std::string::npos ? 0 : static_cast<int>(first);
    const size_t colon = text.find(':');
    if (colon == std::string::npos) {
      if (inAccounting && indent > 0) {
        return false;
      }
      continue;
    }
    const std::string key = trim(text.substr(0, colon));
    const std::string value = trim(text.substr(colon + 1));
    if (indent == 0) {
      if (key == "accounting") {
        if (accountingSeen || text != "accounting:") {
          return false;
        }
        accountingSeen = true;
        inAccounting = true;
        model.accounting_enabled = true;
        continue;
      }
      if (inAccounting) {
        break;
      }
      continue;
    }
    if (!inAccounting) {
      continue;
    }
    if (indent == 2) {
      inTimers = key == "timer_active_us_per_fire";
      inPoll = key == "ui_poll_active_us_per_cycle";
      if (inTimers || inPoll) {
        timer.clear();
      }
      if (key == "version") {
        if (versionSeen || value != "1") {
          return false;
        }
        versionSeen = true;
        model.accounting_version = 1;
      } else if (key == "calibration_status") {
        const std::string status = unquote(value);
        if (calibrationSeen || (status != "uncalibrated" && status != "calibrated")) {
          return false;
        }
        calibrationSeen = true;
        model.calibration_status = status;
      } else if (inPoll) {
        if (pollSectionSeen || !value.empty()) {
          return false;
        }
        pollSectionSeen = true;
      } else if (inTimers) {
        if (timerSectionSeen || !value.empty()) {
          return false;
        }
        timerSectionSeen = true;
      } else {
        return false;
      }
      continue;
    }
    if (inPoll && indent == 4) {
      if (key == "value_us") {
        if (pollValueSeen || !parseUnsignedMicroseconds(value, model.ui_poll_active_us)) {
          return false;
        }
        pollValueSeen = true;
        pollValue = true;
      } else if (key == "source") {
        if (pollSourceSeen) {
          return false;
        }
        pollSourceSeen = true;
        model.poll_source = unquote(value);
        pollSource = !model.poll_source.empty();
      } else if (key == "confidence") {
        if (pollConfidenceSeen) {
          return false;
        }
        pollConfidenceSeen = true;
        model.poll_confidence = unquote(value);
        pollConfidence = validConfidence(model.poll_confidence);
      } else {
        return false;
      }
      continue;
    }
    if (inPoll) {
      return false;
    }
    if (!inPoll && !inTimers) {
      return false;
    }
    if (inTimers && indent == 4 && value.empty()) {
      timer = key;
      if (timer.empty()) {
        return false;
      }
      if (!model.timer_active_us.emplace(timer, CurrentModel::AccountingCost {}).second) {
        return false;
      }
      continue;
    }
    if (inTimers && indent == 4) {
      return false;
    }
    if (inTimers && indent != 6) {
      return false;
    }
    if (inTimers && indent == 6 && timer.empty()) {
      return false;
    }
    if (inTimers && indent == 6 && !timer.empty()) {
      auto &cost = model.timer_active_us[timer];
      if (key == "value_us") {
        if (timerValues.count(timer) != 0) {
          return false;
        }
        timerValues.insert(timer);
        if (!parseUnsignedMicroseconds(value, cost.value_us)) {
          return false;
        }
      } else if (key == "source") {
        if (timerSources.count(timer) != 0) {
          return false;
        }
        timerSources.insert(timer);
        cost.source = unquote(value);
        if (cost.source.empty()) {
          return false;
        }
      } else if (key == "confidence") {
        if (timerConfidence.count(timer) != 0) {
          return false;
        }
        timerConfidence.insert(timer);
        cost.confidence = unquote(value);
        if (!validConfidence(cost.confidence)) {
          return false;
        }
      } else {
        return false;
      }
    }
  }
  if (!model.accounting_enabled) {
    return true;
  }
  if (!versionSeen || !calibrationSeen || !pollSectionSeen || !timerSectionSeen || !pollValue
      || !pollSource || !pollConfidence || model.timer_active_us.empty()) {
    return false;
  }
  for (const auto &entry : model.timer_active_us) {
    if (timerValues.count(entry.first) == 0 || timerSources.count(entry.first) == 0
        || timerConfidence.count(entry.first) == 0) {
      return false;
    }
  }
  return true;
}

std::string accountingFingerprint(const CurrentModel &model, const std::string &model_digest) {
  if (!model.accounting_enabled) {
    return {};
  }
  std::ostringstream canonical;
  canonical << "model_digest=" << model_digest << "\nversion=" << model.accounting_version
            << "\nstatus=" << model.calibration_status << "\ncoeff=" << std::setprecision(17)
            << model.mcu_80 << ',' << model.mcu_160 << ',' << model.mcu_240 << ','
            << model.light_sleep << ',' << model.radio_tx << ',' << model.connected_idle << ','
            << model.display_panel_on << ',' << model.display_panel_sleep << ','
            << model.display_backlight << ',' << model.gps_acquisition << ',' << model.gps_tracking
            << ',' << model.gps_standby << ',' << model.pmic << ',' << model.peripheral
            << "\npoll=" << model.ui_poll_active_us << ':' << model.poll_source << ':'
            << model.poll_confidence << '\n';
  for (const auto &entry : model.timer_active_us) {
    canonical << entry.first << '=' << entry.second.value_us << ':' << entry.second.source << ':'
              << entry.second.confidence << '\n';
  }
  return digestBytes(canonical.str());
}

ModelLoadResult loadCurrentModel(void) {
  ModelLoadResult result;
  std::vector<std::filesystem::path> candidates;
  if (const char *configured = std::getenv("FURBLE_POWER_MODEL"); configured != nullptr) {
    candidates.emplace_back(configured);
  } else {
    candidates.emplace_back("tools/power-model/board-currents.yaml");
    candidates.emplace_back("../tools/power-model/board-currents.yaml");
    candidates.emplace_back("../../tools/power-model/board-currents.yaml");
    candidates.emplace_back(std::filesystem::path(__FILE__).parent_path().parent_path()
                            / "tools/power-model/board-currents.yaml");
  }
  if (candidates.size() == 1) {
    result.source = resolvedPath(candidates.front());
  }

  std::ifstream file;
  std::filesystem::path selected;
  for (const auto &candidate : candidates) {
    file.open(candidate);
    if (file) {
      selected = candidate;
      break;
    }
    file.clear();
  }
  if (!file) {
    return result;
  }

  result.source = resolvedPath(selected);
  if (result.source.empty()) {
    return result;
  }
  std::set<std::string> seen_entries;
  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  if (!parseAccountingModel(contents, result.model)) {
    return result;
  }
  std::istringstream input(contents);
  std::string anchor;
  std::string pending_entry;
  bool in_boards = false;
  bool in_s3 = false;
  std::string board_section;
  std::string board_entry;
  std::string line;
  while (std::getline(input, line)) {
    const size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line.resize(comment);
    }
    if (trim(line).empty()) {
      continue;
    }

    const int indent = static_cast<int>(line.find_first_not_of(' '));
    const std::string text = trim(line);
    const size_t colon = text.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    const std::string key = trim(text.substr(0, colon));
    const std::string value = trim(text.substr(colon + 1));

    if (indent <= 2 && value.find('&') != std::string::npos) {
      const size_t marker = value.find('&');
      const size_t end = value.find_first_of(" \t", marker);
      anchor =
          value.substr(marker + 1, end == std::string::npos ? std::string::npos : end - marker - 1);
      pending_entry.clear();
    } else if (indent <= 2 && key != "_shared") {
      anchor.clear();
      pending_entry.clear();
    }

    if (!anchor.empty()) {
      if (indent == 4 && value.empty()) {
        pending_entry = key;
      } else if (indent >= 6 && key == "value_ma" && !pending_entry.empty()) {
        double parsed = 0.0;
        if (parseNumber(value, parsed)) {
          if (assignModelValue(result.model, anchor, pending_entry, parsed)) {
            if (!seen_entries.emplace(anchor + ":" + pending_entry).second) {
              return result;
            }
          }
        } else {
          // Unknown YAML entries are allowed to evolve independently; a
          // malformed value is fatal only when this loader actually uses it.
          CurrentModel ignored;
          if (assignModelValue(ignored, anchor, pending_entry, 0.0)) {
            return result;
          }
        }
      }
    }

    if (indent == 0 && key == "boards") {
      in_boards = true;
      in_s3 = false;
      board_section.clear();
      continue;
    }
    if (in_boards && indent == 2) {
      in_s3 = key == "m5stick-s3";
      board_section.clear();
      board_entry.clear();
      continue;
    }
    if (in_s3 && indent == 4 && value.empty()) {
      board_section = key;
      board_entry.clear();
      continue;
    }
    if (in_s3 && board_section == "display" && indent == 6 && value.empty()) {
      board_entry = key;
      continue;
    }
    if (in_s3 && board_section == "display" && board_entry == "backlight_full" && indent >= 8
        && key == "value_ma") {
      double parsed = 0.0;
      if (!parseNumber(value, parsed)) {
        return result;
      }
      result.model.display_backlight = parsed;
      if (!seen_entries.emplace("m5stick-s3:display.backlight_full").second) {
        return result;
      }
    }
  }

  static constexpr const char *REQUIRED_ENTRIES[] = {
      "esp32s3_mcu:active_cpu_80mhz",
      "esp32s3_mcu:active_cpu_160mhz",
      "esp32s3_mcu:active_cpu_240mhz",
      "esp32s3_mcu:light_sleep",
      "esp32s3_radio:ble_tx_0dbm",
      "esp32s3_radio:ble_connected_idle_floor",
      "st7789_display:panel_on",
      "st7789_display:panel_sleep_in",
      "gps_unit_v11:module_acquisition_3v3",
      "gps_unit_v11:module_tracking_3v3",
      "gps_unit_v11:standby_pcas12_module",
      "m5stick-s3:display.backlight_full",
  };
  for (const char *entry : REQUIRED_ENTRIES) {
    if (seen_entries.count(entry) == 0) {
      return result;
    }
  }
  result.digest = digestBytes(contents);
  if (result.digest.empty()) {
    return result;
  }
  result.model.accounting_fingerprint = accountingFingerprint(result.model, result.digest);
  if (result.model.accounting_enabled && result.model.accounting_fingerprint.empty()) {
    return result;
  }
  result.valid = true;
  return result;
}

double perSecond(uint64_t value, uint64_t duration_ms) {
  return duration_ms == 0 ? 0.0 : static_cast<double>(value) * 1000.0 / duration_ms;
}

void writeHistogram(std::ostream &output, const std::map<std::string, uint64_t> &histogram) {
  output << "{";
  bool first = true;
  for (const auto &entry : histogram) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n        \"" << jsonEscape(entry.first) << "\": " << entry.second;
  }
  if (!histogram.empty()) {
    output << "\n      ";
  }
  output << "}";
}

void writeDouble(std::ostream &output, double value) {
  output << std::fixed << std::setprecision(6) << value;
}

void writeReportLocked(const std::filesystem::path &path,
                       const std::string &scenario,
                       uint32_t now) {
  if (state.accounting_invalid) {
    requestFailureExit();
    return;
  }
  integrateLocked(now);
  const uint64_t duration_ms = clockElapsed(now, state.window_start_ms);
  const uint64_t safe_duration_ms = std::max<uint64_t>(duration_ms, 1);
  if (!state.model_loaded) {
    const ModelLoadResult loaded_model = loadCurrentModel();
    if (!loaded_model.valid) {
      std::cerr << "Could not load a valid power model; report not written: " << loaded_model.source
                << '\n';
      requestFailureExit();
      return;
    }
    state.model = loaded_model.model;
    state.model_source = loaded_model.source.string();
    state.model_digest = loaded_model.digest;
    if (!state.reporting_enabled && state.model.accounting_enabled) {
      state.accounting_invalid = true;
      requestFailureExit();
      return;
    }
    state.model_loaded = true;
  }
  if (state.accounting_invalid || (state.model.accounting_enabled && state.pending_work_us != 0)) {
    std::cerr << "Power accounting invalid or has unresolved pending work; report not written\n";
    requestFailureExit();
    return;
  }
  const CurrentModel &model = state.model;

  const uint64_t display_on_raw_ms = state.display_ms["on"];
  const uint64_t display_dim_raw_ms = state.display_ms["dim"];
  const uint64_t display_off_raw_ms = state.display_ms["off"];
  const uint64_t display_on_ms = reportDuration(display_on_raw_ms);
  const uint64_t display_dim_ms = reportDuration(display_dim_raw_ms);
  const uint64_t display_off_ms = reportDuration(display_off_raw_ms);
  const uint64_t gps_acquiring_raw_ms = state.gps_ms["acquiring"];
  const uint64_t gps_degraded_raw_ms = state.gps_ms["degraded"];
  const uint64_t gps_tracking_raw_ms = state.gps_ms["tracking"];
  const uint64_t gps_standby_raw_ms = state.gps_ms["standby"];
  const uint64_t gps_acquiring_ms = reportDuration(gps_acquiring_raw_ms);
  const uint64_t gps_degraded_ms = reportDuration(gps_degraded_raw_ms);
  const uint64_t gps_tracking_ms = reportDuration(gps_tracking_raw_ms);
  const uint64_t gps_standby_ms = reportDuration(gps_standby_raw_ms);
  const std::map<int, uint64_t> frequencies = state.frequency_ms;

  const uint64_t frequency_80_raw_ms = frequencies.count(80) > 0 ? frequencies.at(80) : 0;
  const uint64_t frequency_160_raw_ms = frequencies.count(160) > 0 ? frequencies.at(160) : 0;
  const uint64_t frequency_240_raw_ms = frequencies.count(240) > 0 ? frequencies.at(240) : 0;
  const uint64_t frequency_80_ms = reportDuration(frequency_80_raw_ms);
  const uint64_t frequency_160_ms = reportDuration(frequency_160_raw_ms);
  const uint64_t frequency_240_ms = reportDuration(frequency_240_raw_ms);
  const uint64_t light_sleep_raw_ms = state.light_sleep_ms;
  const uint64_t light_sleep_ms = reportDuration(light_sleep_raw_ms);
  const uint64_t light_sleep_in_80 = std::min(frequency_80_raw_ms, light_sleep_raw_ms);
  uint64_t adjusted_light_sleep_us = 0;
  if (model.accounting_enabled) {
    if (state.eligible_light_sleep_us < state.light_sleep_work_us) {
      requestFailureExit();
      return;
    }
    adjusted_light_sleep_us = state.eligible_light_sleep_us - state.light_sleep_work_us;
  }

  const double mcu_ma =
      (static_cast<double>(light_sleep_in_80) * model.light_sleep
       + static_cast<double>(frequency_80_raw_ms - light_sleep_in_80) * model.mcu_80
       + static_cast<double>(frequency_160_raw_ms) * model.mcu_160
       + static_cast<double>(frequency_240_raw_ms) * model.mcu_240)
      / safe_duration_ms;
  const double modeled_work_extra_ma = static_cast<double>(state.light_sleep_work_us)
                                       * (model.mcu_80 - model.light_sleep)
                                       / (static_cast<double>(safe_duration_ms) * 1000.0);
  const double adjusted_mcu_ma = mcu_ma + modeled_work_extra_ma;
  const double display_ma =
      (static_cast<double>(display_on_raw_ms) * (model.display_panel_on + model.display_backlight)
       + static_cast<double>(display_dim_raw_ms)
             * (model.display_panel_on + model.display_backlight * 32.0 / 255.0)
       + static_cast<double>(display_off_raw_ms) * model.display_panel_sleep)
      / safe_duration_ms;
  const uint64_t radio_connected_raw_ms = state.radio_connected_ms;
  const uint64_t radio_connected_ms = reportDuration(radio_connected_raw_ms);
  uint64_t radio_event_count = 0;
  for (const auto &event : state.radio_events) {
    radio_event_count += event.second;
  }
  const double radio_ma = (static_cast<double>(radio_connected_raw_ms) * model.connected_idle
                           + static_cast<double>(radio_event_count) * model.radio_tx * 2.0)
                          / safe_duration_ms;
  // A degraded retry leaves the receiver rail powered but releases the CPU
  // sleep lock. Model its receiver draw as acquisition current and expose the
  // state separately so power regressions cannot disappear from the report.
  const double gps_ma =
      (static_cast<double>(gps_acquiring_raw_ms + gps_degraded_raw_ms) * model.gps_acquisition
       + static_cast<double>(gps_tracking_raw_ms) * model.gps_tracking
       + static_cast<double>(gps_standby_raw_ms) * model.gps_standby)
      / safe_duration_ms;
  const double pmic_ma = model.pmic;
  const double peripheral_ma = model.peripheral;
  const double estimated_ma =
      adjusted_mcu_ma + display_ma + radio_ma + gps_ma + pmic_ma + peripheral_ma;

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    std::cerr << "Could not write power report: " << path << '\n';
    requestFailureExit();
    return;
  }

  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"scenario\": \"" << jsonEscape(scenario) << "\",\n";
  output << "  \"estimate_kind\": \"relative simulator estimate, not a hardware measurement\",\n";
  output << "  \"model_source\": \"" << jsonEscape(state.model_source) << "\",\n";
  output << "  \"model_digest\": \"sha256:" << state.model_digest << "\",\n";
  output << "  \"model_valid\": true,\n";
  output << "  \"board\": \"m5stick-s3\",\n";
  output << "  \"duration_ms\": " << duration_ms << ",\n";
  output << "  \"activity\": {\n";
  output << "    \"timer_fires\": {";
  bool first = true;
  for (const auto &entry : state.timer_fires) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n      \"" << jsonEscape(entry.first) << "\": {\"count\": " << entry.second
           << ", \"per_second\": ";
    writeDouble(output, perSecond(entry.second, safe_duration_ms));
    output << "}";
  }
  if (!state.timer_fires.empty()) {
    output << "\n    ";
  }
  output << "},\n";
  output << "    \"invalidated_area_pixels\": " << state.invalidated_area_pixels << ",\n";
  output << "    \"invalidated_area_pixels_per_second\": ";
  writeDouble(output, perSecond(state.invalidated_area_pixels, safe_duration_ms));
  output << ",\n";
  output << "    \"flushed_pixels\": " << state.flushed_pixels << ",\n";
  output << "    \"flushed_pixels_per_second\": ";
  writeDouble(output, perSecond(state.flushed_pixels, safe_duration_ms));
  output << ",\n";
  output << "    \"scheduler\": {\n";
  output << "      \"ui_cycles\": " << state.ui_cycles << ",\n";
  output << "      \"timer_queue_idle_ms\": " << reportDuration(state.timer_idle_ms) << ",\n";
  output << "      \"task_idle_ms\": " << reportDuration(state.task_idle_ms) << ",\n";
  output << "      \"queue_receives\": {";
  first = true;
  for (const auto &entry : state.queue_receives) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n        \"" << jsonEscape(entry.first) << "\": " << entry.second;
  }
  if (!state.queue_receives.empty()) {
    output << "\n      ";
  }
  output << "},\n";
  output << "      \"queue_empty_receives\": {";
  first = true;
  for (const auto &entry : state.queue_empty_receives) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n        \"" << jsonEscape(entry.first) << "\": " << entry.second;
  }
  if (!state.queue_empty_receives.empty()) {
    output << "\n      ";
  }
  output << "},\n";
  output << "      \"task_delay_count\": {";
  first = true;
  for (const auto &entry : state.task_delay_count) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n        \"" << jsonEscape(entry.first) << "\": " << entry.second;
  }
  if (!state.task_delay_count.empty()) {
    output << "\n      ";
  }
  output << "},\n";
  output << "      \"task_delay_ms\": {";
  first = true;
  for (const auto &entry : state.task_delay_ms) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n        \"" << jsonEscape(entry.first) << "\": " << entry.second;
  }
  if (!state.task_delay_ms.empty()) {
    output << "\n      ";
  }
  output << "}\n";
  output << "    }\n";
  output << "  },\n";

  output << "  \"sleep\": {\n";
  output << "    \"locks\": {";
  first = true;
  for (const auto &entry : state.locks) {
    const auto &lock = entry.second;
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n      \"" << jsonEscape(lock.name) << "\": {\n";
    output << "        \"acquire_count\": " << lock.acquire_count << ",\n";
    output << "        \"release_count\": " << lock.release_count << ",\n";
    output << "        \"unbalanced_release_count\": " << lock.unbalanced_release_count << ",\n";
    output << "        \"current_count\": " << lock.count << ",\n";
    output << "        \"total_hold_ms\": " << reportDuration(activeHold(lock, now)) << ",\n";
    output << "        \"held_at_end\": " << (lock.count > 0 ? "true" : "false") << ",\n";
    output << "        \"hold_histogram_ms\": ";
    writeHistogram(output, currentHistogram(lock.histogram, lock.active_start_ms, lock.count, now));
    output << ",\n        \"owners\": {";
    bool first_owner = true;
    for (const auto &owner_entry : lock.owners) {
      const auto &owner = owner_entry.second;
      if (!first_owner) {
        output << ",";
      }
      first_owner = false;
      output << "\n          \"" << jsonEscape(owner_entry.first)
             << "\": {\"acquire_count\": " << owner.acquire_count
             << ", \"release_count\": " << owner.release_count
             << ", \"total_hold_ms\": " << reportDuration(activeOwnerHold(owner, now))
             << ", \"hold_histogram_ms\": ";
      writeHistogram(output, currentOwnerHistogram(owner, now));
      output << "}";
    }
    if (!lock.owners.empty()) {
      output << "\n        ";
    }
    output << "}\n      }";
  }
  if (!state.locks.empty()) {
    output << "\n    ";
  }
  output << "},\n";
  output << "    \"light_sleep\": {\n";
  output << "      \"zero_lock_ms\": " << reportDuration(state.lock_free_ms) << ",\n";
  output << "      \"timer_queue_idle_ms\": " << reportDuration(state.timer_idle_ms) << ",\n";
  output << "      \"task_idle_ms\": " << reportDuration(state.task_idle_ms) << ",\n";
  output << "      \"eligible_ms\": " << light_sleep_ms << ",\n";
  output << "      \"residency_ms\": ";
  if (model.accounting_enabled) {
    writeDouble(output, static_cast<double>(adjusted_light_sleep_us) / 1000.0);
  } else {
    output << light_sleep_ms;
  }
  output << ",\n";
  output << "      \"residency_percent\": ";
  if (model.accounting_enabled) {
    writeDouble(output, static_cast<double>(adjusted_light_sleep_us) * 100.0
                            / (1000.0 * static_cast<double>(safe_duration_ms)));
  } else {
    writeDouble(output, perSecond(light_sleep_ms, safe_duration_ms) / 10.0);
  }
  output << "\n    },\n";
  output << "    \"frequency_residency_ms\": {\n";
  output << "      \"80\": " << frequency_80_ms << ",\n";
  output << "      \"160\": " << frequency_160_ms << ",\n";
  output << "      \"240\": " << frequency_240_ms << "\n";
  output << "    },\n";
  output << "    \"configured_max_frequency_mhz\": " << state.configured_max_frequency_mhz << ",\n";
  output << "    \"configured_min_frequency_mhz\": " << state.configured_min_frequency_mhz << ",\n";
  output << "    \"light_sleep_enabled\": " << (state.light_sleep_enabled ? "true" : "false")
         << "\n";
  output << "  },\n";

  output << "  \"states\": {\n";
  output << "    \"display_ms\": {\n";
  output << "      \"on\": " << display_on_ms << ",\n";
  output << "      \"dim\": " << display_dim_ms << ",\n";
  output << "      \"off\": " << display_off_ms << "\n";
  output << "    },\n";
  output << "    \"radio_connected_ms\": " << radio_connected_ms << ",\n";
  output << "    \"radio_events\": {";
  first = true;
  for (const auto &entry : state.radio_events) {
    if (!first) {
      output << ",";
    }
    first = false;
    output << "\n      \"" << jsonEscape(entry.first) << "\": " << entry.second;
  }
  if (!state.radio_events.empty()) {
    output << "\n    ";
  }
  output << "},\n";
  output << "    \"gps_ms\": {\n";
  output << "      \"off\": " << reportDuration(state.gps_ms["off"]) << ",\n";
  output << "      \"acquiring\": " << gps_acquiring_ms << ",\n";
  output << "      \"degraded\": " << gps_degraded_ms << ",\n";
  output << "      \"tracking\": " << gps_tracking_ms << ",\n";
  output << "      \"standby\": " << gps_standby_ms << "\n";
  output << "    }\n";
  output << "  },\n";

  output << "  \"energy\": {\n";
  output << "    \"board\": \"m5stick-s3\",\n";
  output << "    \"components_mA\": {\n";
  output << "      \"mcu\": ";
  writeDouble(output, adjusted_mcu_ma);
  output << ",\n      \"radio\": ";
  writeDouble(output, radio_ma);
  output << ",\n      \"display\": ";
  writeDouble(output, display_ma);
  output << ",\n      \"gps\": ";
  writeDouble(output, gps_ma);
  output << ",\n      \"pmic\": ";
  writeDouble(output, pmic_ma);
  output << ",\n      \"peripherals\": ";
  writeDouble(output, peripheral_ma);
  output << "\n    },\n";
  output << "    \"estimated_mA\": ";
  writeDouble(output, estimated_ma);
  output << ",\n";
  output << "    \"accounting_inputs\": {\n";
  output << "      \"duration_ms\": " << duration_ms << ",\n";
  output << "      \"mcu_ms\": {\n";
  output << "        \"light_sleep_in_80\": " << light_sleep_in_80 << ",\n";
  output << "        \"frequency_80\": " << frequency_80_raw_ms << ",\n";
  output << "        \"frequency_160\": " << frequency_160_raw_ms << ",\n";
  output << "        \"frequency_240\": " << frequency_240_raw_ms << "\n";
  output << "      },\n";
  output << "      \"display_ms\": {\n";
  output << "        \"on\": " << display_on_raw_ms << ",\n";
  output << "        \"dim\": " << display_dim_raw_ms << ",\n";
  output << "        \"off\": " << display_off_raw_ms << "\n";
  output << "      },\n";
  output << "      \"radio_connected_ms\": " << radio_connected_raw_ms << ",\n";
  output << "      \"radio_event_count\": " << radio_event_count << ",\n";
  output << "      \"gps_ms\": {\n";
  output << "        \"acquiring\": " << gps_acquiring_raw_ms << ",\n";
  output << "        \"degraded\": " << gps_degraded_raw_ms << ",\n";
  output << "        \"tracking\": " << gps_tracking_raw_ms << ",\n";
  output << "        \"standby\": " << gps_standby_raw_ms << "\n";
  output << "      },\n";
  output << "      \"accounting_mode\": \""
         << (model.accounting_enabled ? "synthetic-virtual-work" : "legacy-unaccounted") << "\",\n";
  output << "      \"accounting_version\": " << model.accounting_version << ",\n";
  output << "      \"calibration_status\": \"" << jsonEscape(model.calibration_status) << "\",\n";
  output << "      \"accounting_fingerprint\": \"" << jsonEscape(model.accounting_fingerprint)
         << "\",\n";
  output << "      \"poll_work_us\": " << (model.accounting_enabled ? state.poll_work_us : 0)
         << ",\n";
  output << "      \"timer_work_us\": " << state.timer_work_us << ",\n";
  output << "      \"timer_work_by_name_us\": {";
  bool first_timer_work = true;
  for (const auto &entry : state.timer_work_us_by_name) {
    if (!first_timer_work) {
      output << ",";
    }
    first_timer_work = false;
    output << "\n        \"" << jsonEscape(entry.first) << "\": " << entry.second;
  }
  if (!state.timer_work_us_by_name.empty()) {
    output << "\n      ";
  }
  output << "},\n";
  output << "      \"work_80_us\": " << state.work_80_us << ",\n";
  output << "      \"work_160_us\": " << state.work_160_us << ",\n";
  output << "      \"work_240_us\": " << state.work_240_us << ",\n";
  output << "      \"eligible_light_sleep_us\": " << state.eligible_light_sleep_us << ",\n";
  output << "      \"light_sleep_work_us\": " << state.light_sleep_work_us << ",\n";
  output << "      \"adjusted_light_sleep_us\": "
         << (state.eligible_light_sleep_us - state.light_sleep_work_us) << ",\n";
  output << "      \"pending_work_us\": " << state.pending_work_us << ",\n";
  output << "      \"accounting_valid\": " << (state.accounting_invalid ? "false" : "true") << "\n";
  output << "    }\n";
  output << "  },\n";
  output << "  \"estimated_mA\": ";
  writeDouble(output, estimated_ma);
  output << "\n}\n";
  output.flush();
  if (!output) {
    std::cerr << "Could not flush power report: " << path << '\n';
    requestFailureExit();
    return;
  }
  output.close();
  if (output.fail()) {
    std::cerr << "Could not close power report: " << path << '\n';
    requestFailureExit();
  }
}

void resetWindowLocked(uint32_t now) {
  integrateLocked(now);
  if (state.model.accounting_enabled && state.pending_work_us != 0) {
    state.accounting_invalid = true;
    requestFailureExit();
    return;
  }
  resetCountersLocked(now);
}

}  // namespace

void profilerBegin(const char *scenario, bool reporting_enabled) {
  std::lock_guard<std::mutex> lock(state.mutex);
  state.started = true;
  state.scenario = scenario == nullptr ? "sim" : scenario;
  state.reporting_enabled = reporting_enabled;
  state.model_loaded = false;
  state.model = CurrentModel {};
  state.model_source.clear();
  state.model_digest.clear();
  state.accounting_fingerprint.clear();
  state.accounting_invalid = false;
  if (reporting_enabled) {
    const ModelLoadResult loaded_model = loadCurrentModel();
    if (!loaded_model.valid) {
      state.accounting_invalid = true;
      requestFailureExit();
    } else {
      state.model = loaded_model.model;
      state.model_source = loaded_model.source.string();
      state.model_digest = loaded_model.digest;
      state.model_loaded = true;
      state.accounting_fingerprint = loaded_model.model.accounting_fingerprint;
    }
  }
  state.display_state = "on";
  state.radio_connected = false;
  state.gps_state = "off";
  state.configured_max_frequency_mhz = 160;
  state.configured_min_frequency_mhz = 40;
  state.light_sleep_enabled = true;
  state.locks.clear();
  state.locks[CPU_FREQ_LOCK].name = "cpu_freq_max";
  state.locks[APB_FREQ_LOCK].name = "apb_freq_max";
  state.locks[NO_LIGHT_SLEEP_LOCK].name = "no_light_sleep";
  for (auto &entry : state.locks) {
    resetHistogram(entry.second.histogram);
  }
  resetCountersLocked(clockMillis());
}

void profilerTimerFire(const char *name) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  integrateLocked(clockMillis());
  uint64_t &fire_count = state.timer_fires[name == nullptr ? "unknown_timer" : name];
  if (!addWorkTotalLocked(fire_count, 1)) {
    requestFailureExit();
    return;
  }
  state.cycle_timer_fired = true;
  if (state.reporting_enabled && state.model.accounting_enabled) {
    const std::string timer_name = name == nullptr ? "unknown_timer" : name;
    const auto found = state.model.timer_active_us.find(timer_name);
    if (found == state.model.timer_active_us.end()) {
      state.accounting_invalid = true;
      requestFailureExit();
      return;
    }
    const uint64_t work_us = found->second.value_us;
    if (!accountWorkLocked(work_us) || !addWorkTotalLocked(state.timer_work_us, work_us)
        || !addWorkTotalLocked(state.timer_work_us_by_name[timer_name], work_us)) {
      requestFailureExit();
    }
  }
}

void profilerInvalidatedArea(uint64_t pixels) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  state.invalidated_area_pixels += pixels;
  // Count invalidation events for the redraw-storm probe. Independent of the
  // report window reset so a scenario controls its own measurement span.
  state.invalidation_probe_count++;
}

void profilerResetInvalidationProbe(void) {
  std::lock_guard<std::mutex> lock(state.mutex);
  state.invalidation_probe_count = 0;
}

uint32_t profilerInvalidationProbeCount(void) {
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.invalidation_probe_count;
}

void profilerFlushedPixels(uint64_t pixels) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  state.flushed_pixels += pixels;
}

void profilerBeginUiCycle(void) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  integrateLocked(clockMillis());
  if (state.reporting_enabled && state.model.accounting_enabled) {
    const uint64_t work_us = state.model.ui_poll_active_us;
    if (!accountWorkLocked(work_us) || !addWorkTotalLocked(state.poll_work_us, work_us)) {
      requestFailureExit();
      return;
    }
  }
  state.cycle_timer_fired = false;
  state.cycle_task_woke = false;
  state.ui_cycles++;
}

void profilerEndUiCycle(void) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  state.timer_queue_idle = !state.cycle_timer_fired;
  state.task_idle = !state.cycle_task_woke;
}

void profilerQueueReceive(const char *queue_name, bool returned_data) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  const std::string name = queue_name == nullptr ? "unnamed" : queue_name;
  if (returned_data) {
    state.queue_receives[name]++;
    // Queue counts are deterministic virtual activity. A host task's wake
    // timing is not used for idle residency because host scheduling still
    // races the UI thread without advancing scenario time.
  } else {
    state.queue_empty_receives[name]++;
  }
}

void profilerTaskDelay(const char *task_name, uint32_t milliseconds) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  const std::string name = task_name == nullptr ? "unnamed" : task_name;
  state.task_delay_count[name]++;
  state.task_delay_ms[name] += milliseconds;
}

void profilerSetDisplayState(const char *display_state) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  integrateLocked(clockMillis());
  state.display_state = display_state == nullptr ? "on" : display_state;
}

void profilerSetRadioConnected(bool connected) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  integrateLocked(clockMillis());
  state.radio_connected = connected;
}

void profilerRadioEvent(const char *name) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  state.radio_events[name == nullptr ? "unknown" : name]++;
}

void profilerSetGpsState(const char *gps_state) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  integrateLocked(clockMillis());
  state.gps_state = gps_state == nullptr ? "off" : gps_state;
}

const char *profilerGpsState(void) {
  static thread_local std::string current;
  std::lock_guard<std::mutex> lock(state.mutex);
  current = state.gps_state;
  return current.c_str();
}

void profilerPowerConfig(int max_frequency_mhz, int min_frequency_mhz, bool light_sleep_enabled) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  integrateLocked(clockMillis());
  state.configured_max_frequency_mhz = max_frequency_mhz;
  state.configured_min_frequency_mhz = min_frequency_mhz;
  state.light_sleep_enabled = light_sleep_enabled;
}

void profilerPowerLockAcquire(int lock_type, const char *lock_name, const char *owner) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  const uint32_t now = clockMillis();
  integrateLocked(now);
  ensureLock(lock_type, lock_name);
  auto &data = state.locks[lock_type];
  const std::string owner_name = owner == nullptr ? "unknown" : owner;
  auto &owner_data = data.owners[owner_name];
  if (owner_data.histogram.empty()) {
    resetHistogram(owner_data.histogram);
  }
  if (data.count == 0) {
    data.active_start_ms = now;
  }
  data.count++;
  data.acquire_count++;
  owner_data.acquire_count++;
  owner_data.active_starts.push_back(now);
}

void profilerPowerLockRelease(int lock_type, const char *lock_name, const char *owner) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  const uint32_t now = clockMillis();
  integrateLocked(now);
  ensureLock(lock_type, lock_name);
  auto &data = state.locks[lock_type];
  const std::string owner_name = owner == nullptr ? "unknown" : owner;
  if (data.count == 0) {
    data.unbalanced_release_count++;
    return;
  }

  data.count--;
  data.release_count++;
  if (data.count == 0) {
    const uint64_t held = clockElapsed(now, data.active_start_ms);
    data.total_hold_ms += held;
    addHistogram(data.histogram, held);
  }

  auto owner_found = data.owners.find(owner_name);
  if (owner_found == data.owners.end()) {
    owner_found = data.owners.emplace(owner_name, OwnerData {}).first;
    resetHistogram(owner_found->second.histogram);
  }
  auto &owner_data = owner_found->second;
  owner_data.release_count++;
  if (!owner_data.active_starts.empty()) {
    const uint64_t held = clockElapsed(now, owner_data.active_starts.back());
    owner_data.active_starts.pop_back();
    owner_data.total_hold_ms += held;
    addHistogram(owner_data.histogram, held);
  }
}

void profilerWriteReport(const char *path, const char *scenario) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  const std::filesystem::path output_path(path == nullptr ? "power-report.json" : path);
  const std::filesystem::path parent = output_path.parent_path();
  if (!parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) {
      requestFailureExit();
      return;
    }
  }
  writeReportLocked(output_path, scenario == nullptr ? state.scenario : scenario, clockMillis());
}

void profilerResetWindow(void) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  resetWindowLocked(clockMillis());
}

}  // namespace Furble::Sim
