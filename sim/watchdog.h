#ifndef FURBLE_SIM_WATCHDOG_H
#define FURBLE_SIM_WATCHDOG_H

namespace Furble::Sim {

/**
 * Host wall-clock stall watchdog.
 *
 * Every other bound in the simulator is denominated in virtual time, so none
 * of them can see a stall that stops virtual time from advancing at all. A
 * boot-order deadlock between the SDL render pump and the simulator thread did
 * exactly that and spun for hours at full CPU with no output. This watchdog is
 * the one bound measured against the host clock, and a trip is always a
 * failure: it prints the phase, the virtual clock, the scheduler task table
 * and a native backtrace of every registered thread, then exits non zero.
 */

/**
 * Register the calling thread with the watchdog under a stable name. A
 * registered thread appears in the stall report with its own backtrace.
 */
void watchdogRegisterThread(const char *name);

/** Drop the calling thread from the watchdog registry before it exits. */
void watchdogUnregisterThread(void);

/**
 * Start the watchdog. The bound is FURBLE_SIM_WATCHDOG_SECONDS host seconds,
 * default 120, and 0 disables the watchdog for an interactive debugging
 * session. Progress is the virtual clock plus the scheduler progress counter
 * plus the recorded phase, so a run that is merely slow keeps resetting it.
 */
void watchdogStart(void);

/** Record the boot or run phase the simulator has reached. */
void watchdogPhase(const char *phase);

/** Stop and join the watchdog thread. */
void watchdogStop(void);

}  // namespace Furble::Sim

#endif
