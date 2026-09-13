#ifndef FURBLE_SIM_FUZZ_MACHINE_H
#define FURBLE_SIM_FUZZ_MACHINE_H

#include <cstdint>
#include <random>

namespace Furble::Sim {

enum class FuzzPhase {
  APPLY,
  SETTLE,
  CHECK,
  ESCAPE,
  FINISH,
};

// Dependency-free scheduler model for the simulator fuzzer. The UI adapter
// calls lvglCycleComplete() after the real lv_task_handler() returns, so a
// settle budget counts completed LVGL cycles rather than driverTick calls.
class FuzzMachine {
 public:
  // A restart checkpoint contains only machine-owned state. LVGL settle and
  // check phases are intentionally not restartable because their UI work is
  // not replayed after a process restart.
  struct State {
    uint32_t phase = 0;
    uint32_t settleNext = 0;
    uint32_t maxSteps = 0;
    uint32_t escapeCadence = 0;
    uint32_t stepCount = 0;
    uint32_t settleRemaining = 0;
    uint32_t attempted = 0;
    uint32_t observedDelta = 0;
    uint32_t noObservedDelta = 0;
    uint32_t settled = 0;
    uint32_t timerStopChecks = 0;
    uint32_t finishing = 0;
    uint32_t interruptedByRestart = 0;
    uint32_t applyStarted = 0;
  };

  explicit FuzzMachine(uint32_t maxSteps, uint32_t escapeCadence = 40);

  FuzzPhase phase() const;
  uint32_t stepCount() const;
  uint32_t attempted() const;
  uint32_t observedDelta() const;
  uint32_t noObservedDelta() const;
  uint32_t settled() const;
  uint32_t timerStopChecks() const;
  uint32_t interruptedByRestart() const;

  State checkpoint() const;

  // Restore only a canonical post-restart state. In-flight LVGL settle and
  // check phases must be discarded rather than replayed.
  bool restore(const State &state);

  // Interrupt the current event at the simulator restart boundary. The
  // attempt is counted once, but no settled or observed counter is advanced.
  bool interruptForRestart();

  // Start an event from Apply. Returns false when the event budget is spent
  // and transitions to the final Escape phase.
  bool beginApply();

  // Record one attempted event and wait for exactly settleCycles completed
  // LVGL cycles before entering Check.
  void eventApplied(uint32_t settleCycles);

  // Called after one actual lv_task_handler() completion.
  void lvglCycleComplete();

  // Complete the post-settle invariant read for the pending event.
  void checkComplete(bool observedDelta, bool timerStopCheck);

  // Escape is intentionally resumable. Each reject or back action is followed
  // by settling, then the adapter returns to Escape to inspect the next state.
  void escapeModalRejected(uint32_t settleCycles = 1);
  void escapeBackIssued(uint32_t settleCycles = 1);
  void escapeChecked(bool reachedMain);

  bool finishing() const;

 private:
  void settleThen(FuzzPhase next, uint32_t settleCycles);

  FuzzPhase phase_;
  FuzzPhase settleNext_;
  uint32_t maxSteps_;
  uint32_t escapeCadence_;
  uint32_t stepCount_ = 0;
  uint32_t settleRemaining_ = 0;
  uint32_t attempted_ = 0;
  uint32_t observedDelta_ = 0;
  uint32_t noObservedDelta_ = 0;
  uint32_t settled_ = 0;
  uint32_t timerStopChecks_ = 0;
  uint32_t interruptedByRestart_ = 0;
  bool finishing_ = false;
  bool applyStarted_ = false;
};

// Stable bounded sampling independent of the standard library distribution
// implementation. It rejects the short raw prefix that would bias modulo.
uint32_t fuzzBoundedRandom(std::mt19937_64 &rng, uint32_t bound);

}  // namespace Furble::Sim

#endif
