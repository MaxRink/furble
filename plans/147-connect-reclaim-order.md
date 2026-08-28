# 147 - Failed-connect reclaim ordering

## Issue

A RICOH GR IV saved reconnect crashed the M5StickS3 with a LoadProhibited
panic during its secure-timeout retry cycle. The link came up, then died under
the encryption handshake: secureConnection() failed with rc=520 (0x200 | HCI
0x08 Connection Timeout). That failure wakes the connect task while the
controller's BLE_GAP_EVENT_DISCONNECT for the dead link is still queued on the
NimBLE host task, so the client still reports connected.

Camera::connect()'s failure branch then saw a live client and armed
setSelfDelete(true, false) before calling _disconnect(). The moment the arm
landed, the host task could consume the queued disconnect event: onDisconnect
fired and NimBLE freed the armed client inline, on the host task, while
_disconnect() was still dereferencing it on the connect task. That is the
use-after-free behind the panic. A second interleaving, where the queued event
is consumed inside the terminate with self-delete still off, does not crash
but leaks the client from the fixed NimBLE pool instead.

## Design

The failed-connect reclaim now tears the link down first and arms self-delete
last:

- _disconnect() runs with self-delete still off. If the queued disconnect
  event is consumed during the terminate, it only clears m_Connected; nothing
  is freed under the running code.
- After _disconnect(), m_Connected decides the reclaim path. If it is still
  true, onDisconnect has not run and a disconnect event is still in flight, so
  the reclaim arms setSelfDelete(true, false) and lets the client free itself
  through the callback, exactly the PR #229 behavior for a queued GAP event.
  The arm is the last touch of the client on the connect task. If m_Connected
  is false, onDisconnect already ran, no further event will fire, and the
  client is reclaimed directly through deleteClient().

m_Connected is the correct gate because it is cleared only after onDisconnect
ran. An isConnected()-based recheck was evaluated and rejected: it breaks the
ble-registration-cleanup scenario because a queued GAP event must free through
the callback, not through a direct delete.

As cheap hardening, every vendor class that caches remote characteristic
pointers now clears them at the top of _connect(). A failed connect can
reclaim the old client without running the vendor's _disconnect(), so the
cached pointers would otherwise dangle into the freed client for the next
session. Ricoh reuses clearRemoteState(); Fujifilm, Canon EOS remote and
smart, Sony, and Lumix null their cached pointers inline. DJI Osmo already
clears its protocol state at connect entry.

## Verification

- New host test ricoh-secure-timeout-uaf builds Camera.cpp, Ricoh.cpp, and the
  NimBLE mock under AddressSanitizer. The mock's new
  mockMarkLinkDeadEventPending() hook models the queued disconnect event: the
  peer fails secureConnection() with the link still reporting connected, and
  the event is delivered at whichever of the two consumption points the
  reclaim reaches first.
- Mutation check: with the fix reverted the test aborts under ASan with
  heap-use-after-free in the reclaim path. With the fix it passes and asserts
  no client leaks from the pool.
- Full host suite passes with the new test included.
- The M5StickS3 debug firmware builds with the required FURBLE_VERSION and
  FURBLE_TEST environment variables.
- The diff is checked for whitespace and em-dashes; clang-format is clean.

## Hardware boundary

The crash was observed on hardware as a LoadProhibited panic right after
"Camera connect failed" during a RICOH GR IV secure-timeout retry cycle; a
backtrace capture of the on-device repro is still pending. With the fix the
retry loop must survive repeated secure timeouts without a panic and without
exhausting the NimBLE client pool. That soak runs when the Ricoh body is next
available on the bench.
