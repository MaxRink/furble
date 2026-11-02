#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "FurbleUI.h"
#include "driver.h"
#include "fuzz.h"
#include "fuzz_machine.h"

// Seeded UI fuzzer. See fuzz.h for the contract. The fuzzer drives the real
// FurbleUI through the same FURBLE_SIM seams the scripted scenarios use
// (simPressButton, simScenarioAction, simulatorHome, simQueryState), so it
// exercises the real per-board input, navigation and settings code paths and
// not a sim-only shortcut. After every event it checks a set of invariants and
// records any that fail. Everything is derived from a single seeded PRNG, so a
// finding at step N reproduces exactly by rerunning the same seed and board.

namespace Furble::Sim {
namespace {

// Per-board physical buttons, matching UI::simPressButton's wiring. On the
// Sticks the side BtnPWR is the encoder previous and its long press is the
// universal back escape; BtnA activates focus; BtnB is encoder next. On the
// Cores BtnA/BtnB/BtnC take those three roles. The back model uses the physical
// long-press escape (navigateBack), so a page that hides the header arrow is
// still leavable and the fuzzer does not re-raise the #113 sim false positive.
#if defined(FURBLE_M5COREX)
constexpr const char *kBtnPrev = "a";
constexpr const char *kBtnSelect = "b";
constexpr const char *kBtnNext = "c";
constexpr const char *kBtnBack = "a";
#else
constexpr const char *kBtnPrev = "pwr";
constexpr const char *kBtnSelect = "a";
constexpr const char *kBtnNext = "b";
constexpr const char *kBtnBack = "pwr";
#endif

// Menu pages reachable through the real menu button click path. simScenarioAction
// looks each entry up and no-ops when a board omits the page, so listing all of
// them here is safe on every panel.
constexpr std::array<const char *, 17> kNavPages = {
    "settings",    "display",     "features",    "gps",       "gps_data", "nmea",
    "timer",       "theme",       "text_size",   "bluetooth", "about",    "power",
    "diagnostics", "device_info", "power_state", "ble",       "battery"};

// Boolean settings the real switch widgets can toggle through simScenarioAction.
constexpr std::array<const char *, 12> kToggles = {
    "gps", "gps_nmea",   "autoconnect", "reconnect",  "multiconnect",  "companion",
    "ir",  "show_title", "tx_adaptive", "conn_saver", "preset_picker", "recon_backoff"};

// Pages that must always fit their panel without scrolling. These are the
// compact home and interactive pages; a "yes" overflow on any of them is a
// layout bug. The long settings and diagnostics lists scroll by design and are
// deliberately excluded (see the overflow-sweep scenario for the same split).
bool mustFit(const std::string &page) {
  return page == "main" || page == "connected" || page == "shutter" || page == "bulb"
         || page == "bulb_run" || page == "timer_run" || page == "display"
         || (page == "timer" && !Platform::getInstance().canTimedWake());
}

enum class Event {
  SELECT,
  NEXT,
  PREV,
  BACK,
  HOME,
  NAV,
  CONNECT,
  DISCONNECT,
  SHUTTER,
  GESTURE,
  BLIND,
  INTERVAL_START,
  INTERVAL_STOP,
  TOGGLE,
  BUTTON_MODE,
  PRESET,
  COMPANION_REQUEST,
  COMPANION_ANSWER,
  COUNT,
};

// Relative weights, indexed by Event. Navigation and button presses dominate so
// the walk covers the menu tree; the modal, connect and timer events fire often
// enough to exercise their state machines.
constexpr std::array<int, static_cast<size_t>(Event::COUNT)> kWeights = {
    12,  // SELECT
    12,  // NEXT
    10,  // PREV
    8,   // BACK
    3,   // HOME
    10,  // NAV
    4,   // CONNECT
    3,   // DISCONNECT
    5,   // SHUTTER
    5,   // GESTURE
    3,   // BLIND
    4,   // INTERVAL_START
    3,   // INTERVAL_STOP
    6,   // TOGGLE
    2,   // BUTTON_MODE
    3,   // PRESET
    3,   // COMPANION_REQUEST
    4,   // COMPANION_ANSWER
};

// These are the state surfaces the fuzzer can observe without reaching into
// LVGL or Control internals. Comparing them after the settle phase makes the
// no-effect count honest: it means no visible state changed, not that a
// setting or command could not have changed behind the query boundary.
constexpr std::array<const char *, 9> kObservableQueries = {
    "page",           "focus",      "modal",         "modal_count", "connected",
    "interval_state", "bulb_state", "connect_timer", "overflow"};

struct ObservableState {
  std::array<std::string, kObservableQueries.size()> values;

