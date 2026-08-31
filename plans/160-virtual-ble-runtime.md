# 160 - strict virtual BLE runtime foundation

## Objective

Create a deterministic host transport boundary that models the parts of a BLE
camera link which can be represented without pretending that a C++ test double
is NimBLE. The runtime is a synthetic conformance peer and remains
`UNCERTIFIED` in every camera-oracle result.

## Implementation state

`sim/ble/VirtualBleRuntime.*` now owns link adapter IDs and generations, exact
GATT handles, characteristic properties, permissions, descriptor and CCCD
rules, ATT statuses, per-link security, MTU, PHY, connection parameters, RPA
identity, and an unbounded ordered trace journal. Notifications and
indications are distinct. An indication cannot be sent again until the
central confirms it. ATT success is kept separate from a modeled physical
outcome.

Every `OperationResult` and `TraceEvent` carries immutable `SYNTHETIC`
provenance and `certificationEligible=false`. There is no API that can relabel
this runtime as capture evidence. Delayed physical and controller events carry
the originating adapter and generation. Teardown or reconnect discards stale
events and writes a trace record instead of mutating the new link.

Profiles are validated before connection. Service ranges and declaration,
value, and descriptor handles must be nonzero, ordered, unique, and contained
by their service. ATT reads support offset chunks and writes support bounded
prepare chunks. Write-without-response is reported as local acceptance only;
it does not invent a peer or physical result.

Legacy advertising is limited to 31 payload bytes and explicit extended
advertising to 1650 bytes. The shared-pointer connection API owns a peer until
orderly teardown; the reference overload remains only for stack-owned fixtures.

`BondStore` and `RegistrationStore` are intentionally separate. A bond cannot
be used as application registration, and a registration cannot authorize an
encrypted ATT operation. `FaultOverlay` injects deterministic, counted
operation failures or explicit unsupported results. No random delay or failure
probability is used.

RPA resolution uses the Bluetooth `ah` AES-128 IRK hash when an IRK is present;
the current on-air address is never replaced by the identity address. Profiles
without an IRK may only use an exact capture address match, which is a synthetic
fixture convenience and is not certification evidence.

`sim/ble/nimble_boundary.*` is only the minimal Apache NimBLE NPL event queue
surface needed to prove deterministic FIFO event ordering and a separate
wrap-safe callout queue on the same virtual clock. Callbacks execute outside
the queue lock. It does not claim to be
the controller, HCI, ATT, GAP, SMP, or `esp-nimble-cpp` implementation. The
future production vertical slice must put the pinned NimBLE 2.5.0 C
host/controller below this boundary before production parity can be claimed.

## Synthetic peer and evidence rules

`SyntheticConformancePeer` provides an intentionally boring peer whose GATT
profile and physical outcomes are supplied by the test. This is useful for
negative-path coverage and wrapper integration. It is not a camera model and
does not import private bytes from public repositories. Official documents and
common implementations remain source and capture questions under plan 159.

The runtime has no oracle certification hook. A synthetic trace must be tagged
as synthetic when it is handed to the camera oracle. Exact camera, firmware,
controller, unit, environment, timing, power, and physical-outcome captures
are still required for `PASS_CERTIFIED` or `FAIL_CERTIFIED`.

## Acceptance and remaining work

- Host tests cover property and permission rejection, CCCD values, notification
  and indication confirmation, security, stores, MTU and PHY limits, RPA
  mismatch, one-shot faults, physical outcome separation, trace payload size,
  adapter generations, disconnect lifecycle, and NPL cancellation.
- The runtime is not yet wired into production `Control`, `Camera`,
  `CameraList`, or `Scan`.
- The C/NPL adapter has no HCI transport, controller timing, GAP event queue,
  ATT request encoder, SMP procedure, privacy resolver, or NimBLE task
  lifecycle. Those are explicit unsupported surfaces, not implicit success.
- Host scheduling, radio airtime, RF interference, analog current, camera
  firmware, and physical shutter outcomes remain unqualified boundaries.
- The shared C boundary still has no HCI transport, full GAP/ATT/SMP procedure
  engine, or production NimBLE task lifecycle. Those operations remain explicit
  unsupported work below the boundary.
