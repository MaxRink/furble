# 105 - Seeded UI fuzzer

Status: tooling plus findings report. This PR adds a reusable simulator fuzzer
and wires it into CI. It fixes no product bugs. Any product fix is a separate
follow up PR, per the fault-hunting discipline.

Implementation state: the fuzzer now has an extracted, dependency-free phase
and random machine with explicit Apply, Settle, Check, Escape, and Finish
phases. Invariant and timer-stop reads occur only after the requested number of
completed LVGL cycles, while the continuous liveness check runs before every
fuzzer tick. The native CI guard runs on all three modeled panel classes. No
sanitizer CI or malformed-input validation is part of this slice.

## Goal

Find UI bugs automatically across the whole on-screen UI rather than one scripted
path at a time. The scripted scenarios (plans 85, 86, 87) each walk a fixed
route. A seeded fuzzer instead feeds long randomized input streams into the real
FurbleUI and checks a set of invariants after every event, so it explores state
combinations no hand-written scenario covers: rapid back and forward, entering
and leaving every page, editing settings mid-navigation, raising the companion
modal at an arbitrary moment, and starting and stopping the intervalometer while
the menu moves underneath it.

The fuzzer runs against the real UI and LVGL layer over a fake Control and
Camera, the same seams the scripted scenarios use. It is FURBLE_SIM only and
never changes firmware behavior or the release binary.

## Design

### Event model

Every event is drawn from a single seeded `std::mt19937_64`, so a run is fully
deterministic: the same seed and board always produce the same stream, and any
finding reproduces exactly. Events are weighted so navigation and button presses
dominate and the state-machine events (connect, timer, modal) still fire often.

The fuzzer drives input through the real per-board seams:

- Physical buttons through `UI::simPressButton`: encoder previous, select, next,
  and the left-button long-press that furble uses as its universal back escape.
  The buttons map to the same per-board input devices `initInputDevices` wires
  up, so the UI reacts exactly as it does to a hardware press. The back model is
  the physical long-press (`navigateBack`), so a page that hides the header
  arrow is still leavable. This is deliberately the model-button model, so the
  fuzzer does not re-raise the #113 sim false positive (the Remote page has no
  software Back button but the left button already provides back on hardware).
- Menu navigation through `simScenarioAction("nav <page>")`, which clicks the
  real menu button and records LVGL menu history, and `simulatorHome`.
- Connect, disconnect, shutter, the three one-button gestures, the blind remote
  shutter, intervalometer start and stop, boolean setting toggles through the
  real switch widgets, button-mode selection, the exposure preset stepper, and
  the companion pairing request plus accept/reject.

The fuzzer uses an explicit Apply, Settle, Check, Escape, and Finish phase
machine. Each attempted event gets a small deterministic random settle budget,
and a post-handler hook counts completed LVGL cycles, so LVGL processes each
transition (connect timer, modal raise, page animation) before invariant and
timer-stop reads. Escape is resumable: one modal rejection or back action is
issued per tick, followed by settling, then Escape checks the resulting page
until main is reached. Bounded random values use rejection sampling over the
raw `mt19937_64` output, keeping the event and cadence stream independent of
the standard library's `uniform_int_distribution` algorithm.

### Invariants

After each event's settle phase the fuzzer checks, through `UI::simQueryState`:

- no crash and, under the sanitizer build, no ASan or UBSan error;
- no stale encoder focus (`focus == stale`), the freed-object / use-after-free
  class;
- no stacked modal (`modal_count > 1`), the re-entrancy class from task #32;
- no layout overflow on a must-fit page (`overflow == yes` on home, connected,
  shutter, bulb, bulb run, timer, timer run, display), the narrow-panel class
  from F7. The long settings and diagnostics lists scroll by design and are
  excluded, matching the overflow-sweep split;
- no intervalometer run-state leak after Stop (`interval_state` must reset to
  idle or finished), the PR #112 class;
- no navigation trap: a periodic and end-of-run escape audit clears any pending
  modal, then presses the model back button until the root menu returns; a page
  that never returns to main is a wedge.

Findings print as `FUZZ FINDING [class] step=N page=P event=E detail=...` with
the recent event trail for minimisation, and the run exits non-zero if any hard
invariant failed. The run ends with a `FUZZ SUMMARY` line containing attempted,
observed-delta, no-observed-delta, settled, timer-stop, and liveness counters,
followed by event-class and per-page coverage counts. The two delta counters
sum to attempted, and attempted equals settled after a normal completion.

### Files

- `sim/fuzz.h`, `sim/fuzz.cpp`: the fuzzer. Compiled into the sim by the
  existing `sim/*.cpp` glob in `sim/build.sh` and `sim/CMakeLists.txt`.
- `sim/fuzz_machine.h`, `sim/fuzz_machine.cpp`: dependency-free phase, settle,
  counter, escape, and bounded-random logic used by the fuzzer and host tests.
- `tests/host/sim_fuzz_machine_test.cpp`: pure cadence, exact-settle, escape,
  counter, and deterministic-random regression tests.
- `sim/driver.cpp`: parses `--fuzz`, `--seed N`, `--fuzz-steps N`,
  `--fuzz-verbose` (and `FURBLE_FUZZ_SEED` / `FURBLE_FUZZ_STEPS`), and calls the
  fuzzer from `driverTick` when armed.
- `sim/build.sh`: optional `FURBLE_SIM_SANITIZE` (for example
  `address,undefined`) for the deeper memory hunt, off by default.
- `sim/scripts/run-fuzz.sh`: runs a fixed seed set and fails on any finding.
  Supports `FURBLE_FUZZ_XFAIL_SEEDS` for tracked-but-unfixed bugs, so a real
  finding can be pinned as expected-fail and CI stays green until its fix lands.

