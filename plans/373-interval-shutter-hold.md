# 373: intervalometer shutter hold decision record

**Decision: the default stays at `{30, SpinValue::UNIT_MS}`. No code change, no
settings migration, no new number.** Decided 2026-11-27. This document records
what the value controls, what supports the shipped number, why the proposed
400 ms is rejected, and the one piece of evidence that would reopen it. It is a
decision record, not an implementation plan: nothing here is queued for a
follow-up PR.

The proposal came from action item 2 of `plans/371-fork-network-sweep.md:65-68`:
`zhuimeng2026-hub/furble` commit `78a9a7b5` (2026-11-17) raises the default to
400 ms and migrates stored values, on the claim that 30 ms "is too short for the
camera to acquire focus and start the shutter".

## What the number controls

The Shutter value is the remote's press-to-release window. It is not a wait for
the camera to finish autofocus, and it is not a timeout on the camera.

- `UI::intervalometer()` in `src/FurbleUI.cpp:7764-7773` enters
  `STATE_SHUTTER_OPEN`, posts `CMD_SHUTTER_PRESS`, sets the LVGL timer period to
  the Shutter value, and moves to `STATE_DELAY`.
- `src/FurbleUI.cpp:7775-7778` enters `STATE_DELAY`, posts
  `CMD_SHUTTER_RELEASE`, then sets the timer period to the Delay value.
- The run page label for that state reads SHUTTER, from `src/FurbleUI.cpp:360-361`
  and `docs/ui-walkthrough.md:221-226`.

So the value is the wall-clock gap between one press command and one release
command. The camera owns everything after that: when it starts autofocus, when
it starts the exposure, and how long the exposure lasts. Releasing the remote
sooner does not shorten the camera's own timed exposure on a body in P, A, S or
M; it ends the "button down" state.

The configured value is also a floor on the gap, not the whole gap. Commands are
queued per target and executed one at a time on the target task
(`src/FurbleControl.cpp:169-199`), and every family with a separate release
writes with response except Canon EOS Smart. On the rest the release therefore
cannot reach the radio before the press write has returned.
`Control::sendCommand` routes shutter and focus straight to the per-target queue
and never reorders ordinary commands (`src/FurbleControl.cpp:102-129` puts
`CMD_DISCONNECT` at the front and nothing else; `src/FurbleControl.cpp:600-608`
does the routing).

The delivery floor is set by the link. While the connection saver is off, which
is the shipped default (`src/FurbleSettings.cpp:713`), `setConnProfile(FAST)`
returns without a request and the link keeps its pre-connect parameters
(`lib/furble/Camera.cpp:843-847`): interval 30 to 50 ms
(`lib/furble/Camera.h:699-700`, that is `BLE_GAP_INITIAL_CONN_ITVL_MIN` and
`_MAX` in the pinned ESP-IDF `ble_gap.h:113,116`), peripheral latency 1
(`lib/furble/Camera.h:701`). With the saver on, a quiet link sits at 250 to
300 ms interval, and the first press of a batch goes out at that interval
(`lib/furble/Camera.h:711-731`).

## Where 30 ms came from

There is no measurement in this tree and no protocol requirement in
`docs/protocols/` or `docs/camera-research/`. The number is a product decision,
and the person who made it stated the intent in public. Upstream maintainer
gkoh, `gkoh/furble#244`, comment of 2026-01-21T06:20:06Z:

> The current timer sends a 'shutter open' command then waits 30ms (default)
> then sends the 'shutter close' command. If in one of the auto-modes, the 30ms
> is typically enough for the camera to gain auto-focus and trigger the
> auto-exposure shutter time. If in CL/CH burst mode you should get multiple
> frames. If you set to bulb mode, furble should now have full timing control of
> the exposure time.

Three design facts follow, and all three still hold in this tree:

1. 30 ms was chosen as the value that typically lets a body in P, A, S or M
   start its own autofocus and exposure. The proposal under test asserts the
   opposite of the person who tested the number.
2. A longer hold produces more frames in continuous drive, because the body keeps
   shooting while the button is down.
3. On a body that latches the remote press, the same value is the exposure in
   bulb. Furble also has a separate bulb path that refuses a camera which cannot
   end a held exposure (`docs/ui-walkthrough.md:195-214`).

## Evidence per camera family

"Honours" means the release write changes camera state, so the Shutter value has
an effect. "Ignores" means the release write does nothing, so the Shutter value
is dead time in the shot cycle.

