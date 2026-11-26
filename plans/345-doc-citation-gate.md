# 345 - A gate for `file:line` citations in docs and plans

Status: implemented. The sweep and cleanup figures below were first measured on
this branch against `origin/master` at `bebf7caf`. The merge train then rebased
the branch five times as master moved: onto `bbe4d22c` (#479 and #487), onto
`b549d36c` (the master #506 produced, after #480 and #482), onto `f1fa36a0`
(#501), onto `0c585de9` (the IMU audit #504, after #475, #492, #498, #502 and
#503), onto `f354e3b3` (the datasheet corpus and its reconciliation, #490, #491,
#496 and #517), and onto `c644f39e` (#512 and #507). Every figure and line number is restated for the last of
those bases and rerun there. No CI run is claimed, and no hardware statement is
made or changed. The tree keeps growing under this gate, so a later base moves
these counts again; that is what the gate is for, not a defect in it.

The review after the merge (`plans/511-review-feedback.md`) re-derived the base
figures: the merge base `c644f39e` reports 86 findings, not the 80 this plan
first carried, because three later merges added six. It also found that rule 3
overstated itself where a definition carries a comment block, that the on-line
rule accepts a comment or log-string mention, and that five wrong citations and
one imprecise line pointer in the touched files survived the gate because it
cannot see them. Those are corrected
here, and the figures below are restated for the tree this plan now ships in:
`81139818` plus this remediation, after #518 and #519 moved four more citations.

## Defect

Plans and docs cite code as `path:line` or `path:start-end`. Nothing moves those
numbers when the cited file changes, and no gate read them. A comment edit is
therefore enough to make a document lie, silently.

That happened. #473 (`370c0a99`) rewrote a three line comment in
`src/FurbleUI.cpp` into seven, at the `UI::wakeDisplay()` dwell gate, adding
exactly four lines at 1887 to 1893. Every line below 1893 moved by four, and
nothing noticed. Three citations show what a reader is up against:

- `plans/338-e2e-page-coverage.md` cited `src/FurbleUI.cpp:11340-11357` for
  `legendSelectable()`, which had moved to 11344-11361. The plan was written
  after the shift with the number from before it.
- `docs/power/ble.md` cited `src/FurbleUI.cpp:6741-6752` for the `CameraList`
  clear in `UI::startScan()`; the call is at 9582, so that number never named
  the clear.
- `plans/implemented/303-upstream-sync.md` cited `src/FurbleUI.cpp:10214` for
  the settings roller and `:12771` for the shutter triggers. Neither was ever
  right: the button-mode roller is created at 10392, and the gesture shutter
  runs in `UI::handleGesture` at 12947.

The first is the four line shift. The other two name no symbol of their own, so
no mechanical check could have caught them; they were found by reading, and they
are the reason the checker below states its limits instead of promising that a
pass means the citations are right.

Several docs audits in the same week were spent finding this class of error by
hand. The cost is not the four lines; it is that a reader cannot tell a stale
citation from a current one.

## What a checker can and cannot know

A checker cannot know what a citation meant. It cannot tell whether the cited
line supports the claim around it, and it cannot see drift that lands on a
different but plausible line. Pretending otherwise would be worse than no gate,
because a green check would then be read as "the citations are correct".

So the gate checks only what is mechanical:

1. The path names a file in this checkout.
2. `1 <= start <= end <= lines in the file`.
3. When the citation annotates a symbol, that symbol is on the cited start line,
   or the start line is inside that symbol's definition.

Rule 3 has to be syntactic to stay honest. A sentence may mention three symbols
and cite a fourth; attributing any of them to the citation would manufacture
failures. The gate therefore reads a symbol only when the citation's own clause
ends with exactly one inline code span that looks like an identifier, or when a
parenthesised citation on its own line follows a clause that opens with one.
That is the shape that claims "this symbol is here".

What it cannot catch, stated in `CONTRIBUTING.md` and in the module docstring as
well:

- A citation that names no symbol is checked for path and range only. The two
  plan 303 citations in the defect above are exactly this: the gate passed both
  while they pointed at the wrong code. They were found by reading.
- Drift that stays inside one definition. A four line edit inside a forty line
  function moves every citation in that function and none of them fail.
- Comment lines inserted directly above a cited definition. The span covers the
  comment block attached above it, so those lines are part of the definition and
  the shift is absorbed. The unit tests pin it.
- A cited line that holds the named symbol and contradicts the prose.
- A symbol that appears on the cited line only in a comment or a log string. The
  on-line rule reads the raw line, so a mention counts. The unit tests pin it.
- A symbol the sentence names before another citation, after the citation, in
  bold rather than backticks, or in a table cell holding several citations.
- A bare filename. `Camera.h:203` may name `lib/furble/Camera.h`, a test stub or
  an upstream copy; the data said the guess would be wrong, so the gate does not
  guess. `esp_console.h` uniquely matches `tests/host/console/esp_console.h`
  here, and `esp_wifi.h` matches `tests/host/wifi/stubs/esp_wifi.h`. Resolving
  those would have validated citations against stubs.
- A citation into a tree this checkout does not contain: ESP-IDF, M5GFX, LVGL,
  esptool, newlib, the component manager output. These are declared in
  `tools/doc_citations.json` with a reason and counted as external, not
  verified.
- `#L123` URL fragments, `file:line:column`, and a range written as two
  citations.

## Design

`tools/check_doc_citations.py`, `tests/test_check_doc_citations.py` and
`tools/doc_citations.json`, registered in `tools/pre_review.py` after the
datasheet citation gate #475 added in the same area, with the existing
conventions: `FAIL: <doc>:<line>: <citation>:
<kind>: <detail>` on stderr, one summary line on stdout, exit 0 clean, 1 on a
finding, 2 for an invalid invocation.

The summary separates what was verified from what was not:

```
doc citations in the tree: 2562 line-checked (92 symbol-checked), 100 external,
129 unresolved basename, 0 errors of 2791 citations in 148 files
```

2,791 citations in 375 Markdown files, 148 of which hold citations. 92 get the
symbol check. That is a narrow share and the number is printed rather than
buried: the gate's coverage of the symbol rule is what the syntax allows, and
`--verbose` prints the scan size so a shrinking symbol share is visible.

Three decisions worth recording:

- Every non-ignored Markdown file, not only `docs/` and `plans/`. The eleven
  `CLAUDE.md` files carry 23 citations of their own, and a directory list would
  rot as directories move.
- `--all` is the default and what pre_review runs. The expected large one-off
  backlog did not exist: 2,562 of the citations resolve and 92 get the symbol
  rule, the whole sweep is one pass, and a clean tree means a full sweep costs
  nothing per pull request. `--base REV` still exists for a large or
  mid-migration doc set, and it is deliberately stronger than "changed docs":
  it checks every citation in a changed document *and* every citation anywhere
  that targets a changed file. That second half is the #473 shape, where the
  document is untouched and the source moves.
- A symbol the file defines must be cited at or inside the definition, but a
  range may also cover a one line declaration (`lib/furble/Camera.h:47,606`).
  Without that exception, `Camera` inside a class block would fail for no
  reason; with it, a range that starts above a function body still fails, which
  is the drift shape.

`tools/doc_citations.json` holds two lists, both with a required reason. Eleven
external prefixes (ESP-IDF's `components/`, four IDF paths cited without that
prefix, `esptool/`, `managed_components/`, `stdlib/`, M5GFX's `src/lgfx/`,
M5Unified's `src/utility/`, LVGL's generated `src/lv_conf_internal.h`). The
fourth IDF path, `esp_eth/`, arrived with #482's W5500 PHY citation and is
declared the same way and for the same reason as `esp_hw_support/`, `esp_pm/`
and `esp_driver_spi/`. `src/utility/` covers the M5Unified RTC classes that
#475's datasheet corpus cites by their M5Unified-relative path. A tracked
path always wins over a declared prefix, so `components/icons/` and
`components/esp-nimble-cpp/` are still read from the tree. The `allow` list
ships empty: every finding was fixed,
none suppressed. An entry that suppresses nothing fails, so the list cannot rot
into a blanket, and `--base` skips that check because it only sees a subset.

## Cleanup

The first full sweep, on `bebf7caf`, found 5 citations whose path exists
nowhere here, one range past the end of its file, and, with the symbol rule as
it now stands, 74 symbol findings. That is 80 findings in all, and every one
was fixed. The merge base `c644f39e` reports 86, not 80: three later merges
added six, five in the datasheet pages #490 to #517 added and one in #504's IMU
plan. That split is 78 symbol, 5 unresolved path and 3 out of range, and every
one of those was fixed too. Two groups are counted and not verified, and neither
is a finding: 84 citations written as a bare filename on the first base (128 on
this one), and 79 citations into an external tree there (100 here), covered by
the eleven declared prefixes above.
Nothing was added to the `allow` list.

The five wrong paths, all errors this gate exists to catch:

- `docs/power/soc.md` cited `src/FurbleControl.h`, which has never existed; the
  header is `include/FurbleControl.h` (two citations).
- `plans/29-virtual-test-rig.md` cited line 613 of `lib/furble/Companion.cpp`.
  That file is gone: the GATT callback is `src/FurbleCompanion.cpp:928` and the
  logic is `CompanionService::handleLocation` in
  `src/FurbleCompanionService.cpp:304`.
- `plans/25-multiconnect-ui.md` cited `src/NimBLEClient.h`, the upstream
  layout; the tracked copy is `components/esp-nimble-cpp/src/NimBLEClient.h:65`.
- Two plans cited `control_e2e/freertos_shim.cpp`; the file is
  `tests/host/control_e2e/freertos_shim.cpp`.

The one line past the end of its file was a range ending at 279 of
`lib/furble/NikonSmart.cpp`, a 266 line file, from
`plans/implemented/60-issue-257.md`. The function is `degreesToDMSubMin` at
238-264.

Of the 74 symbol findings on the first base, 45 named a symbol the file defines,
so the citation was repointed at the definition span. On the merge base the
split is 46 defined and 32 not, out of 78. The worst were thousands of lines out:
`UI::batteryUpdate` was cited at `src/FurbleUI.cpp:2932-2992` and is at
11537-11626, and `Control::sendCommand` was cited at
`src/FurbleControl.cpp:183-185` in two plans and is at 581-606. Both are
`plans/implemented/` archives that a reader could not have trusted.

The remaining 29 named a symbol that lives in another file or in a header. Each
was resolved by moving the citation onto the line of the symbol it actually
means, which is the durable fix: `classifyDisconnect()` now carries the citation
instead of the `TRANSIENT` value it returns, `Intervalometer::save()` carries it
instead of the `interval_t` it writes, and `Platform::powerOff()` carries it
instead of `M5PM1::shutdown()`.

Five more citations the gate could not see were wrong, and one pointed inside the
right function at the wrong line. All six are fixed here. Each sits in a file
this PR already touched, next to a citation it did correct:

- `docs/power/soc.md` OS-02 cited `src/FurbleControl.cpp:379-386` for the 50 ms
  control queue receive and the bare continuation `1149` for
  `m_LastRssiSample`. At the note's snapshot commit both were right; the code
  moved to 484 and 1515 and the tick became 250 ms in 797f01fa, so the prose
  said 50 ms and 20 wakes. The section now says implemented at 250 ms. The
  clause named no symbol and the continuation is not parsed, so neither was a
  finding.
- `plans/implemented/76-reconnect-stuck.md` cited `Camera::m_Mutex` at
  `lib/furble/Camera.cpp:28`, which is `#if defined(FURBLE_CONSOLE)`; the lock in
  `Camera::connect()` is at 532. The other copy of that claim, in the next
  paragraph, was corrected to 532 while this one was missed because its clause
  names two symbols (`Camera::connect()` and `Camera::m_Mutex`), so the gate
  checked the path and line only.
- `plans/34-ota-partitions.md` kept "the task created at `src/main.cpp:32`",
  where the file has an `#if`, while `xTaskCreate` is at 517, in the same
  numbered item whose `Control::sendCommand` citation was corrected. Its
  `Control::sendCommand` citation also pointed at a comment inside the function
  rather than the zero-timeout `xQueueSend` at 605.
- `plans/25-multiconnect-ui.md` placed the `updateItems` guard from the scan
  callback at `src/FurbleUI.cpp:943-945`, which is a `BootTimeline` mark; the
  unlock, callback and relock are at 9638-9645.

That is the honest limit in practice: the gate removed 86 mechanical findings,
and reading still found five wrong citations and one imprecise one. None was
silenced through `allow`.

Two citations are worth recording as the honest limit of this gate,
`plans/implemented/303-upstream-sync.md:39` and `:41`. Each names no symbol in
its own clause, so the gate checked the path and the line and passed both, and
both numbers were wrong. They were found by reading and are now 10392 and
12947. A reviewer reading only the gate cannot tell either from a correct
citation, which is why the limits above are part of the module's contract and
not a footnote.

Fourteen more citation tokens changed on lines the gate did not flag: siblings
on a line whose other citation was flagged, the two plan 303 numbers above, and
a few the gate is blind to. The clearest is `docs/power/soc.md`, which cited
`src/FurbleControl.cpp:22` and the bare continuation `:1149` for the
adaptive-power interval, now `:27` and `:1515`. The bare continuation form
(`:1149` with no path) is not parsed, which is documented above.

The merge-train rebase onto `bbe4d22c` moved those files again. #487 added 14
lines above `Control::sendCommand`, 40 above `Control::disconnect` and 43 above
`m_LastRssiSample` in `src/FurbleControl.cpp`, and 12 above the declaration of
`Control::sendCommand` and `TIMEOUT_INFINITE_MS` in `include/FurbleControl.h`.
Six citations this branch had already repointed were repointed again, to 581,
581-606, 960-1175, 139, 100 and 1515, and the sweep and the unit tests were
rerun on that tree. The gate found all six because each one names a symbol. A
citation into either file that names no symbol shifted again with the same
commit and this gate still passes it, which is the limit stated above.

Two later merges moved citations this branch had already fixed, and both
rounds were found by the gate rather than by reading. #482 cited an ESP-IDF
path without the `components/` prefix, so the config now declares `esp_eth/`.
#501 added one line above `GPS::update()`, `serviceSerial()`, `reloadSetting()`
and the `Control::updateGPS()` call site in `src/FurbleGPS.cpp`, so five
citations moved by one line and were repointed to 2325-2341, 2497-2768, 2672
and 3005-3044. The five came from the CI job on the tree merged with
`f1fa36a0`, not from the pre-push run, because the local base predated #501;
that is the number-moving case the `--base` mode exists for.

The moves between `f1fa36a0` and `0c585de9` were the largest, because five
merges landed: #475's datasheet corpus cites two M5Unified RTC sources by
their M5Unified-relative path, so `src/utility/` is now a declared external
prefix; #504's IMU plan cites a range in `sim/FurbleIMUSim.cpp` that runs past
the end of the 127 line file, 109 to 135 where 110-127 is right; and reading
the pinned
M5Unified for the first of those showed a third error the gate could not have
caught, `docs/components/pcf8563.md` citing `RTC_Class::setAlarmIRQ` at
`src/utility/RTC_Class.cpp:52` when the inline method is in `RTC_Class.hpp`
at the same line, which is now cited there. The last two rounds fixed
sixteen: the eleven above, repointed once more by the merges in between, and the
five new pages. Two of the five were real errors rather than drift:
`docs/protocols/camera-remotes.md` cited the wrong six lines of
`lib/furble/protocol/AdvertisementProtocol.h` for every vendor row, and
`docs/protocols/companion-gatt.md` put the `static_assert` at 169 where the four
size assertions are at 181 to 184. Those rows name no symbol of their own beyond
the last one, so the gate saw only the out of range row and the `static_assert`
row; the rest were found by reading the header while fixing those two.

## Verification

```
python3 tools/check_doc_citations.py
doc citations in the tree: 2562 line-checked (92 symbol-checked), 100 external,
129 unresolved basename, 0 errors of 2791 citations in 148 files

python3 -m unittest tests.test_check_doc_citations
Ran 33 tests

OK
```

The 86 findings come from the same command run against the merge base, with
this branch's `tools/check_doc_citations.py` and `tools/doc_citations.json`
copied in so the script exists there and the declared external prefixes apply:

```sh
git worktree add --detach /tmp/citations-base c644f39e
cp tools/check_doc_citations.py tools/doc_citations.json /tmp/citations-base/tools/
(cd /tmp/citations-base && python3 tools/check_doc_citations.py)
```

```
doc citations in the tree: 2511 line-checked (94 symbol-checked), 99 external,
126 unresolved basename, 86 errors of 2736 citations in 144 files
```

The same commands against `bebf7caf`, the base the fixes were first measured on,
report 80 errors of 1660 citations in 98 files, 1497 of them line-checked and 84
symbol-checked. `bbe4d22c` reports 80 of 1665 in 99 files, `b549d36c` 80 of 1715
in 104 files, `f1fa36a0` 80 of 1729 in 105 files, `0c585de9` 81 of 2037 in 128
files, `f354e3b3` 86 of 2735 in 143 files and `c644f39e` 86 of 2736 in 144 files. The first four bases report the
split 74 symbol, 5 unresolved path and 1 out of range, and the same 80 citation
sites: only the "defines at" line numbers in the details moved. `0c585de9` adds
one out of range finding, from #504's IMU plan. `f354e3b3` adds six: five in the
pages the datasheet corpus added (`docs/components/at6668.md`,
`docs/protocols/camera-remotes.md`, `docs/protocols/companion-gatt.md`,
`docs/protocols/gnss-nmea.md`, `docs/protocols/gnss-ubx.md`) and #504's. Every
one is fixed here.

The unit tests pin both halves: the four line drift above fails, and a citation
of a line inside a definition, of a comment above a declaration, and of a
symbol the file only uses all pass. Five tests pin the documented blind spots
(no named symbol, two named symbols, a plain word in a code span, a comment
mention that satisfies the on-line rule, and a comment inserted above a cited
definition) so that a future change that starts failing them is a change to the
gate's contract, not a silent regression. Two tests pin `--base`: a source edit
that moves a symbol fails an untouched document, and a stale `allow` entry is not
judged there.