## How to reproduce

```
pio run -e m5stick-s3                 # populate .pio/libdeps once
sh sim/build.sh                       # 135x240 (M5StickS3)
FURBLE_SIM_BUILD_DIR="$PWD/sim/build-stickc" \
  FURBLE_SIM_FURBLE_BOARD=FURBLE_M5STICKC \
  FURBLE_SIM_M5GFX_BOARD=board_M5StickC sh sim/build.sh      # 80x160
FURBLE_SIM_BUILD_DIR="$PWD/sim/build-core" \
  FURBLE_SIM_FURBLE_BOARD=FURBLE_M5COREX \
  FURBLE_SIM_M5GFX_BOARD=board_M5Stack sh sim/build.sh       # 320x240

# One seed, verbose event trail:
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build/furble-sim --fuzz --seed 42 --fuzz-steps 600 --fuzz-verbose

# The pinned CI seed set on one binary:
FURBLE_SIM_BIN="$PWD/sim/build-stickc/furble-sim" sh sim/scripts/run-fuzz.sh
```

Deeper memory hunt (slower):

```
FURBLE_SIM_BUILD_DIR="$PWD/sim/build-asan" FURBLE_SIM_SANITIZE=address,undefined \
  sh sim/build.sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-asan/furble-sim --fuzz --seed 1 --fuzz-steps 400
```

## Findings

Budget run: seeds 1 to 20 at 1500 events each on all three panel classes
(135x240 M5StickS3, 80x160 M5StickC, 320x240 M5Stack Core), which is 60 runs and
about 90,000 events, plus the fixed CI seed set (1, 2, 3, 7, 42, 99, 1000,
31337) at 600 events. An address+undefined sanitizer binary
(`FURBLE_SIM_SANITIZE=address,undefined`) is provided and was exercised on
135x240; it reported no ASan or UBSan error on the events it processed. The
sanitizer build is slow, so the native build carries the bulk of the sweep and
the sanitizer build is the deeper on-demand memory check.

No findings. Every invariant held on every seed and board:

- no crash, and no ASan or UBSan error in the sanitizer pass;
- no stale focus, no stacked modal;
- no must-fit-page overflow at any of the three panel sizes;
- no intervalometer run-state leak after Stop;
- no navigation trap: every page the fuzzer entered was leavable by the model
  back button.

This is an expected result on current fork master. The bug classes the fuzzer
targets were each found and fixed by earlier work: the intervalometer run-state
leak and the narrow-panel overflow (plan 86, F1 and F7), the companion modal
re-entrancy (task #32), and the connect-liveness paths. The fuzzer reproduces
none of them because they are fixed, and it found no new ones in this budget.
The value it adds is the standing regression guard below, and the harness is
built to surface a real finding the moment a UI change reintroduces one of these
classes.

The fuzzer's teeth were confirmed by mutation: temporarily marking the
scroll-by-design settings page as must-fit made the overflow invariant fire
immediately with the page, event and step attributed, and the run exited
non-zero. The mutation was reverted; no product or sim code carries it.

### Regression scenarios

Because there are no findings, there are no per-finding seed-pinned scenarios to
add. The regression guard is the fuzzer itself: `sim/scripts/run-fuzz.sh` runs
the fixed seed set and must exit 0. A UI change that reintroduces any targeted
class flips the matching seed to a finding and fails CI. When a future finding
is triaged as real-but-unfixed, its seed goes in `FURBLE_FUZZ_XFAIL_SEEDS` so it
is tracked as expected-fail until the fix PR lands, at which point the runner
flags the unexpected pass and the seed is promoted back to the guarded set.

## CI wiring

`.github/workflows/sim-e2e.yml` gains a "Fuzz the UI" step after the layout
sweep. It runs `sim/scripts/run-fuzz.sh` on all three modeled binaries, the
135x240, 80x160, and 320x240 builds the workflow already produces, at 600 events
per seed. The narrow 80x160 panel is where a layout overflow regression bites
first. The step is inside the existing `sim/**`-triggered job, so no un-globbed
scenario directory is involved. The runner requires exactly one summary per
seed and checks that its requested seed, budget, attempted count, and settled
count agree, and that the observed-delta counters sum to attempted, before
applying pass or expected-fail handling.

## Coverage gaps

Honest list of what this fuzzer does not reach and why:

- Touch tap coordinates. The headless SDL run has no display-backed event pump,
  and the two narrow boards it models are non-touch hardware. The fuzzer drives
  the encoder and the physical-button navigation model instead. Random touch-
  coordinate fuzzing on the Core2 touch panel needs an interactive display and
  is out of scope here.
- Touch versus non-touch widget layouts in one run. The SDL panel attaches a
  mouse touch device, so `M5.Touch.isEnabled()` is true and the UI builds its
  touch-mode remote pages. The non-touch remote widget variants render in a
  different path that this run does not exercise. The button navigation and back
  model are covered regardless of touch mode.
- The Bulb and Bulb-run remote pages are reached inconsistently: the fuzzer
  reaches the connected and shutter pages reliably but the bulb sub-pages depend
  on a specific connected-page selection sequence. The exposure preset stepper
  touches the bulb duration page directly, so the bulb layout is partly covered.
- The button-indicator green focus-outline class (PR #129) is a per-widget
  visual state with no `simQueryState` observer yet. The fuzzer checks focus
  validity and page membership but cannot assert the absence of an unwanted
  highlight on a specific indicator widget. A dedicated observer would close
  this; it is left for the follow-up that lands that feature.
- Scan and connect progress pages are transient (the fake scan completes in one
  tick), so the fuzzer rarely dwells on them. Their layout is covered by the
  scripted connect-flow scenario.
- Deep multi-connect target lists are not modeled; the fake camera is a single
  target.
