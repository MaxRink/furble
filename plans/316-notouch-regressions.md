# 316: physical-button layout suite regressions

## Motivation

`sh sim/scripts/run-notouch.sh` failed on master `8f0fd5be` and on the plan
315 branch (#415). CI runs it on all three panel binaries. This PR stacks on
#415 and makes the suite green on 80x160, 135x240 and 320x240.

Full failure list before the fix, measured on the #415 head `6af88751`:

| Scenario | Panels | Failure |
| --- | --- | --- |
| `bughunt/cancel-sweep-fauxny-ui` | all three | `clock.ms` 24,925 against `<= 24000` |
| `bughunt/about-scroll-large` | 80x160 | `ui.diag.about.reset_visible` `no` |
| `bughunt/page-matrix` | 80x160 | `feedback_events` `ui.scroll_bottom` 83 |
| `bughunt/stick-notouch-layout-80` | 80x160 | Sensors `ui.overflow` `yes` |
| `e2e/hw-profile-stickc` | 80x160 | `mem.component.lvgl_draw` 2560, model 800 |
| `e2e/hw-profile-stickc-plus` | 80x160 | `mem.component.lvgl_draw` 4320, model 2160 |
| `e2e/hw-pmic-plus2-no-pmic` | 80x160 | `mem.component.lvgl_draw` 4320, model 2160 |
| `e2e/hw-profile-headless` | 135x240 | `mem.component.main_stack` 8192, model 16384 |
| `e2e/hw-profile-waveshare` | 135x240 | `mem.component.main_stack` 8192, model 16384 |

## Root causes and fixes

### Cancel sweep: the control task slept through every drain

Commit `797f01fa` (P-013) raised the control task's queue wait from 50 ms to
250 ms on the claim that commands wake it at once. Reaping the drain after a
cancel is not a command. `Control::disconnect()` moves the stopped targets
into the drain set on the UI task and returns. The control task is then
blocked in `xQueueReceive()` and reaps them only on its next idle tick. Each
cancel held `control.zombies` above zero, and the connect gate shut, for up
to 250 ms.

Measured per cancel on the S3 binary, from `action cancel` to
`control.zombies 0`: 190 to 260 ms before the fix, 20 ms after. Fourteen
cancels account for the 2,630 ms. The run took 24,925 ms and now takes
22,295 ms on all three panels. The script's own waits are 18,820 ms and the
final FauxNY connect is 2,500 ms, so the 24,000 ms bound has about 1,400 ms of
slack for fourteen cancels. It was right; the bound is unchanged.

Fix: the interactive `disconnect()` path posts one `CMD_ERROR` to the control
queue after handing the targets to the drain. It carries no work. `IDLE`,
`CONNECT` and `CONNECTING` ignore it, and `ACTIVE` now skips it rather than
logging it as an invalid command. The 250 ms idle tick is kept.

### Feedback Events: a stale mid-gesture pass ran after the settle

Bisected to `55226fd6` (fix(ui): keep the deferred scroll pass non-settling).
`action scroll bottom` raises `LV_EVENT_SCROLL` and then
`LV_EVENT_SCROLL_END`. The SCROLL frame queues `legendBandDeferred()`. The
SCROLL_END handler settles the page and pins the bottom, and clears the
pending flag, but the queued async call still runs afterwards. It re-reads the
rows at their settled positions, changes their widths again and, since
`55226fd6` made it non-settling, no longer re-pins the bottom. On 80x160
Feedback Events that left 83 px below the viewport.

Fix: the SCROLL_END handler cancels the queued pass with
`lv_async_call_cancel()` when it clears the flag. Mid-gesture passes are
unchanged.

### About: the Reset row could never be fully visible on 80x160

This one never passed in this layout. At `3f02599d`, the commit that made
`reset_visible` require full containment, the 80x160 build already failed it.

An info page is one full-height container, so it intersects the floating
legend band at every scroll position and is always narrowed by the 28 px
reserve. On the 78 px page that leaves a 50 px container, and its 8 px
theme padding on each side left the labels 34 px. "Reset:\nPower on" broke
over five lines, taller than the 85 px viewport, so the label could not be
fully shown at any scroll position. "Version" broke as "Ver", "sion".

Fix: the five info pages (About, Device info, Power state, BLE, IMU data) now
build their container through `UI::addInfoPage()`, which marks it. While the
legend reservation narrows a marked container, `applyLegendBandWidths()`
drops its right padding, because the reservation already keeps the text clear
of the legend. The padding returns when the container leaves the band. The
labels gain 8 px on the Stick boards; the Core has no floating legend and is
unchanged. On 80x160 at Normal, About scrolls 503 px instead of 611 px and
the Reset row is fully visible at the bottom.

### Sensors on 80x160: the fit line contradicted the documented contract

`stick-notouch-layout-80.txt` asserted `ui.overflow no` on Sensors while its
own header and sim/CLAUDE.md say the page scrolls on this panel by design and
must keep complete labels at the top, bottom and restored top.
`addSensorsMenu()` says the same since the Motion Engine entry joined it.
Plan 168 recorded a 7 px scroll and kept the line an `xassert`. `6a443c52`
promoted it to `assert` in a sweep that also left the header saying the line
was still an `xassert`. It has failed ever since: 51 px at `3f02599d`, 18 px
now.

Fix: the Sensors block asserts the documented contract as hard asserts:
`ui.overflow yes`, no cut labels and no overlaps at the top, the bottom and
the restored top, both scroll extents reachable and the indicators clear.
`touch-sensors-default-fit` keeps the strict fit in the touch lane. The page
geometry was not changed: fitting would mean shrinking the selected text size,
which the layout rules forbid.

### Memory profile constants

#413 derived the board budgets in `sim/memory_profile.cpp` from code and left
five hw-profile scenarios on the old table. The code is the authority:

- `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` in `sdkconfig.esp32-s3-headless` and
  `sdkconfig.waveshare-s3-eth`.
- `UIMemory::drawBufferBytes()` and `singleDrawBuffer()`: the 80 px StickC uses
  one 80 x 5 x 2 = 800 byte buffer, and the 135 px Plus and Plus2 one
  135 x 8 x 2 = 2,160 byte buffer.

The five scenarios now expect those values.

## Deviations and open items

- `bughunt/timer-overflow-no-timed-wake.txt` fails in the touch-layout
  bug-hunt step on 80x160 and 320x240 (`ui.overflow yes`). It reproduces on a
  build of `6af88751` without this PR. It is outside `run-notouch.sh`, which
  skips touch-guarded scenarios, and is left for a separate fix.
- No bound was raised. The Sensors fit line was replaced by the scroll
  contract its file and sim/CLAUDE.md already documented; every other change
  is a code fix or a corrected constant.

## Docs screenshots

`sim/scripts/docs-capture.sh` was rerun for the S3 and StickC. Only
`docs/img/diag-power-state.png` changed because of this PR: "Light sleep: off"
now fits one line. The StickC and S3 galleries are unchanged. The flat set
still stops at `camera-pair-reject` (`VALID_NO_EFFECT`), the master defect plan
315 records, so the dark, scroll and text size sets were captured with that
step tolerated. Their remaining differences (`connecting`, `connected`,
`remote`, `camera-pairing`, the device info heap values, the About heap rows
in `scroll/`, and the `textsize/large` header title from plan 315) do not come
from this PR and are not committed.

## Validation (simulator only)

- `run-notouch.sh`: all scenarios pass on 80x160, 135x240 and 320x240. The
  remaining `XFAIL` lines are the documented `xassert` gaps.
- `run-e2e.sh`: S3 206 of 206, StickC 65 of 65, Core 69 of 69.
- Touch-layout certified bug-hunt: S3 all pass; StickC and Core fail only
  `timer-overflow-no-timed-wake`, see above.
- `sh tools/run_sanitized_host_tests.sh --clean`: pass.
- Hardware validation is deferred in sim-only mode.
