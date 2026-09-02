# 166 - console workflow coverage

CLAUDE.md names the USB console as the automation surface for this project, and
plan 162 measured it into the host suite. A bench session on 2026-09-02 found
the surface incomplete in the one place that matters most for unattended
testing: a freshly scanned camera could not be onboarded from the console at
all.

`scan start` and `scan list` showed results, but `connect <index>` resolved its
index only against the saved camera list and logged `console: no camera at
index N`. `bt pair yes|no|key` only answered a prompt that was already pending.
Fresh pairing was UI only: Scan page, select a row, connect, and the camera is
saved when its registration succeeds. So bench onboarding could not be
scripted, and every soak run started with a manual walk through the menus.

Auditing the rest of the menu tree against the command table found the same
shape in several other places, so this plan closes the whole gap rather than
the one verb: every workflow the UI offers is now scriptable from the console,
through the same production code path the menu click takes.

## Principle

A console verb never carries a copy of a UI behaviour. It parses its
arguments, applies the gate a script needs to see, and hands one `UI::Request`
to the task which owns the list or the widget. That handler then runs exactly
what the button click runs. So the pairing prompt, the registration gate, the
save on a successful registration, the shutter release on an interval stop and
the page navigation that keeps a bulb exposure alive are all identical
whichever way the workflow is driven, because there is only one implementation
of each.

Where reuse was blocked by a behaviour being inlined in an LVGL lambda, the
behaviour was factored out and both callers now use the helper, rather than the
console growing a second copy. `UI::loadMultiConnectSelection()` and
`UI::seedMultiConnectSelection()` came out of the Connect page's seeding loop
for exactly this reason.

## Verbs added

| UI workflow | Console verb |
| :--- | :--- |
| Scan page, tap a result to pair | `pair <scan index>` |
| Delete page, tap a saved camera | `delete <index>` |
| Forget every saved camera | `delete all`, no menu equivalent |
| Connect page multi-select checkboxes | `multiconnect select \| deselect <index>` |
| The remembered multi-connect set | `multiconnect list \| clear` |
| Timer page start, stop and state | `interval start \| stop \| status` |
| Bulb page start, stop and state | `bulb start \| stop \| status` |
| Display page brightness, applied live | `display brightness <value>` |
| Display mode and page settings readout | `display status`, `display mode` |
| Off menu entry | `power off` |
| Current page name | `ui page` |
| Header back button | `ui back` |

Every one of them ends its answer with `result: <token>`, a machine readable
outcome a host script can branch on without matching prose. The tokens are
`ok`, `no_scan_result`, `no_saved_camera`, `not_running`, `no_button`, `range`
and `selection_full`. Because the refusals are produced on the UI task, which
owns the list or the widget the verb addresses, those verbs wait for the answer
rather than reporting a queue depth, and the token is the verb's exit status:
`ok` returns 0, every other token returns 1.

All of them go through one `sendWorkflowRequest()` helper for that, so the
contract cannot be half applied. An earlier revision let `delete`,
`multiconnect select | deselect` and `ui back` return on the queue send while
the documents promised the wait, which made a refused destructive verb look
like a success to a script reading to the next prompt. The wait is a bounded
100 ms against a 5 ms queue drain, and a UI task still busy after it answers
`error: no answer from the ui task` with a non-zero status rather than an
outcome it never gave.

The headless build answers the same contract from its own request loop in
`src/main.cpp`: the same lines, the same `result:` token, the same non-zero
status on a refusal. It is a second request loop by construction, because it
has no LVGL and no UI task, but it is no longer a second answer format.

`cameras list` and `scan list` gained `camera<N>.saved` and
`camera<N>.selected` on every row. The saved flag earns its keep on `scan
list`: the connectable list carries saved cameras and scan results in the same
sequence, a scan can rediscover a camera the device already knows, and without
the flag a script cannot tell which verb applies to a row. `cameras list`
reloads the saved list first, so every row it prints is saved by construction.
The flag is answered by a new `CameraList::isSavedAddress()`, which reads the store,
since that is the only authority.