  bool operator==(const ObservableState &other) const { return values == other.values; }
};

// Fuzzer state. All of it lives on the UI task, so no locking is needed.
bool active = false;
bool verbose = false;
uint64_t seed = 0;
uint32_t maxSteps = 0;
std::unique_ptr<FuzzMachine> machine;
Event pendingEvent = Event::SELECT;
std::string pendingDescription;
bool pendingWasStop = false;
bool checkpointEligible = false;
ObservableState stateBeforeApply;
uint32_t escapeActions = 0;
std::mt19937_64 rng;
std::deque<std::string> recentEvents;
uint64_t findingCount = 0;
uint64_t previousLiveness = 0;
uint32_t resumedBoots = 0;
constexpr const char *CHECKPOINT_FD_ENV = "FURBLE_SIM_FUZZ_CHECKPOINT_FD";
constexpr size_t MAX_CHECKPOINT_BYTES = 16 * 1024 * 1024;
std::map<std::string, uint32_t> classCounts;
std::map<std::string, uint32_t> eventCounts;
std::map<std::string, uint32_t> pageCounts;

const char *eventName(Event event) {
  switch (event) {
    case Event::SELECT:
      return "select";
    case Event::NEXT:
      return "next";
    case Event::PREV:
      return "prev";
    case Event::BACK:
      return "back";
    case Event::HOME:
      return "home";
    case Event::NAV:
      return "nav";
    case Event::CONNECT:
      return "connect";
    case Event::DISCONNECT:
      return "disconnect";
    case Event::SHUTTER:
      return "shutter";
    case Event::GESTURE:
      return "gesture";
    case Event::BLIND:
      return "blind";
    case Event::INTERVAL_START:
      return "interval-start";
    case Event::INTERVAL_STOP:
      return "interval-stop";
    case Event::TOGGLE:
      return "toggle";
    case Event::BUTTON_MODE:
      return "button-mode";
    case Event::PRESET:
      return "preset";
    case Event::COMPANION_REQUEST:
      return "companion-request";
    case Event::COMPANION_ANSWER:
      return "companion-answer";
    case Event::COUNT:
      break;
  }
  return "unknown";
}

uint32_t pick(uint32_t bound) {
  return fuzzBoundedRandom(rng, bound);
}

Event pickEvent(void) {
  int total = 0;
  for (int weight : kWeights) {
    total += weight;
  }
  int roll = static_cast<int>(pick(static_cast<uint32_t>(total)));
  for (size_t i = 0; i < kWeights.size(); i++) {
    roll -= kWeights[i];
    if (roll < 0) {
      return static_cast<Event>(i);
    }
  }
  return Event::SELECT;
}

void recordEvent(const std::string &description) {
  recentEvents.push_back(description);
  if (recentEvents.size() > 20) {
    recentEvents.pop_front();
  }
  if (verbose) {
    std::cout << "fuzz step " << machine->stepCount() << " event " << description << '\n';
  }
}

void recordFinding(UI *ui,
                   const std::string &bug_class,
                   const std::string &event,
                   const std::string &detail) {
  ++findingCount;
  classCounts[bug_class]++;

  std::cout << "FUZZ FINDING [" << bug_class << "] step=" << machine->stepCount()
            << " page=" << ui->simQueryState("page") << " event=" << event << " detail=" << detail
            << '\n';
  std::cout << "  recent:";
  for (const std::string &entry : recentEvents) {
    std::cout << ' ' << entry;
  }
  std::cout << '\n';
}

ObservableState captureState(UI *ui) {
  ObservableState state;
  for (size_t i = 0; i < kObservableQueries.size(); i++) {
    state.values[i] = ui->simQueryState(kObservableQueries[i]);
  }
  return state;
}

// Check the invariants that hold after any event. The stale focus and stacked
// modal checks map to the freed-object and re-entrancy bug classes; the
// overflow check maps to the narrow-panel layout class.
void checkInvariants(UI *ui, const std::string &event) {
  if (ui->simQueryState("focus") == "stale") {
    recordFinding(ui, "focus-uaf", event, "encoder focus points at a freed object");
  }

  const std::string modalCount = ui->simQueryState("modal_count");
  if (!modalCount.empty() && std::atoi(modalCount.c_str()) > 1) {
    recordFinding(ui, "modal-stack", event, "modal_count=" + modalCount);
  }

  const std::string page = ui->simQueryState("page");
  if (!page.empty()) {
    pageCounts[page]++;
  }
  if (mustFit(ui, page) && ui->simQueryState("overflow") == "yes") {
    recordFinding(ui, "layout-overflow", event, "compact page overflows the panel");
  }
  // The pages that gave up their fit check are not unchecked. Nothing may be
  // drawn over anything else on any page, and a page that scrolls has to scroll
  // a sane amount: a runaway extent is a layout fault even though scrolling is
  // allowed. ponytail: the bound is a whole extra panel of content, loose on
  // purpose so only a real runaway trips it.
  if (ui->simQueryState("label_overlaps") != "0") {
    recordFinding(ui, "layout-overlap", event, "widgets drawn over each other");
  }
  if (std::atoi(ui->simQueryState("scroll_bottom").c_str()) > maxScrollBottom()) {
    recordFinding(ui, "layout-scroll-runaway", event, "page scroll extent ran away");
  }
}

// Advance the escape audit by one action. A modal rejection or physical back
// press is followed by real LVGL settling, then this phase is re-entered until
// the page confirms that main is reachable.
void escapeTick(UI *ui) {
  if (ui->simQueryState("modal") == "open") {
    ui->simScenarioAction("companion-reject");
    machine->escapeModalRejected();
    return;
  }

  if (ui->simQueryState("page") == "main") {
    machine->escapeChecked(true);
    escapeActions = 0;
    return;
  }

  ui->simPressButton(kBtnBack, true);
  escapeActions++;
  if (escapeActions >= 24 && ui->simQueryState("page") != "main") {
    recordFinding(ui, "nav-trap", "escape-audit", "page not leavable by the model back button");
    ui->simulatorHome();
  }
  machine->escapeBackIssued();
}

void applyEvent(UI *ui, Event event) {
  switch (event) {
    case Event::SELECT:
      ui->simPressButton(kBtnSelect, false);
      break;
    case Event::NEXT:
      ui->simPressButton(kBtnNext, false);
      break;
    case Event::PREV:
      ui->simPressButton(kBtnPrev, false);
      break;
    case Event::BACK:
      ui->simPressButton(kBtnBack, true);
      break;
    case Event::HOME:
      ui->simulatorHome();
      break;
    case Event::NAV:
      ui->simScenarioAction((std::string("nav ") + kNavPages[pick(kNavPages.size())]).c_str());
      break;
    case Event::CONNECT:
      ui->simScenarioAction("connect");
      break;
    case Event::DISCONNECT:
      ui->simScenarioAction("disconnect");
      break;
    case Event::SHUTTER:
      ui->simScenarioAction("shutter");
      break;
    case Event::GESTURE:
      switch (pick(3)) {
        case 0:
          ui->simScenarioAction("main-press-hold");
          break;
        case 1:
          ui->simScenarioAction("main-double-click");
          break;
        default:
          ui->simScenarioAction("main-click-hold");
          break;
      }
      break;
    case Event::BLIND:
      ui->simScenarioAction("blind");
      break;
    case Event::INTERVAL_START:
      ui->simScenarioAction("intervalometer");
      break;
    case Event::INTERVAL_STOP:
      ui->simScenarioAction("stop");
      break;
    case Event::TOGGLE:
      ui->simScenarioAction((std::string("toggle ") + kToggles[pick(kToggles.size())]).c_str());
      break;
    case Event::BUTTON_MODE:
      ui->simScenarioAction(pick(2) == 0 ? "button-mode one-button" : "button-mode two-button");
      break;
    case Event::PRESET:
      ui->simScenarioAction(pick(2) == 0 ? "preset-step-up" : "preset-step-down");
      break;
    case Event::COMPANION_REQUEST:
      ui->simScenarioAction("companion-pair-request");
      break;
    case Event::COMPANION_ANSWER:
      ui->simScenarioAction(pick(2) == 0 ? "companion-accept" : "companion-reject");
      break;
    case Event::COUNT:
      break;
  }
}

void finish(void) {
  active = false;
  std::cout << "FUZZ SUMMARY seed=" << seed << " steps=" << maxSteps
            << " attempted=" << machine->attempted()
            << " observed_delta=" << machine->observedDelta()
            << " no_observed_delta=" << machine->noObservedDelta()
            << " settled=" << machine->settled()
            << " interrupted_by_restart=" << machine->interruptedByRestart()
            << " resumed_boots=" << resumedBoots
            << " timer_stop_checks=" << machine->timerStopChecks()
            << " liveness=" << previousLiveness + livenessViolationCount()
            << " findings=" << findingCount << '\n';
  for (const auto &entry : classCounts) {
    std::cout << "  class " << entry.first << " count " << entry.second << '\n';
  }
  std::cout << "FUZZ EVENTS classes=" << eventCounts.size() << ':';
  for (const auto &entry : eventCounts) {
    std::cout << ' ' << entry.first << '=' << entry.second;
  }
  std::cout << '\n';
  std::cout << "FUZZ COVERAGE pages=" << pageCounts.size() << ':';
  for (const auto &entry : pageCounts) {
    std::cout << ' ' << entry.first << '=' << entry.second;
  }
  std::cout << '\n';
  std::cout.flush();
  requestExit(findingCount == 0 ? 0 : 1);
}

// The checkpoint is harness state, not simulated flash or firmware RAM.
// Explicit fields and a version make malformed or incompatible state fail
// closed instead of silently restarting the random walk from a different seed.
std::array<uint32_t *, 14> checkpointFields(FuzzMachine::State &s) {
  return {&s.phase,
          &s.settleNext,
          &s.maxSteps,
          &s.escapeCadence,
          &s.stepCount,
          &s.settleRemaining,
          &s.attempted,
          &s.observedDelta,
          &s.noObservedDelta,
          &s.settled,
          &s.timerStopChecks,
          &s.finishing,
          &s.interruptedByRestart,
          &s.applyStarted};
}

template <typename T>
bool readNumber(std::istream &input, T &value) {
  std::string token;
  if (!(input >> token)) {
    return false;
  }
  const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
  return result.ec == std::errc {} && result.ptr == token.data() + token.size();
}

void writeCounts(std::ostream &output, const std::map<std::string, uint32_t> &counts) {
  output << counts.size() << '\n';
  for (const auto &[name, count] : counts) {
    output << std::quoted(name) << ' ' << count << '\n';
  }
}

bool readCounts(std::istream &input, std::map<std::string, uint32_t> &counts) {
  uint32_t size = 0;
  if (!readNumber(input, size) || size > 256) {
    return false;
  }
  for (uint32_t i = 0; i < size; ++i) {
    std::string name;
    uint32_t count = 0;
    if (!(input >> std::quoted(name)) || name.empty() || name.size() > 128
        || !readNumber(input, count) || count == 0 || count > maxSteps
        || !counts.emplace(name, count).second) {
      return false;
    }
  }
  return true;
}

uint64_t countTotal(const std::map<std::string, uint32_t> &counts) {
  uint64_t total = 0;
  for (const auto &[name, count] : counts) {
    total += count;
  }
  return total;
}

bool restoreCheckpoint(const std::string &payload) {
  std::istringstream input(payload);
  std::string version;
  uint64_t savedSeed = 0;
  uint32_t savedBudget = 0;
  FuzzMachine::State state;
  if (!(input >> version) || version != "FURBLE_FUZZ_RESTART_1" || !readNumber(input, savedSeed)
      || savedSeed != seed || !readNumber(input, savedBudget) || savedBudget != maxSteps
      || !readNumber(input, resumedBoots) || resumedBoots == 0
      || !readNumber(input, previousLiveness) || !readNumber(input, findingCount)) {
    return false;
  }
  for (auto *field : checkpointFields(state)) {
    if (!readNumber(input, *field)) {
      return false;
    }
  }
  if (state.maxSteps != maxSteps || resumedBoots > state.attempted
      || state.interruptedByRestart != resumedBoots || !machine->restore(state) || !(input >> rng)
      || !readCounts(input, classCounts) || !readCounts(input, eventCounts)
      || !readCounts(input, pageCounts) || countTotal(classCounts) != findingCount
      || countTotal(eventCounts) != machine->attempted()
      || countTotal(pageCounts) > machine->settled()) {
    return false;
  }
  uint32_t recentCount = 0;
  if (!readNumber(input, recentCount) || recentCount > 20) {
    return false;
  }
  for (uint32_t i = 0; i < recentCount; ++i) {
    std::string event;
    if (!(input >> std::quoted(event)) || event.empty() || event.size() > 128) {
      return false;
    }
    recentEvents.push_back(event);
  }
  input >> std::ws;
  return input.eof();
}

bool readCheckpointDescriptor(const char *value) {
  int fd = -1;
  const std::string token(value);
  const auto parsed = std::from_chars(token.data(), token.data() + token.size(), fd);
  struct stat info {};
  if (parsed.ec != std::errc {} || parsed.ptr != token.data() + token.size() || fd < 3
      || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 0
      || info.st_uid != getuid() || info.st_size <= 0
      || static_cast<uint64_t>(info.st_size) > MAX_CHECKPOINT_BYTES) {
    return false;
  }
  std::string payload(static_cast<size_t>(info.st_size), '\0');
  size_t offset = 0;
  while (offset < payload.size()) {
    const ssize_t count = read(fd, payload.data() + offset, payload.size() - offset);
    if (count <= 0) {
      close(fd);
      return false;
    }
    offset += static_cast<size_t>(count);
  }
  const bool closed = close(fd) == 0;
  return closed && restoreCheckpoint(payload);
}

}  // namespace

void fuzzConfigure(uint64_t s, uint32_t steps, bool v) {
  active = true;
  seed = s;
  verbose = v;
  maxSteps = steps;
  machine = std::make_unique<FuzzMachine>(steps);
  pendingEvent = Event::SELECT;
  pendingDescription.clear();
  pendingWasStop = false;
  stateBeforeApply = ObservableState {};
  escapeActions = 0;
  rng.seed(s);
  recentEvents.clear();
  findingCount = 0;
  previousLiveness = 0;
  resumedBoots = 0;
  classCounts.clear();
  eventCounts.clear();
  pageCounts.clear();
  if (const char *checkpoint = std::getenv(CHECKPOINT_FD_ENV); checkpoint != nullptr) {
    if (!readCheckpointDescriptor(checkpoint) || unsetenv(CHECKPOINT_FD_ENV) != 0) {
      std::cerr << "fuzz restart: invalid or unreadable continuation\n";
      std::exit(1);
    }
    std::cout << "FUZZ RESUME seed=" << seed << " boot=" << resumedBoots
              << " attempted=" << machine->attempted()
              << " interrupted_by_restart=" << machine->interruptedByRestart() << '\n';
  } else {
    std::cout << "FUZZ START seed=" << seed << " steps=" << maxSteps << '\n';
  }
}

bool fuzzResumedBoot(void) {
  return resumedBoots != 0;
}

bool fuzzSaveRestart(void) {
  if (!active || machine == nullptr || !machine->interruptForRestart()
      || resumedBoots >= machine->attempted()) {
    return false;
  }
  std::ostringstream output;
  output << "FURBLE_FUZZ_RESTART_1 " << seed << ' ' << maxSteps << ' ' << resumedBoots + 1 << ' '
         << previousLiveness + livenessViolationCount() << ' ' << findingCount << '\n';
  auto state = machine->checkpoint();
  for (const auto *field : checkpointFields(state)) {
    output << *field << ' ';
  }
  output << '\n' << rng << '\n';
  writeCounts(output, classCounts);
  writeCounts(output, eventCounts);
  writeCounts(output, pageCounts);
  output << recentEvents.size() << '\n';
  for (const auto &event : recentEvents) {
    output << std::quoted(event) << '\n';
  }
  const std::string payload = output.str();
  if (!output || payload.size() > MAX_CHECKPOINT_BYTES) {
    return false;
  }
  // tmpfile is private and already unlinked. Inherit only its descriptor, so
  // neither successful re-exec nor a failed boot leaves checkpoint caches.
  FILE *checkpoint = std::tmpfile();
  if (checkpoint == nullptr) {
    return false;
  }
  const int fd = fileno(checkpoint);
  const int flags = fcntl(fd, F_GETFD);
  if (std::fwrite(payload.data(), 1, payload.size(), checkpoint) != payload.size()
      || std::fflush(checkpoint) != 0 || std::fseek(checkpoint, 0, SEEK_SET) != 0 || flags < 0
      || fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC) != 0
      || setenv(CHECKPOINT_FD_ENV, std::to_string(fd).c_str(), 1) != 0) {
    std::fclose(checkpoint);
    return false;
  }
  // The descriptor must remain open until the immediately following execvp.
  return true;
}

