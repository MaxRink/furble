# 167 Fujifilm device name

## Motivation

Bench report, 2026-09-02, Fujifilm X100VI:

> the x100 also only shows up as x100 despite the camera showing a much longer
> device name

furble's scan list and saved list both label the body `X100VI`. The camera's own
Bluetooth screen shows a much longer name. The row therefore carries less
identity than the scan log line right beside it already prints, and two X100VI
bodies produce two identical rows.

## What the camera actually advertises

`lib/furble/Scan.cpp` runs an active scan, so NimBLE merges the scan response
into the same advertised device and `NimBLEAdvertisedDevice::getName()` returns
the complete local name when one is present. The bench capture
(2026-09-02, X100VI in pairing mode, console log from the coordinator's bench
session) shows what arrives:

```
I (1694884) furble: Name = X100VI
I (1694885) furble: Address = 4F:DD:9E:FA:90:E3
I (1694886) furble: Serial = 3143344639
```

The same run also logs the address rotating between advertisements
(`4F:DD:9E:FA:90:E3`, `5B:69:B6:41:27:4B`, `77:23:EF:F5:5B:29`) while the serial
stays fixed, so the serial is the only stable identity on the air.

That rotation has a consequence this change does not address.
`CameraList::match()` dedupes scan results by address, so one body advertising
under three private addresses still lands as three rows, and after this change
all three read `X100VI 1C4F9`. Distinguishing two bodies of the same model needs
serial-keyed deduplication for Secure, which is a separate change to the scan
list and is deliberately out of scope here. This change makes the row say which
body it is; it does not yet make one body produce one row.

So the local name really is the bare model. The longer label the user sees is a
camera-menu string; it is not in the advertisement, and furble cannot know it
before it connects. The honest fix is model plus serial.

The five serial bytes are `31 43 34 46 39`. Every one of them is a printable
ASCII alphanumeric, and read as text they spell `1C4F9`.

What is known is only that: five advertised bytes, rendered as text when every
byte is a printable alphanumeric and as hex otherwise. Whether those five bytes
are the serial the camera itself prints is not established. Body plates carry
eight characters, and [61-camera-compatibility.md](61-camera-compatibility.md)
records libfuji reading a full serial from the Device Information Service
(`0x180a`). The bench gate for this change therefore compares the rendered
`1C4F9` against the X100VI body plate and against DIS `0x2A25`, and the result
is written back here.

