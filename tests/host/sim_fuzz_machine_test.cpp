#include <cstdint>
#include <iostream>
#include <random>

#include "fuzz_machine.h"

namespace {

int failures = 0;

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

void applyAndCheck(Furble::Sim::FuzzMachine &machine,
                   uint32_t settleCycles,
                   bool observedDelta,
                   bool timerStopCheck = false) {
  require(machine.beginApply(), "event begins in Apply");
  machine.eventApplied(settleCycles);
  for (uint32_t cycle = 0; cycle < settleCycles; cycle++) {
    require(machine.phase() == Furble::Sim::FuzzPhase::SETTLE,
            "settle remains active before the final cycle");
    machine.lvglCycleComplete();
  }
  require(machine.phase() == Furble::Sim::FuzzPhase::CHECK,
          "the final completed cycle enters Check");
  machine.checkComplete(observedDelta, timerStopCheck);
}

void testCadenceAndSettle() {
  Furble::Sim::FuzzMachine machine(41, 40);
  for (uint32_t step = 0; step < 39; step++) {
    applyAndCheck(machine, 1, true);
    require(machine.phase() == Furble::Sim::FuzzPhase::APPLY,
            "cadence stays in Apply before budget 40");
  }
  applyAndCheck(machine, 1, true);
  require(machine.phase() == Furble::Sim::FuzzPhase::ESCAPE,
          "budget 40 enters Escape exactly once");
  machine.escapeChecked(true);
  require(machine.phase() == Furble::Sim::FuzzPhase::APPLY,
          "a non-final cadence escape returns to Apply");

  Furble::Sim::FuzzMachine settleMachine(1);
  require(settleMachine.beginApply(), "settle test event begins");
  settleMachine.eventApplied(3);
  settleMachine.lvglCycleComplete();
  require(settleMachine.phase() == Furble::Sim::FuzzPhase::SETTLE,
          "one cycle is not enough for N=3");
  settleMachine.lvglCycleComplete();
  require(settleMachine.phase() == Furble::Sim::FuzzPhase::SETTLE,
          "two cycles are not enough for N=3");
  settleMachine.lvglCycleComplete();
  require(settleMachine.phase() == Furble::Sim::FuzzPhase::CHECK, "exactly N cycles enter Check");
}

void testEscapeTraces() {
  Furble::Sim::FuzzMachine modal(1);
  applyAndCheck(modal, 1, true);
  require(modal.phase() == Furble::Sim::FuzzPhase::ESCAPE, "final event enters Escape");
  require(modal.finishing(), "final event marks the escape as finishing");
  modal.escapeModalRejected(1);
  require(modal.phase() == Furble::Sim::FuzzPhase::SETTLE,
          "modal rejection settles before Escape resumes");
  modal.lvglCycleComplete();
  require(modal.phase() == Furble::Sim::FuzzPhase::ESCAPE,
          "modal rejection resumes Escape after settling");
  modal.escapeChecked(false);
  require(modal.phase() == Furble::Sim::FuzzPhase::ESCAPE,
          "an unsuccessful escape check remains resumable");
  modal.escapeChecked(true);
  require(modal.phase() == Furble::Sim::FuzzPhase::FINISH,
          "Finish follows the settled modal escape check");

  Furble::Sim::FuzzMachine back(1);
  applyAndCheck(back, 1, true);
  back.escapeBackIssued(1);
  require(back.phase() == Furble::Sim::FuzzPhase::SETTLE, "one back action starts settling");
  back.lvglCycleComplete();
  require(back.phase() == Furble::Sim::FuzzPhase::ESCAPE, "back escape resumes after settling");
  back.escapeChecked(false);
  back.escapeBackIssued(1);
  back.lvglCycleComplete();
  require(back.phase() == Furble::Sim::FuzzPhase::ESCAPE,
          "the final back action also settles before checking");
  back.escapeChecked(true);
  require(back.phase() == Furble::Sim::FuzzPhase::FINISH, "final Escape check permits Finish");
}

void testCounters() {
  Furble::Sim::FuzzMachine machine(3);
  applyAndCheck(machine, 0, false, true);
  applyAndCheck(machine, 1, true, false);
  applyAndCheck(machine, 2, false, true);
  require(machine.attempted() == 3, "attempted count matches applied events");
  require(machine.settled() == 3, "settled count matches checked events");
  require(machine.attempted() == machine.observedDelta() + machine.noObservedDelta(),
          "delta counters sum to attempted events");
  require(machine.observedDelta() == 1, "observed-delta count is truthful");
  require(machine.noObservedDelta() == 2, "no-observed-delta count is truthful");
  require(machine.timerStopChecks() == 2, "timer-stop count records stop checks");
}

void testRandomVector() {
  std::mt19937_64 rng(123);
  const uint32_t expected[] = {4, 1, 1, 0};
  for (uint32_t value : expected) {
    require(Furble::Sim::fuzzBoundedRandom(rng, 10) == value, "bounded raw RNG vector is stable");
  }
}

void requireStateEqual(const Furble::Sim::FuzzMachine::State &left,
                       const Furble::Sim::FuzzMachine::State &right,
                       const char *message) {
  require(left.phase == right.phase && left.settleNext == right.settleNext &&
              left.maxSteps == right.maxSteps &&
              left.escapeCadence == right.escapeCadence &&
              left.stepCount == right.stepCount &&
              left.settleRemaining == right.settleRemaining &&
              left.attempted == right.attempted &&
              left.observedDelta == right.observedDelta &&
              left.noObservedDelta == right.noObservedDelta &&
              left.settled == right.settled &&
              left.timerStopChecks == right.timerStopChecks &&
              left.finishing == right.finishing &&
              left.interruptedByRestart == right.interruptedByRestart &&
              left.applyStarted == right.applyStarted,
          message);
}

void testCheckpointRoundTrip() {
  Furble::Sim::FuzzMachine machine(3, 7);
  applyAndCheck(machine, 0, true, true);
  const auto saved = machine.checkpoint();

  Furble::Sim::FuzzMachine restored(99, 1);
  require(restored.restore(saved), "canonical checkpoint restores");
  requireStateEqual(saved, restored.checkpoint(), "checkpoint round-trips exactly");
  require(restored.beginApply(), "restored machine resumes Apply");
  restored.eventApplied(0);
  restored.checkComplete(false, false);
  require(restored.settled() == 2, "restored machine continues settled events");
}

void testCheckpointValidation() {
  Furble::Sim::FuzzMachine machine(3);
  const auto saved = machine.checkpoint();

  auto invalid = saved;
  invalid.phase = static_cast<uint32_t>(Furble::Sim::FuzzPhase::SETTLE);
  require(!machine.restore(invalid), "restore rejects stale settle phase");
  requireStateEqual(saved, machine.checkpoint(), "invalid restore leaves state unchanged");

  invalid = saved;
  invalid.attempted = 1;
  invalid.interruptedByRestart = 1;
  require(!machine.restore(invalid), "restore rejects inconsistent attempt counters");
}

void testInterruptedRestart() {
  Furble::Sim::FuzzMachine beforeApply(3);
  require(beforeApply.beginApply(), "an event can begin before interruption");
  require(beforeApply.interruptForRestart(), "restart interrupts dispatched Apply");
  require(beforeApply.attempted() == 1, "interruption consumes one attempt");
  require(beforeApply.interruptedByRestart() == 1, "interruption count is recorded");
  require(beforeApply.settled() == 0 && beforeApply.observedDelta() == 0 &&
              beforeApply.noObservedDelta() == 0,
          "interruption does not record settled observations");
  require(beforeApply.phase() == Furble::Sim::FuzzPhase::APPLY,
          "non-final interruption resumes fresh Apply");
  require(!beforeApply.interruptForRestart(), "the same interruption cannot be consumed twice");

  Furble::Sim::FuzzMachine duringSettle(1);
  require(duringSettle.beginApply(), "settle interruption event begins");
  duringSettle.eventApplied(2);
  require(duringSettle.interruptForRestart(), "restart interrupts an in-flight settle");
  require(duringSettle.attempted() == 1 && duringSettle.interruptedByRestart() == 1,
          "settle interruption does not double-count the attempt");
  require(duringSettle.settled() == 0, "settle interruption remains unsettled");
  require(duringSettle.phase() == Furble::Sim::FuzzPhase::ESCAPE &&
              duringSettle.finishing(),
          "final interruption enters finishing Escape");
}

}  // namespace

int main() {
  testCadenceAndSettle();
  testEscapeTraces();
  testCounters();
  testRandomVector();
  testCheckpointRoundTrip();
  testCheckpointValidation();
  testInterruptedRestart();
  if (failures != 0) {
    return 1;
  }
  std::cout << "sim fuzz machine tests passed\n";
  return 0;
}
