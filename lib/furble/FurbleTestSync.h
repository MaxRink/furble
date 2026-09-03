#ifndef FURBLE_TEST_SYNC_H
#define FURBLE_TEST_SYNC_H

/**
 * Test-only named synchronisation points for the control state machine.
 *
 * The host harness cannot otherwise force a preemption inside a sub-50 ms
 * window, so races like the late DISCONNECTING republish were unforceable and
 * their guards were not regression-testable. A sync point marks one such
 * window by name. A host test installs a barrier or callback on the name and
 * can then park the arriving task exactly inside the window while another
 * thread completes the racing operation.
 *
 * Firmware builds never define FURBLE_TEST_SYNC, so the macro expands to
 * nothing: zero code, zero symbols, zero overhead. Only a host test target
 * that defines FURBLE_TEST_SYNC compiles the call, and that target must link
 * an implementation of Furble::TestSync::point() (the host controller in
 * tests/host/testsync/).
 *
 * Point registry. Names are stable; tests depend on them. Keep this list
 * minimal and document the exact window each name marks.
 *
 * "connectall_returned"
 *   Control task, STATE_CONNECT handling in Control::task(). Fires after
 *   connectAll() has returned (m_ConnectInProgress already cleared) and
 *   before the task decides whether to publish the returned state. A task
 *   parked here can hold a STATE_DISCONNECTING result while disconnect()
 *   completes to STATE_IDLE on another thread, which is the exact window the
 *   republish guard below the point protects.
 *
 * "idle_connect_dequeued"
 *   Control task, STATE_IDLE handling in Control::task(). Fires after a
 *   queued CMD_CONNECT has been dequeued and before STATE_CONNECT is
 *   published, so a test can interleave work between command acceptance and
 *   the state transition it triggers.
 *
 * "disconnect_abort_armed"
 *   Caller task inside Control::disconnect(). Fires after m_ConnectAbort has
 *   been armed and before STATE_DISCONNECTING is published, the window in
 *   which connectAll() can already observe the abort while the state still
 *   reads CONNECTING. Do not park a barrier here while a connect is in
 *   flight. The abort token is armed at this point but cancelConnect() runs
 *   about 25 lines later, so parking widens the window in which connectAll()
 *   sees the abort before the state moves and returns STATE_CONNECTING
 *   instead of STATE_DISCONNECTING. Counting callbacks are safe. Note that
 *   Control::resetForTest(), the host-only reset seam, itself calls
 *   disconnect(), so a counting callback on this point also sees every
 *   reset-driven disconnect, not just the ones the test issues.
 *
 * "fujifilm_registration_wait"
 *   Connecting task inside Fujifilm::waitForRegistration(), after the GATT
 *   subscription is live and before the confirmation poll starts. A test that
 *   injects a registration notification, a geotag request or a link drop while
 *   the gate is waiting has to know the gate is waiting. Sleeping a fixed
 *   number of milliseconds first does not know that: on a loaded host the
 *   connecting thread may not have reached the wait yet, and the injection then
 *   either goes nowhere or lands after the deadline. Parking here makes the
 *   window exact. The point fires before the registration deadline is taken, so
 *   a parked thread does not spend the timeout it is about to observe.
 */

#if defined(FURBLE_TEST_SYNC)

namespace Furble {
namespace TestSync {

/**
 * Announce arrival at a named synchronisation point.
 *
 * Called from production code via FURBLE_TEST_SYNC_POINT. With no controller
 * installed on the name this returns immediately, so an unused point never
 * changes behaviour even in a test build.
 */
void point(const char *name);

}  // namespace TestSync
}  // namespace Furble

#define FURBLE_TEST_SYNC_POINT(name) ::Furble::TestSync::point(name)

#else

#define FURBLE_TEST_SYNC_POINT(name) \
  do {                               \
  } while (0)

#endif  // FURBLE_TEST_SYNC

#endif  // FURBLE_TEST_SYNC_H
