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
rather than reporting a queue depth.

`cameras list` and `scan list` gained `camera<N>.saved` and
`camera<N>.selected` on every row. The saved flag earns its keep on `scan
list`: the connectable list carries saved cameras and scan results in the same
sequence, a scan can rediscover a camera the device already knows, and without
the flag a script cannot tell which verb applies to a row. `cameras list`
reloads the saved list first, so every row it prints is saved by construction.
The flag is answered by a new `CameraList::isSaved()`, which reads the store,
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
`CameraList::isSaved()` across a save and a remove, against an in-memory
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

The simulator's `CameraList` grew from a single saved flag into a real store of
saved names, because `delete all` and the multi-connect cap cannot be exercised
against one camera. Its `remove()` now leaves the loaded list alone, matching
the firmware, where erasing in place would have shifted the indices under a
caller sweeping the list.

Five certified e2e scenarios cover pairing (including the save on a successful
registration), the list and delete verbs, the multi-connect round trip and its
eight-name cap, the timer and bulb run-state transitions, and the display range
refusal with page and back.

## Implementation state

Implemented by PR #265 against fork master. The plan number races 165, which is
PR #264 and in flight; 161 is #261 and 164 has merged.

The on-device pairing run is owed and has not been performed: `scan start`,
`pair 0`, accept the prompt on the X100VI, then `cameras list` showing the saved
entry. Every other verb is covered by the host suite and by the firmware
compile. Only Fujifilm cameras are available, so the other vendors are code
review only, as usual.
