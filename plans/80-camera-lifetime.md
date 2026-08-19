# 80: Camera lifetime, own cameras by shared_ptr

## Motivation

`CameraList` owns the saved and scanned cameras in a
`std::vector<std::unique_ptr<Furble::Camera>>`. `CameraList::load()` and
`CameraList::clear()` free every entry. `CameraList::get()` and
`CameraList::last()` hand out raw `Camera*` that the UI passes straight into
`Control::addActive()`. From there the connect machinery holds those raw
pointers well past the call:

- `Control::Target::m_Camera` stores the raw pointer for the life of the target.
- `Control::m_ConnectCamera` tracks the camera being connected. `connectAll()`
  runs on the control task and calls `camera->connect()` outside the mutex,
  which blocks for the whole connect timeout.
- The UI connect screen reads `getConnectingCamera()` every timer tick to draw
  the name and progress bar, and the GPS path writes geotags through the same
  camera.

While a connect is in flight the UI can call `CameraList::load()` again. It runs
on the same LVGL task but at a different point: entering the connect or delete
menu, autoconnect, the console `connect` and `cameras` requests, and the scan
rebuild all reload or clear the list. `m_ConnectList.clear()` frees the very
`Camera` the control task is still connecting, and every raw pointer into it
becomes dangling. The in-flight `connect()`, the progress read, and the geotag
write then touch freed memory. This is a use after free with two symptom paths:

- Crash: the freed heap block is reused or poisoned, so the next NimBLE call or
  field read faults.
- False connected: the freed fields still read plausibly, so furble reports a
  connection on a camera object that no longer exists.

## Origin

Introduced upstream by gkoh/furble commit
[`f146821`](https://github.com/gkoh/furble/commit/f146821) "Implement
simultaneous camera connect (#127)". That change added the per-camera control
task and the raw `Camera*` snapshot into `m_ConnectCamera`, wiring a
cross-task consumer to the raw pointers that `CameraList` frees. The bug is
still present upstream.

## Fix

Own the cameras by `std::shared_ptr<Furble::Camera>` from `CameraList` all the
way through `Control`. Ownership is now shared, so a camera that a connect is
using stays alive until that use ends, independent of the list.

- `CameraList::m_ConnectList` becomes
  `std::vector<std::shared_ptr<Furble::Camera>>`. `get()` and `last()` return a
  `shared_ptr`. `load()`, `match()` and `addFauxNY()` build with `make_shared`.
  `load()` and `clear()` still drop the list's strong reference; a camera the
  control task still holds simply survives until that reference goes too.
- `Control::Target::m_Camera` and `Control::m_ConnectCamera` become
  `shared_ptr`. `addActive()` takes a `shared_ptr`. `getConnectingCamera()`
  returns a `shared_ptr`, so the UI connect screen holds a strong reference
  while it reads the name and progress. `connectAll()`, the adaptive power
  sampler and the conn-saver apply snapshot their cameras as `shared_ptr`, so a
  camera stays alive across the unlocked connect and radio calls even if
  `disconnect()` clears its target meanwhile.
- A `Target` releasing its `shared_ptr` on destruction is the last owner only
  when neither the list nor an in-flight connect still references the camera, so
  the camera is freed at exactly the right time. The #62 `m_Connected` lifecycle
  and the zombie-target quarantine are unchanged; a quarantined target now also
  keeps its camera alive through its `shared_ptr`, which is the goal.

### UI event user_data

The camera menu items used to stash a raw `Camera*` in the lv_event user_data.
That pointer outlives a `CameraList::load()`, so a click on a stale item was a
use after free even with shared ownership everywhere else. The items now stash
the CameraList index, not a pointer, and each event callback resolves the index
back to a camera fresh from `CameraList` at click time. A stale index that is
now out of range resolves to nothing and the callback returns, instead of
dereferencing freed memory. This covers the connect, multi-connect, delete and
scan-result items.

## Scope and no behavior change

This is a lifetime-only change. FauxNY, save and remove semantics, multi-connect
selection, and Infinite-ReConnect all behave exactly as before. The simulator
shims and callers are updated to match the new signatures so `sim/build.sh`
still compiles.

## Verification

Code only. The M5StickS3 was offline during this work, so this was built and
checked by compilation, not on hardware. The coordinator hardware-verifies the
connect, multi-connect, delete and reconnect paths before merge.

- Builds: m5stick-s3, m5stick-s3-debug, m5stick-c.
- Simulator: `sim/build.sh`.
- clang-format 21 clean on all changed files.
- The five committed release sdkconfigs are unchanged.
