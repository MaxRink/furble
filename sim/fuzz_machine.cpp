#include "fuzz_machine.h"

#include <cstdlib>

namespace Furble::Sim {

FuzzMachine::FuzzMachine(uint32_t maxSteps, uint32_t escapeCadence)
    : phase_(FuzzPhase::APPLY),
      settleNext_(FuzzPhase::APPLY),
      maxSteps_(maxSteps),
      escapeCadence_(escapeCadence) {}

FuzzPhase FuzzMachine::phase() const {
  return phase_;
}

uint32_t FuzzMachine::stepCount() const {
  return stepCount_;
}

uint32_t FuzzMachine::attempted() const {
  return attempted_;
}

uint32_t FuzzMachine::observedDelta() const {
  return observedDelta_;
}

uint32_t FuzzMachine::noObservedDelta() const {
  return noObservedDelta_;
}

uint32_t FuzzMachine::settled() const {
  return settled_;
}

uint32_t FuzzMachine::timerStopChecks() const {
  return timerStopChecks_;
}

uint32_t FuzzMachine::interruptedByRestart() const {
  return interruptedByRestart_;
}

FuzzMachine::State FuzzMachine::checkpoint() const {
  return State {static_cast<uint32_t>(phase_),
                static_cast<uint32_t>(settleNext_),
                maxSteps_,
                escapeCadence_,
                stepCount_,
                settleRemaining_,
                attempted_,
                observedDelta_,
                noObservedDelta_,
                settled_,
                timerStopChecks_,
                finishing_ ? 1U : 0U,
                interruptedByRestart_,
                applyStarted_ ? 1U : 0U};
}

bool FuzzMachine::restore(const State &state) {
  const auto validPhase = [](uint32_t value) {
    return value <= static_cast<uint32_t>(FuzzPhase::FINISH);
  };
  if (!validPhase(state.phase) || !validPhase(state.settleNext) ||
      state.finishing > 1 || state.applyStarted > 1 || state.settleRemaining != 0 ||
      state.applyStarted != 0 || state.settled != state.stepCount ||
      state.stepCount > state.maxSteps || state.attempted > state.maxSteps ||
      state.interruptedByRestart > state.attempted ||
      state.observedDelta > state.settled || state.noObservedDelta > state.settled ||
      static_cast<uint64_t>(state.observedDelta) + state.noObservedDelta !=
          state.settled ||
      state.timerStopChecks > state.settled) {
    return false;
  }

  const FuzzPhase phase = static_cast<FuzzPhase>(state.phase);
  if (phase != FuzzPhase::APPLY && phase != FuzzPhase::ESCAPE) {
    return false;
  }
  if (static_cast<uint64_t>(state.settled) + state.interruptedByRestart !=
      state.attempted) {
    return false;
  }
  if ((phase == FuzzPhase::APPLY && state.finishing != 0) ||
      (phase == FuzzPhase::ESCAPE && state.finishing == 0 &&
       state.attempted >= state.maxSteps) ||
      (state.finishing != 0 &&
       (phase != FuzzPhase::ESCAPE || state.attempted != state.maxSteps))) {
    return false;
  }

  phase_ = phase;
  settleNext_ = static_cast<FuzzPhase>(state.settleNext);
  maxSteps_ = state.maxSteps;
  escapeCadence_ = state.escapeCadence;
  stepCount_ = state.stepCount;
  settleRemaining_ = state.settleRemaining;
  attempted_ = state.attempted;
  observedDelta_ = state.observedDelta;
  noObservedDelta_ = state.noObservedDelta;
  settled_ = state.settled;
  timerStopChecks_ = state.timerStopChecks;
  interruptedByRestart_ = state.interruptedByRestart;
  finishing_ = state.finishing != 0;
  applyStarted_ = false;
  return true;
}

bool FuzzMachine::interruptForRestart() {
  if (!applyStarted_ ||
      (phase_ != FuzzPhase::APPLY && phase_ != FuzzPhase::SETTLE &&
       phase_ != FuzzPhase::CHECK)) {
    return false;
  }

  // eventApplied() already counted an attempt before SETTLE or CHECK. An
  // interruption from APPLY means the UI action was dispatched but never
  // reached that accounting boundary, so consume it here instead.
  if (phase_ == FuzzPhase::APPLY) {
    attempted_++;
  }
  interruptedByRestart_++;
  applyStarted_ = false;
  settleRemaining_ = 0;
  settleNext_ = FuzzPhase::APPLY;
  if (attempted_ >= maxSteps_) {
    finishing_ = true;
    phase_ = FuzzPhase::ESCAPE;
  } else {
    finishing_ = false;
    phase_ = FuzzPhase::APPLY;
  }
  return true;
}

bool FuzzMachine::beginApply() {
  if (phase_ != FuzzPhase::APPLY || applyStarted_) {
    return false;
  }
  if (stepCount_ >= maxSteps_ || attempted_ >= maxSteps_) {
    finishing_ = true;
    phase_ = FuzzPhase::ESCAPE;
    return false;
  }
  applyStarted_ = true;
  return true;
}

void FuzzMachine::eventApplied(uint32_t settleCycles) {
  if (phase_ != FuzzPhase::APPLY || !applyStarted_) {
    return;
  }
  attempted_++;
  settleThen(FuzzPhase::CHECK, settleCycles);
}

void FuzzMachine::lvglCycleComplete() {
  if (phase_ != FuzzPhase::SETTLE || settleRemaining_ == 0) {
    return;
  }
  settleRemaining_--;
  if (settleRemaining_ == 0) {
    phase_ = settleNext_;
  }
}

void FuzzMachine::checkComplete(bool observedDelta, bool timerStopCheck) {
  if (phase_ != FuzzPhase::CHECK) {
    return;
  }
  applyStarted_ = false;
  settled_++;
  if (observedDelta) {
    observedDelta_++;
  } else {
    noObservedDelta_++;
  }
  if (timerStopCheck) {
    timerStopChecks_++;
  }
  stepCount_++;

  if (attempted_ >= maxSteps_) {
    finishing_ = true;
    phase_ = FuzzPhase::ESCAPE;
  } else if (escapeCadence_ != 0 && stepCount_ % escapeCadence_ == 0) {
    phase_ = FuzzPhase::ESCAPE;
  } else {
    phase_ = FuzzPhase::APPLY;
  }
}

void FuzzMachine::escapeModalRejected(uint32_t settleCycles) {
  if (phase_ == FuzzPhase::ESCAPE) {
    settleThen(FuzzPhase::ESCAPE, settleCycles);
  }
}

void FuzzMachine::escapeBackIssued(uint32_t settleCycles) {
  if (phase_ == FuzzPhase::ESCAPE) {
    // Even a back action that reaches main must settle before the next Escape
    // check can allow Finish. This keeps teardown after a real UI cycle.
    settleThen(FuzzPhase::ESCAPE, settleCycles);
  }
}

void FuzzMachine::escapeChecked(bool reachedMain) {
  if (phase_ == FuzzPhase::ESCAPE && reachedMain) {
    phase_ = finishing_ ? FuzzPhase::FINISH : FuzzPhase::APPLY;
  }
}

bool FuzzMachine::finishing() const {
  return finishing_;
}

void FuzzMachine::settleThen(FuzzPhase next, uint32_t settleCycles) {
  settleNext_ = next;
  settleRemaining_ = settleCycles;
  phase_ = settleCycles == 0 ? next : FuzzPhase::SETTLE;
}

uint32_t fuzzBoundedRandom(std::mt19937_64 &rng, uint32_t bound) {
  if (bound == 0) {
    std::abort();
  }
  const uint64_t threshold = (0ULL - static_cast<uint64_t>(bound)) % bound;
  uint64_t raw;
  do {
    raw = rng();
  } while (raw < threshold);
  return static_cast<uint32_t>(raw % bound);
}

}  // namespace Furble::Sim
