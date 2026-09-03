#ifndef FURBLE_TEST_SYNC_CONTROLLER_H
#define FURBLE_TEST_SYNC_CONTROLLER_H

#include <cstdint>
#include <functional>

// Host-side controller for the FURBLE_TEST_SYNC named sync points declared in
// lib/furble/FurbleTestSync.h. Link testsync/TestSyncController.cpp into a test
// target that compiles the production sources with FURBLE_TEST_SYNC defined.
// It implements Furble::TestSync::point() and adds the control surface below.
//
// Every parked thread carries its own timeout. A forgotten release therefore
// unparks after the timeout and latches anyTimedOut(), so the test fails
// instead of the suite hanging.

namespace Furble {
namespace TestSync {

// Clear all barriers, callbacks, and the timeout latch. Call at test start.
// Must not be called while a thread is parked at a point.
void reset(void);

// Park the next thread arriving at the named point until release() or until
// timeout_ms expires. One shot: the barrier disarms once the parked thread
// resumes.
//
// Must not be called for a name that already has a thread parked on it. It
// overwrites the parked and released flags in place, so the parked thread
// loses its release and stays stuck until its own timeout expires, which
// latches anyTimedOut(). Arm, await, release, then arm again.
void armBarrier(const char *name, uint32_t timeout_ms);

// Run the callback on every thread arriving at the named point. The callback
// runs after the thread has passed any armed barrier. Replaces a previous
// callback; an empty function clears it.
//
// The callback is snapshotted when the thread arrives, before it waits on
// any barrier, and the snapshot is what runs. A callback installed while a
// thread is already parked therefore does not run for that thread, only for
// the next arrival. Install callbacks before the traffic they observe.
void onPoint(const char *name, std::function<void(void)> callback);

// Block until a thread is parked at the named point. Returns false if none
// arrives within timeout_ms.
bool awaitArrival(const char *name, uint32_t timeout_ms);

// Release the thread parked at the named point.
void release(const char *name);

// True once any parked thread has timed out waiting for release(). A test
// must assert this is false at the end.
bool anyTimedOut(void);

}  // namespace TestSync
}  // namespace Furble

#endif  // FURBLE_TEST_SYNC_CONTROLLER_H
