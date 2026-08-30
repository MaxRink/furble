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
