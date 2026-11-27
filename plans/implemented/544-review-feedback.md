# 544 - Interval shutter hold decision record, review feedback

Review of PR 544 (`fix/interval-shutter-hold`, head `9c8439e1`, previously
`53d999fc`) against `origin/master`. Verdict: request changes, on citation
accuracy only. The decision itself is right and the load-bearing evidence checks
out: the upstream quote the whole record rests on is verbatim accurate.

Posted as a review comment. The authenticated account is the PR author, so
GitHub refuses a formal request-changes from it.

## What the latest push already fixed

The `371` reference is now correct. `plans/371-fork-network-sweep.md` exists on
master and its action item 2 is exactly the shutter hold item: line 65 starts
"The intervalometer shutter hold, from `zhuimeng2026-hub/furble` (`78a9a7b5`)"
and line 68 says it must be decided per camera family. `plans/373:10` now cites
`plans/371-fork-network-sweep.md:65-68`, which is where it lives. That finding
is closed.

The pointer to the 0 ms follow-up is also good: PR #548 exists
(`fix/shutter-zero-hold`, "fix(interval): release the shutter when the
configured hold is 0", open). Plan 376, now 389 after the plan renumbering,
is not on master yet, which is expected if 548 adds it.

## What passed, do not redo

- **The `gkoh/furble#244` quote is accurate.** Issue title "Future enhancement
  to Timer operation for ASTRO/BULB", comment id 3776391418 by gkoh at
  2026-01-21T06:20:06Z. `plans/implemented/373-interval-shutter-hold.md:61-66` reproduces it
  word for word. This was the blocker check and it passes.
- **30 ms is the shipped default.** `include/interval.h:11`
  (`INTERVAL_DEFAULT_SHUTTER = {30, SpinValue::UNIT_MS}`), and both docs agree:
  `docs/settings-and-controls.md:225`, `docs/wiki/Settings-Reference.md:215`.
- **All ten family rows are right.** I checked each press and release against
  the driver and the cited research row:
  - Fujifilm Basic and Secure, `{01 00}` then `{02 00}` press, `{01 00}` then
    `{00 00}` release, with response: `lib/furble/Fujifilm.cpp:178-195`,
    `lib/furble/protocol/FujifilmProtocol.cpp:144-158`.
  - Canon EOS Remote, `0x8c` then `0x0c`, with response:
    `lib/furble/CanonEOSRemote.cpp:69-79`, constants at
    `lib/furble/CanonEOSRemote.h:44-46`.
  - Canon EOS Smart, `{00 01}` then `{00 02}`, no response:
    `lib/furble/CanonEOSSmart.cpp:194-202`.
  - Nikon Remote, `{02 02}` then `{02 00}`, with response, release a documented
    no-op: `lib/furble/NikonRemote.cpp:89-97`, constants at
    `lib/furble/NikonRemote.h:55-56`, no-op stated at
    `docs/camera-research/fuji-canon-nikon.md:421-423`.
  - Nikon Smart, both no-ops: `lib/furble/NikonSmart.cpp:167-181`.
  - Sony, `0x0901` then `0x0801`, with response:
    `lib/furble/Sony.cpp:173-183`, `lib/furble/Sony.h:124-125`.
  - LUMIX, opcode `04` then `05`, with response:
    `lib/furble/Lumix.cpp:273-284` and `:322-328`, opcodes at
    `lib/furble/Lumix.h:96-97`.
  - Ricoh, `{01 01}` capture with AF, release logs and returns:
    `lib/furble/Ricoh.cpp:448-458`, opcode values at `lib/furble/Ricoh.h:74-82`.
  - DJI Osmo, record toggle, release logs and returns:
    `lib/furble/DJIOsmo.cpp:447-462`.
  - FauxNY, logs only: `lib/furble/FauxNY.cpp:56-62`.
  - The "writes with response except Canon EOS Smart" claim at `plans/373:36-38`
    holds: all six families with a separate release pass a response write except
    `CanonEOSSmart.cpp:196` and `:201`, which pass `false`.
- **The migration argument is confirmed.** `interval_t` has no version field and
  no dirty bit (`include/interval.h:16-30`); the version is implied by `sizeof`
  at `src/FurbleSettings.cpp:414-435`; the only migration widens
  `interval_v1_t` to `interval_t` by record size and fills `wait` at `:421-423`;
  and `save()` writes all four values from one spinner edit
  (`src/FurbleUIIntervalometer.cpp:78-82`). A stored 30 ms really cannot be told
  from a default, so a safe migration and a complete one are mutually exclusive
  here.
- **The 0 ms defect is real, exactly as described.** `src/FurbleUI.cpp:7837-7840`
  calls `lv_timer_set_period` only when `next > 0`. `next` is the shutter value
  at `:7771`, and after a frame the timer still carries the Delay period set at
  `:7778`, so a 15 s delay becomes a 15 s hold. 0 is inside the documented range
  at `docs/settings-and-controls.md:225` ("0 to 999"). The claim at
  `plans/373:178-185` is load-bearing for #548 and it is correct.

## 1. `plans/373:106-111` attributes a citation the fork commit never made (blocking for the record)

The record says:

> The fork commit cites issue `#208` as the reason the value is "long enough for
> the camera to focus and fire".

Checked `zhuimeng2026-hub/furble@78a9a7b5` (2026-11-17T02:00:39Z):

- The commit message gives the rationale with no issue reference: "30ms is too
  short for the camera to acquire focus and start the shutter".
- `#208` appears twice, both in the README hunk, both about connectivity: the
  `[^4]` footnote "Requires furble v3.5.0 or later, see #208" on the X100VI row,
  and the "Fujifilm July 2025 Firmware" section ("Support for the new behaviour
  was added in furble v3.5.0, see #208"), which is the pairing, bonding and
  rotated-address change.
- The quoted phrase does not appear in the commit at all. The nearest wording is
  the code comment added to `src/FurbleSettings.cpp`: "the legacy default is too
  short for the camera to focus and fire".

The conclusion survives and is in fact stronger: the fork gives no source for
400 ms, rather than a wrong one. But the record should not put a citation in
someone else's commit. Suggested replacement: state that the commit asserts the
rationale with no citation, and that its only `#208` references are the Fujifilm
connectivity notes it documents in the same commit. The PR description carries
the same sentence at its line 21 and needs the same correction.

`gkoh/furble#208` itself is described correctly: "Fujifilm firmware update (July
2025) breaks furble connectivity", created 2025-07-23. Only the attribution is
wrong.

The record's other claim about the fork stands: the migration it added (`if
(interval.shutter.value == INTERVAL_LEGACY_SHUTTER.value && ...` inside
`Settings::load<interval_t>`) does rewrite every stored 30 ms unconditionally,
so `plans/373:165-169` is accurate.

## 2. `plans/373:168` cites a `plans/` rule that is not written down

> which breaks the `plans/` rule to keep a user override intact

There is no such rule. `plans/README.md`, `CLAUDE.md` and `docs/` contain no
statement about keeping a user override intact; `grep -i override` over
`plans/README.md` returns nothing on the subject. The nearest real rule is
`plans/README.md:5-6`: "All new behavior is configurable. Defaults keep current
behavior." That rule does support the decision, so cite it, or drop the appeal to
an unwritten rule.

## 3. The description's gate claim is wrong, and the real gate cannot see this default

The description says:

> `settings-table` is the gate that compares `include/interval.h:11` against
> `docs/settings-and-controls.md` and `docs/wiki/Settings-Reference.md`, and it
> passes at 30 ms.

`tests/host/settings_table_test.cpp` reads only two files:
`src/FurbleSettings.cpp` (`:294`) and `include/CLAUDE.md` (`:298`). It never
opens `include/interval.h`, `docs/settings-and-controls.md` or
`docs/wiki/Settings-Reference.md`, and it contains no interval or shutter
reference at all.

The tool that does compare documented defaults,
`tools/check_settings_contracts.py`, runs in `pre_review` and explicitly cannot
resolve this one:

```
WARN: src/FurbleSettings.cpp:685: INTERVAL default could not be resolved to a
scalar (unknown identifier interval); documentation comparison must treat it as
unverified
```

So the header and the two doc rows agree today (I checked all three), but nothing
in the tree enforces that they keep agreeing. The record and the description
should say the agreement was verified by reading, not that a test covers it. If a
gate is wanted, the unresolved-aggregate warning is the place to start, and that
is a separate change.

## 4. Nit: two rows cite line ranges that do not contain the bytes they list

The Fujifilm row (`plans/373:87`) gives `{01 00}`/`{02 00}`/`{00 00}` but cites
`lib/furble/Fujifilm.cpp:178-195`, which only calls `makeShutterFrame`; the bytes
are built at `lib/furble/protocol/FujifilmProtocol.cpp:144-158`. The LUMIX row
(`plans/373:93`) gives opcodes `04`/`05` but cites `lib/furble/Lumix.cpp:273-284`
and `:322-328`; the values are at `lib/furble/Lumix.h:96-97`. The Sony row gets
this right by citing `Sony.h:124-125`. Add the missing lines so every byte in the
table has a source that contains it.

## Gates I ran

On the current head `9c8439e1`:

```
python3 tools/check_doc_citations.py --root .   0 errors of 3202 citations in 181 files
python3 tools/check_ci_workflows.py             pass, 13 workflows
git diff --stat origin/master...HEAD            plans/373 +212, plans/README +1, no code
```

On the previous head `53d999fc`:

```
python3 tools/pre_review.py --base origin/master   13 gates, exit 0, all PASS
```

Worth knowing: none of findings 1, 2 or 3 is caught by `check_doc_citations.py`.
That gate confirms a cited file and line range resolve, not that the cited text
says what the citation claims. Findings 1 and 2 pass it today, so the gate cannot
be used as evidence that the attributions are right.

## Resolution

All four findings were re-checked against head `9c8439e1` and base `baa4ac23`,
not against the base the review was written on. The line references in the
findings above point at `9c8439e1`, the version reviewed; the fixes below moved
some of them, and git history keeps that commit.

| Finding | State | Fix |
| :--- | :--- | :--- |
| 1, the fork commit's `#208` attribution | false positive, kept and made checkable | The commit does cite `#208` on that exact phrase. `include/interval.h` lines 20 to 24 of `78a9a7b5`, read with `gh api repos/zhuimeng2026-hub/furble/contents/include/interval.h?ref=78a9a7b5`, are the comment block whose line 23 is `* Long enough for the camera to focus and fire, see #208.`, above `INTERVAL_DEFAULT_SHUTTER = {400, SpinValue::UNIT_MS}`. The patch for that file is the third `#208` occurrence after the two README ones, so the attribution was right. The record now names the file, the line range and the commit, and adds the reviewer's other point, that the commit message itself carries no reference. |
| 2, the `plans/` override rule | fixed | The appeal to an unwritten rule is gone. The sentence cites `plans/README.md:5-6` instead: all new behavior is configurable and defaults keep current behavior, which is the written rule that supports keeping 30 ms. |
| 3, the settings-table gate claim | fixed in the record and the description | The record now says the agreement between the header and the two doc rows was verified by reading, and why no gate covers it: `tests/host/settings_table_test.cpp:294-298` reads only `src/FurbleSettings.cpp` and `include/CLAUDE.md`, and `tools/check_settings_contracts.py` reports the INTERVAL default as an unresolved aggregate. The description was corrected to match. |
| 4, two rows cite ranges without their bytes | fixed | The Fujifilm row also cites `lib/furble/protocol/FujifilmProtocol.cpp:144-158`, which builds `{01 00}` plus `{02 00}` or `{00 00}`. The LUMIX row also cites `lib/furble/Lumix.h:96-97`, where `CMD_SHUTTER_PRESS` is `0x04` and `CMD_SHUTTER_RELEASE` is `0x05`. |

No other part of the record changed. The blocks the review marked correct were
not restated or re-verified. The follow-up plan was renumbered from 376 to 389
by the plan arbiter, and the references above were updated to match.
