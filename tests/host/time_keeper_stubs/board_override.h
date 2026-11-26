#ifndef FURBLE_HOST_TIME_KEEPER_BOARD_OVERRIDE_H
#define FURBLE_HOST_TIME_KEEPER_BOARD_OVERRIDE_H

/*
 * Host stub for the simulator's scenario-selected board identity.
 *
 * time_keeper_persistence_test compiles `src/FurbleBoard.cpp` with FURBLE_SIM
 * so the simulator branch of the shared sources is the one under test, but it
 * does not link the simulator and has no scenario to seed. This header stands
 * in for `sim/board_override.h` and reports "no override", so
 * `Board::profile()` takes the unchanged `M5.getBoard()` path against this
 * directory's M5Unified stub, which is the path the test asserts.
 *
 * Keep the two declarations identical to `sim/board_override.h`: a signature
 * change there has to change here or the target stops compiling.
 */

namespace Furble::Sim {

inline bool boardOverrideSet(void) {
  return false;
}

inline int boardOverrideId(void) {
  return -1;
}

}  // namespace Furble::Sim

#endif