bool fuzzActive(void) {
  return active;
}

bool fuzzCheckpointEligible(void) {
  return active && checkpointEligible && machine != nullptr;
}

bool fuzzWriteCheckpoint(std::ostream &output) {
  if (!fuzzCheckpointEligible()) {
    return false;
  }
  const auto state = machine->checkpoint();
  output << "FURBLE_FUZZ_CHECKPOINT 1\n"
         << seed << ' ' << maxSteps << ' ' << verbose << ' ' << static_cast<uint32_t>(pendingEvent)
         << ' ' << pendingWasStop << ' ' << escapeActions << '\n';
  output << state.phase << ' ' << state.settleNext << ' ' << state.maxSteps << ' '
         << state.escapeCadence << ' ' << state.stepCount << ' ' << state.settleRemaining << ' '
         << state.attempted << ' ' << state.observedDelta << ' ' << state.noObservedDelta << ' '
         << state.settled << ' ' << state.timerStopChecks << ' ' << state.finishing << '\n';
  output << std::quoted(pendingDescription) << '\n' << rng << '\n';
  for (const auto &value : stateBeforeApply.values) {
    output << std::quoted(value) << '\n';
  }
  output << recentEvents.size() << '\n';
  for (const auto &event : recentEvents) {
    output << std::quoted(event) << '\n';
  }
  output << findings.size() << '\n';
  for (const auto &finding : findings) {
    output << finding.step << ' ' << std::quoted(finding.bug_class) << ' '
           << std::quoted(finding.page) << ' ' << std::quoted(finding.event) << ' '
           << std::quoted(finding.detail) << '\n';
  }
  auto writeMap = [&output](const auto &map) {
    output << map.size() << '\n';
    for (const auto &entry : map) {
      output << std::quoted(entry.first) << ' ' << entry.second << '\n';
    }
  };
  writeMap(classCounts);
  writeMap(eventCounts);
  writeMap(pageCounts);
  return output.good();
}