`CameraList::load()` rebuilds every Camera and so resets the active flags. The
Connect page seeds the remembered multi-connect set back before it draws its
checkboxes, and the console reload path now does the same, or every row would
have reported `selected: false` whatever the store held. Every load site in the
UI goes through one `UI::reloadCameraList()` so that cannot drift.

The existing `saved:` and `count:` lines are unchanged, so scripts which parse
them keep working.

## Pairing

`pair` is the Scan page row click. The handler activates the scan result,
sets the connect context's menu name to the Scan page, and calls `doConnect()`.
That menu name is the gate which saves the camera when its registration
succeeds, and the console scan never set it because it never entered the page,
so the request sets it. Nothing else about the flow differs from a tap.

Whether the connectable list currently holds scan results is knowable only on
the task which last rebuilt it, so that is where the refusal lives: `pair`
answers `no_scan_result` when the list is the saved one or the index names
nothing. An earlier revision gated on `Scan::isActive()` from the console task,
which was wrong twice over. The console scan path did not apply the scan
timeout setting, so it inherited whatever the last UI scan left behind and its
results could outlive it by an arbitrary amount; and scan results stay pairable
after a scan ends, exactly as the Scan page keeps its rows clickable. The
console scan now applies the same duty and timeout settings `startScan()`
applies.

A camera which raises a pairing confirmation is answered with `bt pair yes`, as
before.

## Timer and bulb

All four verbs send the real button event rather than calling a start or stop
helper, because both buttons carry behaviour beyond it.

The load-page callback is registered after the click callback on both Start
buttons, so one synthetic click starts the run and then navigates to its run
page, in that order. That ordering is load bearing for the bulb: leaving the
Bulb run page stops the exposure, so a start which skipped the navigation would
be cancelled by the next page change. Both refuse to start without an active
connection, matching the existing shutter command gate, because both fire the
shutter.

Stop is the same argument in reverse. The Stop callbacks release the shutter
and click the header back button, neither of which `bulbStop()` or a paused
timer does, so the Bulb run page Stop button is now held as a member for the
same reason the Start button already was. Both stops are gated on the run
state: without the gate a synthetic click would release a shutter a script is
deliberately holding and navigate the UI out from under it, and the Bulb Stop
button restarts a finished exposure rather than stopping it.

## Display brightness

The Display page slider spans the board's minimum brightness to 240 in steps of
16, and that minimum is a board fact the console task cannot see. Below it the
panel is black, and a persisted black panel needs a reflash to undo, so a value
outside the range is refused with `result: range` rather than clamped silently.
`display status` prints the range from the UI task so a script can read it.

## Not covered, and why

- **Navigating to a page by name.** The name to page tables exist only under
  `FURBLE_SIM`, and `m_Menu` is keyed by pointer identity rather than string
  content. Adding a second navigation mechanism to firmware to serve the
  console is not worth the weight, so `ui page` reads and `ui back` steps out.
- **Touch calibration.** It needs real touches at real coordinates.
- **Timer and bulb configuration.** Count, delay, shutter, wait and bulb
  duration are struct settings which only the LVGL rollers write, and the UI
  reads them once at construction, so a console write would be both invisible
  and overwritten. `interval status` and `bulb status` report the live values
  instead, so a script can assert what a start would actually run.
- **Settings export and import to SD.** `settings list`, `settings set` and
  `provision` already move the same data over the console.
- **Pairing on the headless build.** `pair` returns `not supported in this
  build` there. The save on a successful registration lives on the UI task and
  the headless build has no equivalent, so it would connect and then forget the
  camera. Display-less onboarding needs that gate built first; it is a separate
  piece of work.

## Tests

`tests/host/console_commands_test.cpp` gains `testWorkflowCommands()`, which
drives every new verb through the production `Console::init()` command table
and asserts on what reached the UI queue double: the request enum and its
argument, the usage and refusal text, the acknowledgement line, and that a
refused verb queues nothing at all. The command table and subcommand contracts
were extended, so a dropped registration or a renamed subcommand fails the
suite. `tests/host/advertisement_dispatch_test.cpp` covers
`CameraList::isSavedAddress()` across a save and a remove, against an in-memory
Preferences stub that replaces the previous no-op one.

The refusals that only the UI task can produce are asserted by the simulator
scenarios rather than the host suite, because that is where the deciding state
lives.

