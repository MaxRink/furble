#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

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

struct Finding {
  std::string bug_class;
  std::string page;
  std::string event;
  std::string detail;
  uint32_t step;
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
ObservableState stateBeforeApply;
uint32_t escapeActions = 0;
std::mt19937_64 rng;
std::deque<std::string> recentEvents;
std::vector<Finding> findings;
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
  Finding finding;
  finding.bug_class = bug_class;
  finding.page = ui->simQueryState("page");
  finding.event = event;
  finding.detail = detail;
  finding.step = machine->stepCount();
  findings.push_back(finding);
  classCounts[bug_class]++;

  std::cout << "FUZZ FINDING [" << bug_class << "] step=" << machine->stepCount()
            << " page=" << finding.page << " event=" << event << " detail=" << detail << '\n';
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
  if (mustFit(page) && ui->simQueryState("overflow") == "yes") {
    recordFinding(ui, "layout-overflow", event, "compact page overflows the panel");
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
  std::cout << "FUZZ SUMMARY seed=" << seed << " steps=" << machine->stepCount()
            << " attempted=" << machine->attempted()
            << " observed_delta=" << machine->observedDelta()
            << " no_observed_delta=" << machine->noObservedDelta()
            << " settled=" << machine->settled()
            << " timer_stop_checks=" << machine->timerStopChecks()
            << " liveness=" << livenessViolationCount() << " findings=" << findings.size() << '\n';
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
  requestExit(findings.empty() ? 0 : 1);
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
  findings.clear();
  classCounts.clear();
  eventCounts.clear();
  pageCounts.clear();
  std::cout << "FUZZ START seed=" << seed << " steps=" << maxSteps << '\n';
}

bool fuzzActive(void) {
  return active;
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
      pendingEvent = pickEvent();
      pendingDescription = eventName(pendingEvent);
      pendingWasStop = pendingEvent == Event::INTERVAL_STOP;
      stateBeforeApply = captureState(ui);
      applyEvent(ui, pendingEvent);
      recordEvent(pendingDescription);
      eventCounts[pendingDescription]++;
      // The post-handler hook counts the current LVGL cycle and the next
      // settle cycles before Check reads any state.
      machine->eventApplied(2 + pick(5));
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