bool fuzzReadCheckpoint(std::istream &input) {
  constexpr std::streamsize kMaxCheckpointBytes = 4 * 1024 * 1024;
  if (input.rdbuf()->in_avail() > kMaxCheckpointBytes)
    return false;
  std::string magic;
  unsigned version = 0;
  uint32_t event = 0;
  uint64_t savedSeed = 0;
  uint32_t savedSteps = 0;
  bool savedVerbose = false;
  bool savedStop = false;
  uint32_t savedEscapes = 0;
  if (!(input >> magic >> version) || magic != "FURBLE_FUZZ_CHECKPOINT" || version != 1
      || !(input >> savedSeed >> savedSteps >> savedVerbose >> event >> savedStop >> savedEscapes)
      || savedSeed != seed || savedSteps != maxSteps) {
    return false;
  }
  FuzzMachine::Checkpoint state {};
  if (!(input >> state.phase >> state.settleNext >> state.maxSteps >> state.escapeCadence
        >> state.stepCount >> state.settleRemaining >> state.attempted >> state.observedDelta
        >> state.noObservedDelta >> state.settled >> state.timerStopChecks >> state.finishing)
      || maxSteps == 0 || maxSteps > 10000000 || event >= static_cast<uint32_t>(Event::COUNT)
      || state.maxSteps != maxSteps || state.stepCount > maxSteps || state.phase > 5
      || state.settleNext > 5 || !machine->restore(state)) {
    return false;
  }
  std::string savedDescription;
  if (!(input >> std::quoted(savedDescription) >> rng) || savedDescription.size() > 4096) {
    return false;
  }
  std::array<std::string, kObservableQueries.size()> savedBefore;
  for (auto &value : savedBefore) {
    if (!(input >> std::quoted(value)) || value.size() > 4096)
      return false;
  }
  pendingEvent = static_cast<Event>(event);
  size_t count = 0;
  if (!(input >> count) || count > 10000) {
    return false;
  }
  recentEvents.clear();
  for (size_t i = 0; i < count; i++) {
    std::string value;
    if (!(input >> std::quoted(value)) || value.size() > 4096)
      return false;
    recentEvents.push_back(std::move(value));
  }
  if (!(input >> count) || count > 100000)
    return false;
  findings.clear();
  for (size_t i = 0; i < count; i++) {
    Finding finding;
    if (!(input >> finding.step >> std::quoted(finding.bug_class) >> std::quoted(finding.page)
          >> std::quoted(finding.event) >> std::quoted(finding.detail))
        || finding.bug_class.size() > 4096 || finding.page.size() > 4096
        || finding.event.size() > 4096 || finding.detail.size() > 4096)
      return false;
    findings.push_back(std::move(finding));
  }
  auto readMap = [&input](auto &map) {
    size_t size = 0;
    if (!(input >> size) || size > 100000)
      return false;
    for (size_t i = 0; i < size; i++) {
      std::string key;
      uint32_t value = 0;
      if (!(input >> std::quoted(key) >> value) || key.size() > 4096
          || !map.emplace(std::move(key), value).second)
        return false;
    }
    return true;
  };
  classCounts.clear();
  eventCounts.clear();
  pageCounts.clear();
  if (!readMap(classCounts) || !readMap(eventCounts) || !readMap(pageCounts))
    return false;
  char trailing = 0;
  if (input >> trailing)
    return false;
  verbose = savedVerbose;
  pendingWasStop = savedStop;
  escapeActions = savedEscapes;
  pendingDescription = std::move(savedDescription);
  stateBeforeApply.values = std::move(savedBefore);
  checkpointEligible = false;
  return true;
}

