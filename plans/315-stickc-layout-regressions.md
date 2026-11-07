# 315: M5StickC layout regressions

## Motivation

Three certified e2e scenarios failed on master at `8f0fd5be` on the 80x160
M5StickC simulator only. They passed on the 135x240 S3 and the 320x240 Core
builds, so each defect hid behind the wider panels.

| Scenario | Assertion | Master result |
| --- | --- | --- |
| `e2e/statusbar-stability.txt` | `ui.battery_drift 0` after connect | `-4` |
| `e2e/camera-name-rows.txt` | `ui.overflow no` on Cameras | `yes` |
| `e2e/timezone-auto-gps.txt` | `ui.cut_labels 0` on Time | `1` |

`camera-name-rows` came with `60ccccdf` and `timezone-auto-gps` with
`f10b35ef`. Neither failure reproduces on the S3 or Core panels.

## Root causes (measured on the 80x160 simulator)

### Header battery drift

The header content box is 76 px wide with a 2 px column gap. At the idle
baseline the row held the rig title, the zero-width flex-grow spacer, the
reconnect icon, the GPS icon and the battery. Each zero-width item still costs
a flex gap: `0 + 2 + 0 + 2 + 24 + 2 + 24 + 2 + 24 = 80`. The battery sat at
x=56..80, past the content edge. After connect it sat at x=52..76.

The GPS icon was part of that baseline only by accident. It is created
visible, and the 500 ms icon timer hides it when GPS is off. The scenario
samples at 400 ms, before the first tick, so the battery was measured in a
transient overflowing state that every board shows for half a second at boot.

### Cameras page overflow

`rebuildCamerasPage()` padded each camera label right by the legend reserve.
The page-load handler then ran `reserveLegendColumns()`, which narrows every
direct label child level with the floating legend by the reserve plus its
gap. The label lost the column twice and kept a 22 px text box. "FUJIFILM"
broke mid-word, the row grew to six lines, and the page overflowed.

### Time page cut label

The Time page container was `LV_PCT(100)` high. On 80x160 the zone label,
roller, Local and UTC rows need more than the viewport, so the container became
a nested scroller and drew the UTC row below its own box (y=132..153 against a
box ending at y=138). The info rows are focusable, so the row could be reached
by focus scrolling, but on entry it was cut at the bottom edge.

## Fix

- The header row is end aligned and the spacer is gone. A shown title still
  grows into the free space. A hidden title no longer leaves a zero-width item
  that costs a gap, so three icons fit the 80 px header exactly. When the icons
  overflow the row, the overflow leaves the left edge, not the battery.
- The GPS icon visibility is set once at construction through the same
  `updateGpsIcon()` helper the icon timer uses, so it never flashes at boot.
- `rebuildCamerasPage()` drops the extra label padding and calls the shared
  `reserveLegendColumns()` itself, so the timer-driven rebuild is reserved too.
- The Time page container is content sized, so the page scrolls, as the 80x160
  Text size page already does. The rows are start aligned, so the wider panels
  render the same.

## Deviations and open items

- The rig title is always shown in the simulator. With the title shown and
  three icons (GPS on plus the reconnect icon), the 80 px header still needs
  78 px. LVGL does not count a grow item's margin, so the title gap cannot be
  folded away without a separate icon container. The existing
  `xassert board-varies` lines keep recording that 2 px drift on 80x160, and the
  four-icon state cannot fit 80 px at all.
- No assertion was softened or demoted.

## Simulator build fix

`FURBLE_SIM_RIG=0` builds failed on master after #412: the non-rig branch of
`sim/CompanionRigTransport.cpp` uses `HostAllocScope` but only the rig branch
included `heap_count.h`. CI builds the rig, so only the docs capture saw it.
The non-rig branch now includes the header.

## Docs screenshots

`sim/scripts/docs-capture.sh` regenerated the gallery. Committed changes:

- every `docs/img/stickc/` image. The header title gets the whole free width
  instead of half of it, so "si" reads "sim". A master build of the same
  script reproduces the committed master images byte for byte, so the change
  is this PR's.
- `settings-time.png` and `dark-settings-time.png`. The content-sized
  container now sits above the floating legend band, so the rows keep the full
  width and fewer lines wrap.
- The Core gallery is unchanged.

Not committed: `connecting`, `connected`, `remote`, `camera-pairing` and
`diag-device-info`. They differ on master already. The device info heap value
follows the #412/#413 heap model, and the non-rig flow leaves the companion
pairing box over the connect captures, then `camera-pair-reject` returns
`VALID_NO_EFFECT` and stops the script. That is a separate master defect.

## Validation (simulator only)

- The three target scenarios pass on 80x160.
- `run-e2e.sh`: 320x240 Core 69 of 69. 80x160 StickC 62 of 65 and 135x240
  S3 204 of 206; every failure is a `hw-profile-*` or `hw-pmic-*` memory
  constant that disagrees with `sim/memory_profile.cpp` on master (#413).
- `run-notouch.sh`: the remaining failures reproduce on an 80x160 master
  build: `cancel-sweep-fauxny-ui` (24,925 ms against a 24,000 ms bound; it
  also fails on the S3 and Core runs), `about-scroll-large`, `stick-notouch-layout-80` (Sensors overflow)
  and the `hw-profile-*` constants. `page-matrix` on 80x160 failed on master
  at the Time page cut label; it now passes that point and stops at
  `feedback_events` scroll bottom 83, which a master build also reports.
- `python3 tools/pre_review.py --base origin/master`: 8 gates pass.
- `sh sim/scripts/check-doc-tokens.sh`: pass.
- Hardware validation is deferred in sim-only mode.