| Family | Driver | Press | Release | Write flag | Shutter value | Source |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Fujifilm Basic and Secure | `Fujifilm` | `{01 00}` then `{02 00}` | `{01 00}` then `{00 00}` | response | Honours. `2` is full press, `0` is release, so the value is how long the body sees full press. | `lib/furble/Fujifilm.cpp:178-195`; `lib/furble/protocol/FujifilmProtocol.cpp:144-158`; `docs/camera-research/fuji-canon-nikon.md:107-113`; `docs/camera-research/parity-matrix.md:312-313` |
| Canon EOS Remote | `CanonEOSRemote` | `{8c}` | `{0c}` | response | Honours. Separate press and release bytes. | `lib/furble/CanonEOSRemote.cpp:69-79`; `docs/camera-research/parity-matrix.md:318` |
| Canon EOS Smart | `CanonEOSSmart` | `{00 01}` | `{00 02}` | no response | Honours. Separate press and release bytes. Both can share one connection event because neither waits for an ATT response. | `lib/furble/CanonEOSSmart.cpp:194-202`; `docs/camera-research/fuji-canon-nikon.md:296-298`; `docs/camera-research/parity-matrix.md:317` |
| Nikon Remote | `NikonRemote` | `{02 02}` short press | `{02 00}`, a documented no-op | response | Ignores. The press completes the capture on its own. Nikon also states the ML-L7 shutter cannot be held for burst capture. | `lib/furble/NikonRemote.cpp:89-97`; `docs/camera-research/fuji-canon-nikon.md:421-423`; `docs/camera-research/fuji-canon-nikon.md:367-372`; `docs/camera-research/parity-matrix.md:319-320` |
| Nikon Smart | `NikonSmart` | nothing | nothing | n/a | Ignores. The mode cannot become active over BLE. | `lib/furble/NikonSmart.cpp:167-181`; `docs/camera-research/parity-matrix.md:321` |
| Sony | `Sony` | `0x0901` full down | `0x0801` full up | response | Honours. Full down and full up. | `lib/furble/Sony.cpp:173-183`; `lib/furble/Sony.h:124-125`; `docs/camera-research/sony-lumix-dji.md:135-136`; `docs/camera-research/parity-matrix.md:322` |
| LUMIX | `Lumix` | opcode `04` | opcode `05` | response | Honours. ShutterOn and ShutterOff inside the started session. | `lib/furble/Lumix.cpp:273-284`, `lib/furble/Lumix.cpp:322-328`, `lib/furble/Lumix.h:96-97`; `docs/camera-research/sony-lumix-dji.md:282-283`; `docs/camera-research/parity-matrix.md:323` |
| Ricoh | `Ricoh` | `{01 01}` capture with AF | logs and returns | response | Ignores. Single-write capture. | `lib/furble/Ricoh.cpp:448-458`; `docs/camera-research/parity-matrix.md:314-316`; `docs/settings-and-controls.md:232-233` |
| DJI Osmo Action | `DJIOsmo` | record toggle | logs and returns | n/a | Ignores. No still capture on this path. | `lib/furble/DJIOsmo.cpp:447-462`; `docs/camera-research/parity-matrix.md:324` |
| FauxNY | `FauxNY` | logs | logs | n/a | Ignores. Test camera. | `lib/furble/FauxNY.cpp:56-70` |

The delivered default is `{30, SpinValue::UNIT_MS}` at `include/interval.h:11`,
used by `Settings::load<interval_t>` (`src/FurbleSettings.cpp:424-429`) and by
`applyDefault` (`src/FurbleSettings.cpp:677-686`). It is documented as "How long
the shutter is held per frame" in `docs/settings-and-controls.md:225` and
`docs/wiki/Settings-Reference.md:215`.

## Why 400 ms is rejected

**It has no source.** The only reason the fork gives is a header comment in the
same commit: "Long enough for the camera to focus and fire, see #208", at
`zhuimeng2026-hub/furble` `include/interval.h` lines 20 to 24 of `78a9a7b5`.
`gkoh/furble#208` is "Fujifilm firmware update (July 2025) breaks furble
connectivity": pairing, bonding and rotated Bluetooth addresses. It says
nothing about shutter timing, so the citation does not support the number. The
commit message adds no reference of its own, and the same commit bundles an
unrelated AF-ON label rename, so the value arrived with a UI preference rather
than with a measurement.

**It is wrong on Ricoh, DJI and Nikon.** Those three read the press as the whole
capture and ignore the release, so 400 ms adds 370 ms of dead time to every shot
cycle and changes nothing about the picture. Nikon goes further: the ML-L7
shutter "cannot be held for burst capture" at all
(`docs/camera-research/fuji-canon-nikon.md:367-372`).

**It multiplies burst frames.** A body in CL or CH keeps shooting while the
button is down, which is stated intent in `gkoh/furble#244`. At 3 to 20 frames
per second, 400 ms is 1 to 8 extra frames per intervalometer step instead of the
single frame a 30 ms hold gives.