void fuzzResumeAfterRestart(void) {
  checkpointEligible = false;
}

void fuzzTick(UI *ui) {
  if (!active || ui == nullptr) {
    return;
  }

  switch (machine->phase()) {
    case FuzzPhase::APPLY:
      if (!machine->beginApply()) {
        return;
      }
      checkpointEligible = false;
      pendingEvent = pickEvent();
      pendingDescription = eventName(pendingEvent);
      pendingWasStop = pendingEvent == Event::INTERVAL_STOP;
      stateBeforeApply = captureState(ui);
      applyEvent(ui, pendingEvent);
      recordEvent(pendingDescription);
      eventCounts[pendingDescription]++;
      checkpointEligible = true;
      // Let the UI and its scheduler-visible workers finish a complete
      // handoff before checking the resulting page. Two cycles were enough
      // for most events but allowed a host-thread wakeup to land one cycle
      // late, which made page coverage depend on wall-clock scheduling. Keep
      // the randomized tail so the fuzzer still exercises different settling
      // windows, but make the minimum deterministic and long enough for the
      // slowest production path.
      machine->eventApplied(6 + pick(5));
      return;

    case FuzzPhase::SETTLE:
      // The post-handler hook advances this phase. Do not count a driver-only
      // tick as an LVGL cycle.
      return;

    case FuzzPhase::CHECK:
    {
      checkInvariants(ui, pendingDescription);
      const bool observedDelta = !(captureState(ui) == stateBeforeApply);

      if (pendingWasStop) {
        const std::string state = ui->simQueryState("interval_state");
        if (state != "idle" && state != "finished" && state != "unknown") {
          recordFinding(ui, "timer-leak", pendingDescription,
                        "interval_state=" + state + " after stop");
        }
      }
      machine->checkComplete(observedDelta, pendingWasStop);

      return;
    }

    case FuzzPhase::ESCAPE:
      escapeTick(ui);
      return;

    case FuzzPhase::FINISH:
      finish();
      return;
  }
}

void fuzzCycleComplete(UI *ui) {
  if (!active || ui == nullptr || machine == nullptr) {
    return;
  }
  machine->lvglCycleComplete();
}

}  // namespace Furble::Sim