Five mutations were checked and all were caught: dropping the `pair` enqueue,
dropping the multi-connect enqueue, sending `delete all` a positive index,
making `interval status` print nothing, and removing the no-scan gate from
`pair`.

## Simulator coverage

The UI-side request handlers are the half of this work that the host command
suite cannot reach: `tests/host` never compiles `src/FurbleUI.cpp`. The
simulator now builds with `FURBLE_CONSOLE`, in both `sim/CMakeLists.txt` and
`sim/build.sh`, so those handlers are compiled and executed rather than only
compiled by the firmware build. `src/FurbleUIAudit.cpp` comes with it and left
the build inventory exemption list. `src/FurbleConsole.cpp` itself stays out:
it is the serial transport and the command parser, both already driven end to
end by the host suite against the real ESP-IDF console API, and
`sim/FurbleConsoleSim.cpp` supplies the few entry points other firmware calls.

A scenario drives a request with `action console <request> [arg]`, which posts
through `UI::sendRequest()` exactly as the console does, and reads the answer
back through a new `console.<key>` assert namespace fed by a single
`UI::consolePrint()` output path. `console.result` is the outcome token.

PR #261 landed first and put the production `Control`, `Camera`, `CameraList`
and `Scan` into the simulator over MockNimBLE, which removed the simulator's
own list substitute. So the scenarios here run against the same
`CameraList::isSavedAddress()`, `save()` and `remove()` the firmware runs, and the
pairing scenario's save on a successful registration is the production
registration path rather than a model of it. A `saved_cameras N` seed stands up
a multi-entry saved list, because `delete all` and the multi-connect cap cannot
be exercised against one camera.

### Measured coverage

The numbers below are the CI coverage artifacts, master run 33633532620 on
`ab638874` against run 33640436617 on this head. An earlier revision of this
plan compared each file against its floor rather than against master, which
made the union look like a full point of improvement when it is not.

| Panel or file | master | this head |
| :--- | ---: | ---: |
| grand union | 70.42 | 70.45 |
| host | 63.52 | 64.71 |
| sim m5stick-s3 | 51.47 | 52.16 |
| sim m5stick-c | 36.71 | 37.61 |
| sim m5stack-core | 36.28 | 37.14 |
| `src/FurbleUI.cpp` | 80.71 | 81.20 |
| `lib/furble/Camera.cpp` | 74.75 | 74.75 |
| `src/FurbleConsole.cpp` | 90.03 | **89.55** |
| `lib/furble/Scan.cpp` | 91.20 | **90.05** |
| `src/FurbleUIAudit.cpp` | not built | 0.00 |

The union moves 0.03 points, not a point. The work adds 584 covered lines but
822 instrumented ones, and 186 of those are `src/FurbleUIAudit.cpp` entering the
simulator build at 0.00 percent: it compiles there now, but no scenario drives
`ui audit` and `audit` is not in the `action console` vocabulary, so it dilutes
the union rather than lifting it.

Two files regress, and both regress for the same reason: `FURBLE_CONSOLE`
compiles code into the simulator which no scenario can reach.

`src/FurbleConsole.cpp` 90.03 to 89.55, 1572/1746 to 1713/1913. It is not in the
simulator build at all, so this is the host suite alone: the new verbs add 167
instrumented lines, 141 of them covered. The uncovered remainder is the
`FURBLE_NO_DISPLAY` refusal arms and the queue-unavailable arms of the new
verbs, which the host build does not compile and does not fault in.

`lib/furble/Scan.cpp` 91.20 to 90.05, 311/341 to 353/392. Defining
`FURBLE_CONSOLE` for the simulator compiles the BT journal call sites, 51 more
instrumented lines with 42 of them covered. Seven of the nine which are not are
`Scan.cpp:187-189` and `328-331`, the journal on the custom callbacks scan
overload `Scan::start(NimBLEScanCallbacks *, ...)`. Its console caller is
`src/FurbleBtDebug.cpp`, which the simulator does not build, and no certified
scenario drives the vendor reconnect which is its other caller. The last two are
`Scan.cpp:213-214`, where llvm-cov splits the scan-end journal call across two
lines and counts only its continuation. So one floor moved down, to 89.50
against a measured 90.05, and `tests/coverage_floor.json` records that reason.

