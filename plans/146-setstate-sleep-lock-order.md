# 146 - Sleep lock ordering in Control::setState

## Issue

`Control::setState` published `m_State = STATE_ACTIVE` before acquiring the
`NO_LIGHT_SLEEP` power lock. `Control::getState` reads `m_State` without
`m_StateMutex` by design. A lock-free reader could therefore observe the
active state while the sleep lock was not yet held. The window is a few
instructions wide. On hardware the same task acquires the lock microseconds
later, so there is no functional consequence. The control fuzzer caught the
window once under heavy host CPU load: seed 42 reported "active but sleep lock
not held at stale-session-reconnect".

## Design

`setState` now computes the hold decision first, acquires the sleep lock
before storing the new state, and releases it after the store. A reader that
observes `STATE_ACTIVE` is now guaranteed the sleep lock is held. The release
ordering is safe because every path to `STATE_IDLE` passes through
`STATE_DISCONNECTING` first, so the idle state is never published with the
lock still held. The early return for an unchanged hold state is replaced by
guarded acquire and release branches with identical net behavior.

## Verification

- Root cause was proven by mutation: a forced 10 ms delay between the state
  store and the lock acquire makes the fuzzer finding fire on every active
  settle, and the same delay with this reorder passes 10 iterations clean.
- The host suite runs green, including all control fuzz seeds.
- The affected M5StickS3 debug firmware build is run with the required
  `FURBLE_VERSION` and `FURBLE_TEST` environment variables.

## Hardware boundary

No hardware behavior changes. The lock is acquired in the same call by the
same task. No hardware test is required for this ordering fix.
