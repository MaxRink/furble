# 112 - Connect crash on a mid-connect peer drop

## Motivation

Found on hardware. While furble was connecting to a Fujifilm X100VI the camera
was power-cycled. The peer reset in the middle of the connect handshake and
furble crashed and rebooted. After the reboot, connecting worked normally.

This is a connect-path race, distinct from the disconnect and supervision
timeout work in #149 and the gone-peer reclaim in #128. Those cover a drop on an
established link. This is a drop while `Camera::connect()` is still running the
handshake. Tasks #40 and #54.

## Root cause

`Camera::connect()` armed `setSelfDelete(true, true)` on the NimBLE client for
the whole attempt. The vendor `_connect()` runs on the control task and
dereferences the client repeatedly: service discovery, the pairing and identify
writes, the notification and indication subscriptions. When the peer resets
mid-handshake, `onDisconnect` fires on the NimBLE host task and a self-deleting
client is freed there, while `_connect()` is still running and about to
dereference that same client. The next `m_Client->getService()` (through
`Fujifilm::subscribe`) lands on freed memory. Cross-task use-after-free, so it
depended on timing and only surfaced when the camera vanished at exactly the
wrong moment.

## Fix

Keep NimBLE self-delete off for the connect window. `Camera::connect()` now sets
`setSelfDelete(false, false)` before `_connect()`, so a mid-connect drop only
clears the connected flag and never frees the client out from under the running
handshake. `_connect()` then unwinds cleanly (its client calls return null or
false on the down link) and returns false.

On failure the wrapper reclaims the client deterministically on the control
task: tear down a still-live link, detach the callbacks
(`setClientCallbacks(nullptr)`, the #128 reclaim pattern so a late `onDisconnect`
lands on NimBLE no-op callbacks), then `deleteClient()`. This unifies the three
failure classes (never linked, half-open, mid-connect drop) into one path and
removes the reliance on an async self-delete that could race a reader.

On success self-delete is restored (`setSelfDelete(true, true)`) so the live
session tears down through `onDisconnect` exactly as before, unchanged from the
prior behavior. The client leak fix (#99, deleteClient on the never-linked
failure) and the half-open teardown are preserved.

## Tests

New host regression `connect_mid_drop_test` (ctest `connect-mid-drop`), compiled
with AddressSanitizer like `control_reclaim_uaf_test`. A new peer fault
`FujifilmVirtualCamera::dropLinkDuringConnect` severs the link during the
identify write and, on a self-deleting client, frees it inline through the new
`NimBLEClient::mockDropLinkSelfDelete` hook, modelling the NimBLE host task
self-delete. On the unfixed code `_connect()` then dereferences the freed client
and ASan aborts (heap-use-after-free in `NimBLEClient::getService`). With the fix
the connect fails cleanly, the camera is left disconnected, the client is
reclaimed (no pool leak), and a follow-up connect succeeds. Mutation proven:
restoring `setSelfDelete(true, true)` for the connect window reproduces the ASan
abort.

All 35 host ctests pass. clang-format clean.

## Implementation state

Implemented on `fix/connect-crash-mid-drop` off fork/master. Product change is
`lib/furble/Camera.cpp` only. Host-only additions: the mock hook, the peer
fault, the new test, its CMake target.

Owed on-device re-verify: connect while power-cycling the camera mid-attempt,
confirm no crash. The user hits this by hand.

## Follow-up (out of scope)

Reconnect after a furble-initiated disconnect is slower than necessary. The
first reconnect retry waits `ReconnectBackoff::FIRST_RETRY_MS` (2.5 s), the #122
stale-session backoff. After a furble-initiated disconnect there is no live
stale session on the peer, so that first wait is avoidable latency. A small
follow-up could skip the first-retry backoff when the disconnect was
furble-initiated. Not part of this crash fix.