Every floor still passes, so nothing is blocked, and the other floors are left
where they are rather than ratcheted up so this PR does not raise the bar for
the PRs merging ahead of it.

The second review round changed `src/FurbleConsole.cpp` again. Routing every
workflow verb through one `sendWorkflowRequest()` removed the duplicated queue
and print blocks, and a local `tools/coverage.py --check` on that head, rebased
onto `f425fd38`, measures the file at 89.92 percent (1712/1904) with the grand
union at 70.29 percent and every floor passing. The CI coverage floor job on
this head is the authority for the published numbers.

### Follow-ups not taken here

- `src/FurbleUIAudit.cpp` is at 0.00 percent. Adding `audit` to the `action
  console` vocabulary in `sim/scenario_action.cpp` plus one assert would turn
  186 diluting lines into real coverage. It needs `UIAudit::dump()` to print
  through `UI::consolePrint()` first, so a scenario can read the answer.
- `delete all` runs the whole sweep in one request under the UI mutex, N NVS
  commits and N bond removals with LVGL stalled, where the Delete page only
  ever does one per click. Bounded by the saved list size, but it wants a
  yielding sweep before a bench script points it at a large list.

Five certified e2e scenarios cover pairing (including the save on a successful
registration), the list and delete verbs, the multi-connect round trip and its
eight-name cap, the timer and bulb run-state transitions, and the display range
refusal with page and back.

## Sequencing

Rebased onto fork master `ab638874`, which carries PR #261.

Merge order is #266, then #245, then this. Two things this PR touches are
things #245 also touches, so the rebase has a checklist rather than a sentence.

**The saved flag.** #245 adds `CameraList::isSaved()`, matching on
`CameraListProtocol::sameSavedIdentity()`: vendor type plus address, with the
advertised name as a fallback for Fujifilm Secure. This PR needs only the narrow
address-key test and deliberately does not claim that name, so it calls its
helper `CameraList::isSavedAddress()`. At the rebase, drop
`isSavedAddress()` and point `camera<N>.saved` at #245's `isSaved()`, which is
the better rule. That changes the flag's semantics in one case worth knowing
about: a re-addressed Fujifilm Secure body starts reading `true` where the
address-key test read `false`.

**The pairing handler.** `Request::PAIR` is deliberately thin, the same three
steps the Scan page row click takes, so it wants to become a call to
`UI::beginPairing()`. That swap is not literal. `beginPairing()` is
`setActive(true)` plus `doConnect(e)`; it does not set
`m_ConnectContext.menuName = m_ScanStr`, because the Scan page path already set
it in `startScan()`. The console path never enters the page, so a literal swap
drops the one line which makes a successful registration save the camera, which
is the whole point of the verb. The rebase must either keep that assignment in
the handler or give `beginPairing()` a parameter for it.

Rebase checklist:

1. Keep `m_ConnectContext.menuName = m_ScanStr` in the `PAIR` handler, or carry
   it into `beginPairing()`.
2. Keep the assertion which already guards it: the `console-pairing` scenario's
   `assert console.saved 1` after a successful registration. Removing the menu
   name assignment fails it with `expected '1' got '0'`, which is mutation 5 of
   the review round two, so the guard is proven and must survive the rebase.
3. Replace `CameraList::isSavedAddress()` with #245's `CameraList::isSaved()`
   and re-read the `camera<N>.saved` assertions in `console-camera-list`.
4. `beginPairing()` also refuses an already-saved camera and does not clear the
   other cameras' active flags, both of which the `PAIR` handler does today.
   Neither has a token in the table yet, so adding `already_saved` is part of
   the swap, not a follow-up.

## Implementation state

Implemented by PR #265 against fork master. The plan number races 165, which is
PR #264 and in flight; 161 is #261 and 164 has merged.

The on-device pairing run is owed and has not been performed: `scan start`,
`pair 0`, accept the prompt on the X100VI, then `cameras list` showing the saved
entry. Every other verb is covered by the host suite and by the firmware
compile. Only Fujifilm cameras are available, so the other vendors are code
review only, as usual.