**It is a 13x longer default bulb exposure.** On Fujifilm, Sony and LUMIX the
same value is the exposure in bulb, because the release ends the exposure. A
user who never opens the Timer page and runs a bulb sequence would go from 30 ms
to 400 ms exposures.

**It still would not guarantee autofocus.** 400 ms is not an autofocus timeout.
In dim light AF from a full press is routinely slower than that, so the change
buys nothing it promises while paying all of the above.

## Per family defaults, considered and rejected

Plan 371 asked for the number to be decided per camera family rather than
copied. The per-family fact that exists is which drivers ignore the value at
all, and that is already documented (`docs/settings-and-controls.md:232-233`,
`docs/ui-walkthrough.md:171-176`). What does not exist is a per-family
requirement: the need for a longer hold is a property of the body, its firmware
and the mode it is in (AF-S against AF-C, single against continuous drive,
P/A/S/M against bulb), not of the vendor. A per-vendor constant would encode a
guess about a mode-dependent property in `lib/furble/`, and it would still be
wrong for a Fujifilm body in CH.

If a body ever needs a different number, the right shape is a capability the
driver declares, so the UI can say whether the value is honoured and warn when
it matters. That needs a capture to justify, and it is not justified today.

## The migration argument

No migration is added, because the default does not change. Recorded here so the
fork's migration is not copied later, and because the argument is the reason this
default cannot simply be changed:

- The interval blob has no version field and no dirty bit. It is `count`,
  `delay`, `shutter`, `wait` (`include/interval.h:13-30`), and its version is
  implied by `sizeof` (`src/FurbleSettings.cpp:420-430`). The only existing
  migration widens `interval_v1_t` to `interval_t` by record size and fills
  `wait`.
- A record is written only when a spinner is edited
  (`src/FurbleUIIntervalometer.cpp:78-82`, called from the spinner owner's
  `save()`), and it writes all four values. A user who once changed Delay has a
  stored Shutter of 30 ms that they never chose. A user who deliberately set
  30 ms has a byte-identical record.
- So a v2 to v3 migration has exactly two defensible rules and cannot have both:
  keep every stored 30 ms, which is safe and reaches no one, or rewrite every
  stored 30 ms, which reaches everyone and destroys an explicit choice. The fork
  chose the second, which breaks `plans/README.md:5-6`: defaults keep current
  behavior.
- A future default change would first need a v3 layout with an explicit "user
  set this" bit written on the first spinner edit, and even then the v2 to v3
  step still has to pick one of the two rules above.

## What this decision turned up

Both are recorded here so the next reader does not have to rediscover them.

1. **A configured Shutter of 0 is not an immediate release.**
   `src/FurbleUI.cpp:7837-7840` only calls `lv_timer_set_period` when `next > 0`,
   so the Shutter state inherits whatever period the timer already had. With
   Shutter set to 0 the release is therefore scheduled at the previous period,
   which after a frame is the Delay value: a 15 s delay becomes a 15 s shutter
   hold. 0 is inside the documented range
   (`docs/settings-and-controls.md:225`). This is a separate defect, fixed in
   PR #548 (plan 376), not here.
2. **The Timer docs stated the hold but not its two consequences.** At the
   time of this decision neither `docs/settings-and-controls.md:225` nor
   `docs/wiki/Settings-Reference.md:215` said that the value is the exposure in
   bulb on bodies that latch the press, or that a longer value shoots several
   frames on a body in continuous drive. Both follow from `gkoh/furble#244` and
   from the per-family table above. The wording gap is closed by PR #548 (plan
   376), which owns the Timer text.

## Flip condition

One real case reopens this, and nothing else does:

- A hardware capture naming body, firmware and mode where a 30 ms hold produces
  no frame and a longer hold produces one, with the AF mode and drive mode
  stated. The fix would then be a driver capability plus a UI hint, and a
  default raised only for the bodies that need it.
- Or a vendor protocol document that states a minimum remote press time. None of
  the vendor pages pinned in `docs/camera-research/` does.

## Doc surface

No behaviour change, so no doc change is required by the documentation mandate.
`docs/settings-and-controls.md:225` and `docs/wiki/Settings-Reference.md:215`
already state the shipped default and agree with `include/interval.h:11`. That
agreement was verified by reading, because no gate compares the three:
`tests/host/settings_table_test.cpp:294-298` reads only
`src/FurbleSettings.cpp` and `include/CLAUDE.md`, and
`tools/check_settings_contracts.py` reports the INTERVAL default as an
unresolved aggregate, so its documentation comparison marks the row unverified.
`docs/supported-hardware.md` makes no claim about the intervalometer value and
does not need to change. `docs/ui-walkthrough.md:414-423` describes the Shutter
row without a number, and no UI label changes.
