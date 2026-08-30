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
  explicit FuzzMachine(uint32_t maxSteps, uint32_t escapeCadence = 40);

  FuzzPhase phase() const;
  uint32_t stepCount() const;
  uint32_t attempted() const;
  uint32_t observedDelta() const;
  uint32_t noObservedDelta() const;
  uint32_t settled() const;
  uint32_t timerStopChecks() const;

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
  bool finishing_ = false;
};

// Stable bounded sampling independent of the standard library distribution
// implementation. It rejects the short raw prefix that would bias modulo.
uint32_t fuzzBoundedRandom(std::mt19937_64 &rng, uint32_t bound);

}  // namespace Furble::Sim

#endif