The advertisement layout itself (company id `0x04d8`,
type byte, then four token bytes for Basic or five serial bytes for Secure) is
the one already implemented in `lib/furble/protocol/FujifilmProtocol.cpp` and
sourced from [tiredboffin/fffw](https://github.com/tiredboffin/fffw), catalogued
in [61-camera-compatibility.md](61-camera-compatibility.md); the ASCII reading of
the serial field comes from the bench capture above.

Considered and rejected: reading the GATT Device Name characteristic (0x2A00)
after connecting. It cannot label a scan row, which is where the report starts,
and it would add a read to every connect for a string the camera may not expose.

## Design

`FujifilmProtocol` gains two pure helpers:

- `formatSerial()` renders the five bytes. All alphanumeric renders as text
  (`1C4F9`); anything else falls back to the uppercase hex the scan log has
  always printed; an all-zero field, meaning nothing parsed, renders as nothing.
- `deviceName()` appends the rendered serial to the advertised name. It is
  idempotent, so a name that already ends in the serial is returned unchanged.

`FujifilmSecure` composes the name in both constructors. The advertisement
constructor now logs `Advertised name` and `Name` separately so the bench log
still shows the raw local name.

Only Secure gets this. A Basic body's manufacturer data carries a rotating
pairing token, not a serial, so there is no stable identity to append.

Matcher acceptance rules are untouched: `matchesBasicAdvertisement()` and
`matchesSecureAdvertisement()` are not modified, and the new host test asserts
both matchers still accept the peer advertisements.

## Storage compatibility

No stored-format change and no migration. `FujifilmSecure::nvs_t` already holds
`char name[MAX_NAME]` (64) and the five byte serial side by side, and the
composed name is 12 characters for the bench body, so `MAX_NAME` does not move.
Because the saved constructor composes from the stored serial, an entry paired
before this change gains the serial the next time the list loads, without a
re-pair.

### The remembered multi-connect set is keyed on the displayed name

`Settings::MULTISELECT` remembers a multi-connect selection as a list of
displayed names. That keying was written when a name was whatever the vendor
advertised, and it had two defects the moment a name is composed:

- The field was `char name[8][16]`, written with `snprintf` (15 characters), and
  compared with `strncmp` over 15 characters. Two bodies of one model whose
  composed names agree for the first 15 characters were indistinguishable, so
  the set ticked whichever row it met first. `MODEL SERIAL` reaches that length
  easily: the hex fallback alone is 10 characters, so `X100VI DEADBEEF01` and
  `X100VI DEADBEEF02` collided.
- A prefix comparison is the wrong test for identity in any case. It cannot say
  "this is the body I remembered", only "this looks like it".

The fix has three parts:

- `MULTISELECT_NAME_MAX` widens from 16 to 32, which holds a 21 character model,
  a space, and the 10 character hex fallback.
- The store and the compare become `Settings::multiselectAdd()` and
  `Settings::multiselectContains()`, so they cannot drift apart again. The
  compare is over the whole NUL terminated stored string.
- Widening changes the stored record size, so `Settings::load<multiselect_t>()`
  and the SD settings importer both read a record written in the old layout
  through `multiselect_legacy_t` and widen it. Nobody loses a saved selection to
  the upgrade, and an old SD backup still restores.

**The field width is a hard limit, and it fails closed at the store.** A name
that does not fit is refused by `multiselectAdd()` rather than shortened.
Storing it truncated would not have been safe: a 32 character name truncated to
31 is byte-for-byte the name of a different camera called exactly that 31
character prefix, and the whole-string compare would then tick that other body.
Refusing it means the set forgets one camera instead of ticking the wrong one.
The boundary is pinned by test: the widest name that fits round trips, the
narrowest that does not is refused, does not advance the count, and never
claims the camera its truncation would have matched. No camera name furble
composes today comes near 31 characters.

**Downgrading loses the selection, it does not break.** A build from before this
change reads the 257 byte record with a 129 byte buffer. `Preferences::get()`
compares the stored length against the buffer, logs, and returns 0 without
writing anything, so the old `len != sizeof(selection)` branch zeroes the set.
The user re-ticks; nothing overruns and nothing crashes.

**The one-time re-tick still applies.** A selection stored against the old bare
`X100VI` does not match the composed `X100VI 1C4F9`, whatever the comparison,
because the name itself changed. Those checkboxes need ticking once after the
update. From then on the composed name is stable.

An SD settings backup exported before this change carries the old record size.
`FurbleSD`'s importer widens it the same way, so an old backup still restores.

## Panel fit

Camera list rows are the only icon-less menu item, so `addMenuItem()` gave the
label `LV_PCT(100)` width and `LV_LABEL_LONG_SCROLL_CIRCULAR`. That is the
redraw trap the project guide names. Measured in the simulator with the new
`ui.row_scrolling` and the existing `ui.invalidate_count`, holding the saved
list at a fixed state for 2000 ms of virtual time:

| Panel | Long mode | Scrolling | Invalidations / 2 s |
| --- | --- | --- | --- |
| 80x160 | scroll | yes | 115 |
| 135x240 | scroll | yes | 115 |
| 320x240 | scroll | no | 0 |
| 80x160 | wrap | no | 0 |
| 135x240 | wrap | no | 0 |
| 320x240 | wrap | no | 0 |

The row is the saved list entry for the bench body, `FUJIFILM X100VI 1C4F9`,
held at a fixed state. So the composed name pushed both Stick panels into a
permanent animation, roughly one invalidation per frame. The same was already
true of any name that did not fit, `FauxNY Camera` included. Scrolling also
hides most of the name at any instant, which is the opposite of the point of
composing it.

The fix is to stop scrolling camera rows: the icon-less branch of
`addMenuItem()` now uses `LV_LABEL_LONG_WRAP`. A name too wide for the panel
wraps onto a second line instead of animating. `rebuildCamerasPage()` already
made exactly this choice for exactly this reason, so the two list surfaces now
agree. The model-only-on-80x160 alternative was rejected because it drops the
serial on the one panel where two bodies of a model are hardest to tell apart,
and furble has no per-camera detail page to recover it from.

After the change all three panels report `ui.row_scrolling no` and 0
invalidations over the same window, and the complete name is on screen, on more
than one line where it does not fit one. Icon menu rows keep the circular scroll
they always had.

Wrapping has a second consequence the scroll hid. A scrolled row is one line
tall near the top of the list; a wrapped row is taller and fills its width, so
it reaches the navigation indicators the Stick boards float over the page
instead of reserving a navbar for. Measured with plan 165's
`ui.indicator_clearance`, the 80x160 saved list went from `clear` to `overlap`
the moment the row wrapped. `UI::floatingIndicatorReserve()` now returns the
indicator width on those boards and zero everywhere else, and both the camera
list rows and the connected Cameras page rows keep it clear. The Cameras page
needed it too: its row is the composed name plus a status word, which wraps for
the same reason. All three panels now assert `ui.indicator_clearance clear` on
the saved list, the scan list and the Cameras page.

That assertion has teeth on the Stick boards only. `countIndicatorOverlaps()`
clamps every measured area to the page viewport, and on the Core boards the
indicators live in a navbar outside it, so the query reports `clear` there
whatever the rows do. On a touch layout it reports `n/a`, and the scenario seeds
`no_touch true` so it never reads that. The 80x160 and 135x240 runs are the ones
carrying the guarantee.

**The cost on 80x160, and whose name it is.** Reserving 24 px of an 80 px panel
leaves about seven characters a line. The bench body advertises `X100VI`, so the
real composed name is `X100VI 1C4F9` and it renders as two clean lines,
`X100VI` then `1C4F9`. The four-line render with `FUJIFILM` broken mid-word is
the simulator's, because the virtual peer advertises `FUJIFILM X100VI`, which no
Fujifilm body puts in its local name. The scenario therefore measures a name
wider than any real one, which is a conservative gate but an unrepresentative
screenshot. Teaching the peer to advertise the captured local name is a
follow-up, recorded below.

The alternative considered for the reserve was a model-only label on that board,
which drops the serial on the one panel where two bodies of a model are hardest
to tell apart. That is not needed for the real name.

`camera-name-rows.txt` asserts the rendered row text, `ui.row_scrolling no`,
`ui.overflow no` and `ui.indicator_clearance clear` on all three panel classes,
for the saved list, the scan list and the connected Cameras page; the manifest
lists all three boards, so the alternate-board end-to-end CI step picks it up
without a workflow change. It seeds `no_touch true`, because no Stick board
ships a touch panel and the touch layout would measure a list geometry no
shipped device renders. PR #264 has since merged, so the indicator-clearance
query is asserted here rather than argued from row geometry.

The `fuji-secure` topology is new. The existing `fuji` peer advertises as Basic,
so it carries a pairing token and no serial and cannot show this change at all.
`fuji-secure` is the same peer with `config.secure` set, which is what the bench
X100VI actually is.

## Test seams

- `tests/host/fujifilm_device_name_test.cpp`: the pure helpers, the scanned
  camera, the saved round trip, the legacy saved entry upgrade, and the
  unchanged Basic name and both matchers.
- `lib/testing/peer/FujifilmVirtualCamera.h`: the peer's default secure serial
  is now the bench-observed `1C4F9` bytes, so host tests and the simulator both
  derive the name from a realistic advertisement.
- `tests/host/settings_nvs_roundtrip_test.cpp`: the multi-connect keying.
  `X100VI DEADBEEF01` and `X100VI DEADBEEF02` agree over the 15 characters the
  old comparison used and must not be confused; the widest name that fits round
  trips while the narrowest that does not is refused, does not advance the
  count, and never claims the camera its truncation would have matched; a full
  set rejects another entry; a record written in the legacy layout still loads
  with both remembered bodies intact; and an SD backup in the legacy layout
  still imports.
- `sim/scenarios/e2e/camera-name-rows.txt` with the new `ui.row_text` and
  `ui.row_scrolling` queries. It seeds `ble_peers fuji-secure` and `ble_saved
  true`, so
  the virtual Fujifilm peer advertises, the production `Scan` delivers it and
  the production `FujifilmSecure` constructors compose the name. Nothing in the
  simulator restates the expected format; the assertion is the only place it is
  written down. The scenario also connects and visits the Cameras page, because
  `updateCameraRow()` renders the composed name plus a status word and that row
  wraps.

## Follow-up, not in this change

Two layout gaps the wrap change leaves open. Both were found on review of this
branch and neither is fixed here.

- The multi-connect list does not wrap. `addMenuItem()` returns early on the
  checkbox branch, before the long mode is set, so a multi-connect row keeps the
  checkbox default and a composed name clips: `FUJIFIL` on 80x160. The Connect,
  Scan and Delete lists wrap; the multi-connect list is the one camera list that
  still does not, which is also the one list where telling two bodies of a model
  apart matters most.
- Wrapping costs vertical space, so a long-named list overflows sooner. With
  three cameras named like the bench body both Stick lists scroll: 26 px of
  content below the viewport on 80x160 and 41 px on 135x240, measured on review
  before the indicator reserve landed. The reserve makes rows narrower and so
  taller again, so those figures are a floor, not a ceiling. Focus and
  scroll-into-view still work, so every row is reachable; the page is simply
  taller than one screen. `camera-name-rows.txt` measures one saved camera,
  which is the case the bench report is about, and the virtual radio has no
  three-peer topology, so this is not asserted anywhere yet.
- The virtual Fujifilm peer advertises `FUJIFILM X100VI`. The bench capture of
  the local name is `X100VI`, so the peer should advertise that and the scenario
  should assert `X100VI_1C4F9`. It is left as it is here because the wider name
  is the stricter layout gate, and narrowing it belongs with a peer-fidelity
  change rather than this one.
- 80x160 legibility, for a name as wide as the peer's. With the indicator
  reserve such a name wraps over four lines and breaks mid-word. A smaller font
  for camera rows, or a model-only label with the serial shown somewhere else,
  would fix it. The real `X100VI 1C4F9` does not need either.

The advertised five bytes are the only identity available before a connect, and
that is what a scan row can show. After registration furble is connected and
could read GAP Device Name (`0x2A00`) and DIS Serial Number (`0x2A25`) and
persist the better string into `nvs_t::name`, so the saved list shows what the
camera calls itself rather than what it advertises. Sequence that after the
multi-connect keying fix above: it changes stored names again, and the
remembered set must already be keyed on the whole name before a second rename
lands, or the same collision returns wider.

## Implementation state

Implemented as written. Deviations:

- The plan first considered a scenario seed carrying the name as a string. The
  scenario DSL is whitespace separated and rejects trailing words, so
  `ui.row_text` reports whitespace as underscores, following the existing
  `ui.reconnect_count` precedent of avoiding spaces in an asserted value.
- The simulator label went through three forms. It was first a literal, then a
  `fujifilm_name` boolean seed calling the production derivation over a fake
  camera. Rebasing onto PR #261 removed the fake `CameraList` entirely, so the
  scenario now seeds a virtual Fujifilm peer and reads the name the production
  path produces. The seed and its documentation are gone with it.
- Review follow-up: `FujifilmSecure::serialise()` now terminates `nvs_t::name`
  after `strncpy`, matching Lumix, Ricoh and DJIOsmo. A 64 byte name was not
  reachable before and is not reachable now, but this change is what lengthens
  the field, so the guard belongs with it.
- Review round 2: `camera-name-rows.txt` seeds `no_touch true`. Without it CI
  asserted the touch layout, which no Stick board ships.
- Review round 2: the multi-connect keying above. The review offered
  `strcmp` alone as the size-stable option. It was rejected: with a 16 byte
  field, `strcmp` never matches a name over 15 characters, so a body with a hex
  fallback serial would stop being remembered entirely. Widening plus the
  legacy-record migration keeps every existing selection and makes the compare
  exact for every name furble composes.
- Review round 2: the camera row long mode. The review offered accepting the
  scroll with a measurement, or a model-only label on 80x160. The measurement
  was taken and rejected both: wrapping keeps the list static on every panel
  and keeps the serial on every panel.
- Review round 2: the serial provenance wording, in this plan and in
  `FujifilmProtocol.h`, no longer claims the five bytes are how the camera
  prints its serial.
- Review round 3: the indicator reserve. Adding the requested
  `assert ui.indicator_clearance clear` failed on 80x160, and a probe build with
  the old circular scroll proved the wrap change caused it rather than the
  longer name. It is fixed rather than recorded as expected-fail, since it was a
  regression this branch introduced.
- Review round 3: `multiselectAdd()` refuses a name that does not fit instead of
  storing it truncated. The truncated entry was not the safe failure the round 2
  writeup claimed: it is exactly the name of a different camera, and the
  whole-string compare would have matched it.

## Verification

- Host suite: 93 tests. `sim-scheduler` is flaky on this host and sometimes
  hangs rather than failing; master `77aa113f` scores 4 of 6 on the same box, so
  it is not this change. Everything else passes.
- Mutation check on the name derivation test, 11 mutants: 10 killed. One
  survivor, "deviceName ignores an empty serial", is an equivalent mutant: with
  an empty suffix the trailing-suffix comparison is trivially true and returns
  the same name, so the early return is a readability guard rather than
  behaviour. It is kept for clarity.
- Mutation check on the review-round tests, 4 mutants, all killed. Comparing
  the remembered name with `strncmp` over the legacy length again kills "another
  body of the same model is not recognised". Dropping the legacy record
  migration kills "the legacy remembered count survives the upgrade" and both
  legacy body assertions. Relaxing the store-width guard from `>=` to `>`, the
  off-by-one that survived the previous round, kills four: "a name one character
  too long is refused", "a refused name does not advance the count", "an
  oversized name is refused rather than stored truncated" and "an oversized name
  never claims the camera its truncation would match". Restoring
  `LV_LABEL_LONG_SCROLL_CIRCULAR` on a camera row kills
  `assert ui.row_scrolling no` on 80x160 and 135x240.
- Python suite: 133 tests, OK.
- Simulator end-to-end with the CI environment, on the head rebased onto
  `77aa113f`: 83 certified scenarios on 135x240 and 8 each on 80x160 and
  320x240, all passing, including `camera-name-rows` on all three. Certified
  bug-hunt scenarios pass on all three boards (8, 7 and 3), and the watchdog and
  invalid-seed gates pass. The earlier rounds on `f425fd38` saw connect
  assertions fail intermittently on a heavily loaded host; PR #270's UI-thread
  handoff fairness and per-scenario wall-clock bounds cleared them.
- `camera-name-rows` also passes on all three boards with `FURBLE_SIM_NO_TOUCH=1`
  in the environment as well as through its own seed. Forcing that variable over
  the whole certified suite is not a CI mode and is not one here either:
  `home-seven-rows`, `home-seven-rows-large` and `level-spirit` pin the touch
  layout and fail under it on any build, this branch included.
- Coverage floor: measured by CI on the pushed head, and the result is recorded
  in the pull request discussion. On the previous head, based on `f425fd38`,
  `lib/furble/Camera.cpp` reported 503 of 697 lines against a floor of 73.75
  percent on two attempts while master and an earlier head reported 521, with no
  test or scenario missing. That measurement was bistable rather than clearly a
  regression: the earlier head produced both numbers on the same commit.
- clang-format 21 dry run clean, no sdkconfig drift, no em-dashes.
- Firmware compile: not obtained on the build host. Four attempts of
  `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` in a clean host
  worktree, including the prescribed single re-run, each stalled in the ESP-IDF
  component manager at "Reading CMake configuration" with a dead CLOSE_WAIT
  socket to the component registry. The host is on an intermittent uplink and
  another build was contending for the same PlatformIO cache. CI builds all
  twelve board environments on the pushed head.

Owed: the on-device look at the scan list and the saved list on the X100VI. The
coordinator runs that, and it is the merge gate for this PR.

Observed but not fixed here: `sim/scripts/check-doc-tokens.sh` already reported
`nav level_main` as an undocumented token before this branch. That belongs to
plan 153's follow-up, not to this change.
