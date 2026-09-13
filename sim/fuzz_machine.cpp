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

bool FuzzMachine::beginApply() {
  if (phase_ != FuzzPhase::APPLY) {
    return false;
  }
  if (stepCount_ >= maxSteps_) {
    finishing_ = true;
    phase_ = FuzzPhase::ESCAPE;
    return false;
  }
  return true;
}

void FuzzMachine::eventApplied(uint32_t settleCycles) {
  if (phase_ != FuzzPhase::APPLY) {
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

  if (stepCount_ >= maxSteps_) {
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

FuzzMachine::Checkpoint FuzzMachine::checkpoint() const {
  return {static_cast<uint32_t>(phase_),
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
          finishing_};
}

bool FuzzMachine::restore(const Checkpoint &checkpoint) {
  if (checkpoint.phase > static_cast<uint32_t>(FuzzPhase::FINISH) ||
      checkpoint.settleNext > static_cast<uint32_t>(FuzzPhase::FINISH) ||
      checkpoint.maxSteps == 0) {
    return false;
  }
  phase_ = static_cast<FuzzPhase>(checkpoint.phase);
  settleNext_ = static_cast<FuzzPhase>(checkpoint.settleNext);
  maxSteps_ = checkpoint.maxSteps;
  escapeCadence_ = checkpoint.escapeCadence;
  stepCount_ = checkpoint.stepCount;
  settleRemaining_ = checkpoint.settleRemaining;
  attempted_ = checkpoint.attempted;
  observedDelta_ = checkpoint.observedDelta;
  noObservedDelta_ = checkpoint.noObservedDelta;
  settled_ = checkpoint.settled;
  timerStopChecks_ = checkpoint.timerStopChecks;
  finishing_ = checkpoint.finishing;
  return stepCount_ <= maxSteps_ && settled_ == stepCount_ &&
         observedDelta_ + noObservedDelta_ == stepCount_ &&
         attempted_ == stepCount_ + (phase_ == FuzzPhase::SETTLE || phase_ == FuzzPhase::CHECK) &&
         timerStopChecks_ <= stepCount_ && finishing_ == (stepCount_ >= maxSteps_);
}

void FuzzMachine::resumeAfterRestart() {
  // A restart may occur while the applied event is still settling. Preserve
  // that phase and remaining budget so the resumed process completes the same
  // check exactly once instead of issuing an extra event.
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
