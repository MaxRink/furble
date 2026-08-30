#include "power_profiler.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "clock.h"
#include "driver.h"

namespace Furble::Sim {
namespace {

constexpr int CPU_FREQ_LOCK = 0;
constexpr int APB_FREQ_LOCK = 1;
constexpr int NO_LIGHT_SLEEP_LOCK = 2;
// Background task wakeups are host-thread scheduled. Keep report residency
// values at one-second resolution so that race timing cannot move a baseline.
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
    if (state.light_sleep_enabled && state.timer_queue_idle && state.task_idle) {
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

double parseNumber(const std::string &value, double fallback) {
  const std::string text = trim(value);
  if (text.empty() || text == "null") {
    return fallback;
  }
  char *end = nullptr;
  const double number = std::strtod(text.c_str(), &end);
  return (end == text.c_str()) ? fallback : number;
}

void assignModelValue(CurrentModel &model,
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
  }
}

CurrentModel loadCurrentModel(void) {
  CurrentModel model;
  std::vector<std::filesystem::path> candidates;
  if (const char *configured = std::getenv("FURBLE_POWER_MODEL"); configured != nullptr) {
    candidates.emplace_back(configured);
  }
  candidates.emplace_back("tools/power-model/board-currents.yaml");
  candidates.emplace_back("../tools/power-model/board-currents.yaml");
  candidates.emplace_back("../../tools/power-model/board-currents.yaml");

  std::ifstream file;
  for (const auto &candidate : candidates) {
    file.open(candidate);
    if (file) {
      break;
    }
    file.clear();
  }
  if (!file) {
    return model;
  }

  std::string anchor;
  std::string pending_entry;
  bool in_boards = false;
  bool in_s3 = false;
  std::string board_section;
  std::string board_entry;
  std::string line;
  while (std::getline(file, line)) {
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
        assignModelValue(model, anchor, pending_entry, parseNumber(value, 0));
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
      model.display_backlight = parseNumber(value, model.display_backlight);
    }
  }
  return model;
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
  integrateLocked(now);
  const uint64_t duration_ms = clockElapsed(now, state.window_start_ms);
  const uint64_t safe_duration_ms = std::max<uint64_t>(duration_ms, 1);
  const CurrentModel model = loadCurrentModel();

  const uint64_t display_on_ms = reportDuration(state.display_ms["on"]);
  const uint64_t display_dim_ms = reportDuration(state.display_ms["dim"]);
  const uint64_t display_off_ms = reportDuration(state.display_ms["off"]);
  const uint64_t gps_acquiring_ms = reportDuration(state.gps_ms["acquiring"]);
  const uint64_t gps_degraded_ms = reportDuration(state.gps_ms["degraded"]);
  const uint64_t gps_tracking_ms = reportDuration(state.gps_ms["tracking"]);
  const uint64_t gps_standby_ms = reportDuration(state.gps_ms["standby"]);
  const std::map<int, uint64_t> frequencies = state.frequency_ms;

  const uint64_t frequency_80_ms =
      reportDuration(frequencies.count(80) > 0 ? frequencies.at(80) : 0);
  const uint64_t frequency_160_ms =
      reportDuration(frequencies.count(160) > 0 ? frequencies.at(160) : 0);
  const uint64_t frequency_240_ms =
      reportDuration(frequencies.count(240) > 0 ? frequencies.at(240) : 0);
  const uint64_t light_sleep_ms = reportDuration(state.light_sleep_ms);
  const uint64_t light_sleep_in_80 = std::min(frequency_80_ms, light_sleep_ms);

  const double mcu_ma = (static_cast<double>(light_sleep_in_80) * model.light_sleep
                         + static_cast<double>(frequency_80_ms - light_sleep_in_80) * model.mcu_80
                         + static_cast<double>(frequency_160_ms) * model.mcu_160
                         + static_cast<double>(frequency_240_ms) * model.mcu_240)
                        / safe_duration_ms;
  const double display_ma =
      (static_cast<double>(display_on_ms) * (model.display_panel_on + model.display_backlight)
       + static_cast<double>(display_dim_ms)
             * (model.display_panel_on + model.display_backlight * 32.0 / 255.0)
       + static_cast<double>(display_off_ms) * model.display_panel_sleep)
      / safe_duration_ms;
  const uint64_t radio_connected_ms = reportDuration(state.radio_connected_ms);
  const double radio_ma = (static_cast<double>(radio_connected_ms) * model.connected_idle
                           + static_cast<double>([&]() {
                               uint64_t count = 0;
                               for (const auto &event : state.radio_events) {
                                 count += event.second;
                               }
                               return count;
                             }()) * model.radio_tx
                                 * 2.0)
                          / safe_duration_ms;
  // A degraded retry leaves the receiver rail powered but releases the CPU
  // sleep lock. Model its receiver draw as acquisition current and expose the
  // state separately so power regressions cannot disappear from the report.
  const double gps_ma =
      (static_cast<double>(gps_acquiring_ms + gps_degraded_ms) * model.gps_acquisition
       + static_cast<double>(gps_tracking_ms) * model.gps_tracking
       + static_cast<double>(gps_standby_ms) * model.gps_standby)
      / safe_duration_ms;
  const double pmic_ma = model.pmic;
  const double peripheral_ma = model.peripheral;
  const double estimated_ma = mcu_ma + display_ma + radio_ma + gps_ma + pmic_ma + peripheral_ma;

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    std::cerr << "Could not write power report: " << path << '\n';
    requestExit(1);
    return;
  }

  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"scenario\": \"" << jsonEscape(scenario) << "\",\n";
  output << "  \"estimate_kind\": \"relative simulator estimate, not a hardware measurement\",\n";
  output << "  \"model_source\": \"tools/power-model/board-currents.yaml\",\n";
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
  output << "      \"residency_ms\": " << light_sleep_ms << ",\n";
  output << "      \"residency_percent\": ";
  writeDouble(output, perSecond(light_sleep_ms, safe_duration_ms) / 10.0);
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
  writeDouble(output, mcu_ma);
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
  output << "\n  },\n";
  output << "  \"estimated_mA\": ";
  writeDouble(output, estimated_ma);
  output << "\n}\n";
}

void resetWindowLocked(uint32_t now) {
  integrateLocked(now);
  resetCountersLocked(now);
}

}  // namespace

void profilerBegin(const char *scenario) {
  std::lock_guard<std::mutex> lock(state.mutex);
  state.started = true;
  state.scenario = scenario == nullptr ? "sim" : scenario;
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
  state.timer_fires[name == nullptr ? "unknown_timer" : name]++;
  state.cycle_timer_fired = true;
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
    std::filesystem::create_directories(parent);
  }
  writeReportLocked(output_path, scenario == nullptr ? state.scenario : scenario, clockMillis());
}

void profilerResetWindow(void) {
  std::lock_guard<std::mutex> lock(state.mutex);
  ensureStartedLocked();
  resetWindowLocked(clockMillis());
}

}  // namespace Furble::Sim
