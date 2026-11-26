# 328 - Reference documentation audit: settings and console

## Implementation state

2026-11-13: implemented on `docs/settings-console-accuracy` and amended after
review rounds 1 to 3 returned changes required. Twenty claims were reported against
`docs/settings-and-controls.md`, its `docs/wiki/` mirror,
`docs/console-commands.md`, its `docs/wiki/Console-Commands.md` mirror, and
`README.md`. Each one was re-checked against the code before editing, and all
twenty held in substance. Two carried a wrong substantive detail in the report
(item 1 named the wrong cameras for the feature table, item 12 named the wrong
set of disagreeing settings) and three carried stale line numbers (items 15, 16
and 20); the Deviations section and the table record what the code actually
says.

Review round 1 found three blocking statements (rows 12, 14 and 15 of the table)
and fourteen non-blocking items. Rows 21 to 24 are the documentation fixes the
non-blocking items required, and the Review rounds section records every finding
with its resolution and evidence. No firmware, test, screenshot, or simulator
scenario changed.

Review round 2 found two blocking statements (rows 8 and 24, the second one
matching the `README.md` paragraph) and five non-blocking items. One finding
needed doc work of its own and is row 25; the rest were two wrong line
references, a wrong menu-entry count, an "untouched" claim over files the branch
edits, and overstated closure claims. Rows 1, 8, 15, 22, 24 and 25, the
Deviations section and the Not changed section carry the corrections, and the
Review rounds section records all seven findings. No firmware, test, screenshot,
or simulator scenario changed.

Review round 3 found two non-blocking items, both in this plan: the wiki heading
line in table row 25 and the settings diff path count in the Gates section. Both
were corrected against a `grep -n` of the branch tree and a settings gate run. No
firmware, test, screenshot, or simulator scenario changed.

## Motivation

The settings and console references are the two documents a developer reads
before touching the menu or the USB console, and they were drifting from the
source in ways a reader cannot detect: duplicated rows for one page, a setting
implemented in `src/FurbleSettings.cpp` with no documented row, options listed
with the wrong count, and a sentence that attributes one map to two different
behaviours. A wrong value list is worse than a missing row, because it looks
verified.

## Findings

Line numbers are on `origin/master` at `4af87727`, the commit this branch was
cut from. Commits merged since leave the settings and console pages,
`README.md` and every code file cited here unchanged; both walkthroughs were
edited upstream, so row 25 counts its lines on this branch.

| # | Claim | Truth | Code | Fix |
| ---: | :--- | :--- | :--- | :--- |
| 1 | `README.md:541` limited location tagging to Fujifilm, Sony and Ricoh. | Every camera the feature table marks in its GPS column implements `updateGeoData()`: Fujifilm, Canon EOS (Smart), Ricoh, PENTAX, Sony ZV and Panasonic LUMIX. The table crosses the GPS cell for Canon EOS (Remote), Nikon (Remote), Nikon (Smart) and DJI, and Nikon (Smart) still implements the location write. | `README.md:150-161` (GPS column), `lib/furble/CanonEOSSmart.cpp:217`, `lib/furble/Lumix.cpp:340`, `lib/furble/Ricoh.cpp`, `lib/furble/Sony.cpp:177`, `lib/furble/Fujifilm.cpp:323`, `lib/furble/NikonSmart.cpp:183` | The paragraph names the feature table and its GPS column, lists that set, and states that the table is the support statement: Nikon (Smart) implements the write, its row has Scanning ticked, and Pairing, Shutter Release, Focus and GPS are crossed. |
| 2 | `docs/console-commands.md:259` named the receiver baud field `baud`. | `gps` prints `detected_baud`. | `src/FurbleConsole.cpp:1407` | Field name corrected. |
| 3 | `docs/settings-and-controls.md:138` and `:142`, wiki `:138` and `:142` had two `Assisted start` rows with different value lists. | The roller has three options and the menu adds one row. | `include/FurbleUI.h:853-854`, `src/FurbleUI.cpp:9767-9775` | The two-option row is deleted; the three-option row stays. |
| 4 | Same files `:141` and `:147` duplicated `GPS Data`. | The page is added once, with one row in the menu. | `src/FurbleUI.cpp:9811` | The thinner duplicate is deleted in both files. The kept row is the richer one, with the receiver detail and the 80x160 note, and review round 1 aligned the wiki copy with it, so both files now carry the same text. |
| 5 | The GPS tables omitted `Motion Adaptive`. | Wire id 66, key `gps_motion`, bool, default false, and a switch row right after `Assisted start`. It gates the IMU stationary detector and logs each transition; no receiver policy reads the verdict yet. | `src/FurbleSettings.cpp:51`, `:171`, `:709`, `src/FurbleUI.cpp:9776`, `:3012-3026`, `src/FurbleGPS.cpp:2356-2383` | Row added to both tables, in menu position. |
| 6 | `docs/console-commands.md:255` said `gps` prints "error counts". | It prints `chars` and `sentences`. | `src/FurbleConsole.cpp:1416-1417` | The sentence names the two counters. |
| 7 | The Power tables omitted `Battery Saver` and `Auto off while charging`. | Both are rows in `Settings > Power`. Battery Saver is the opt-in one-switch profile and is inserted first; Auto off while charging is inserted before `Auto off` and exists only where the board can power off. | `src/FurbleUI.cpp:11677`, `:11699-11704`, `:11796-11797`, `src/FurbleSettings.cpp:86-87`, `:721`, `:660-661`, `include/FurbleAutoOff.h:9-19` | Both rows added to both tables, describing the shipped hint and the charging opt-in. |
| 8 | `docs/settings-and-controls.md:3` and wiki `:3` claimed to be the complete reference. | No `Settings::` reference exists in `src/FurbleUI.cpp` for sixteen setting types in `src/FurbleSettings.cpp`. Five of them have no menu row at all, `WiFi` 51, `WiFi SSID` 52, `WiFi Passphrase` 53, `NTP` 54 and `NTP Server` 55, and are written by the console, companion, WebUI, SD import/export and provisioning instead. The rest already have a row or a paragraph on the page: `COMPANION_PASSWORD` 47, `GPS_GNSS` 48, `GPS_PSM` 49, `GPS_ANA` 50, `MQTT` 56, `MQTT_URI` 57, `MQTT_USER` 58, `MQTT_PASS` 59, `MQTT_BASE` 60, `MQTT_HA` 61 and `WEB_UI` 62. | `src/FurbleSettings.cpp:55-57`, `:67-68`, `:92-100`, `:109-113`, `:776-783`, `src/FurbleSD.cpp:248-250`, `:292-293`, `src/FurbleProvision.cpp:72-73`, `:132-134`, `src/FurbleCompanionService.cpp:680-681`, `:1158-1167`, `src/FurbleWebUI.cpp:1105-1122`, `:1742-1751` | Two paragraphs added to "Settings that live outside the Settings menu": the five WiFi and NTP settings with their wire ids, keys and defaults, every write path (console, companion, authenticated WebUI, SD import and export, provisioning) and a link to the console reference, then one sentence that points the other settings without a `Settings` page row at the rows and paragraphs they already have. Rewritten in review round 2: the first version opened "Five settings have no menu row", which is false for the page as a whole. |
| 9 | `docs/console-commands.md:42` omitted `power off`. | `power off` queues a power-off request and reports `not supported in this build` under `FURBLE_NO_DISPLAY`, because `doPowerOff()` runs on the UI task. | `src/FurbleConsole.cpp:1908-1932`, registered at `:3606` | The `power` row names `off` and the display-less refusal; the README command block names `off` too. |
| 10 | The command summary table omitted `ntp`, `provision`, `imu`, `wifi`, `interval`, `bulb` and `display`. | All seven are registered. | `src/FurbleConsole.cpp:3596-3656`, in particular `:3602`, `:3604`, `:3611`, `:3614`, `:3616`, `:3636-3638` | Seven rows added to both the page and the wiki mirror, in registration order. |
| 11 | The README serial console block omitted `provision`. | `provision <hex\|base64 TLV blob>` decodes a provisioning bundle and applies its settings. | `src/FurbleConsole.cpp:1169-1203`, registered at `:3616` | One line added to the block, in the same column as its neighbours. |
| 12 | The Applies column was attributed to `Settings::appliesImmediately()`, "which is also what the serial console reports". | There are two maps and they disagree. `Settings::appliesImmediately()` answers for the UI. The console prints its own `appliesWhen()`: `immediately`, `on next connect` (CONN_SAVER only) or `on reboot` (default). They differ for Button Mode (console `immediately`, settings restart) and for TX Power, WiFi, NTP and NTP Server (settings immediate, console `on reboot`). Five column rows come from runtime behaviour instead of either map: Brightness and Inactivity (the Display page applies them), Button Mode (re-read on every shutter press) and Connection power save and Sleep while connected (read when a connection is made). | `src/FurbleSettings.cpp:141-244`, `src/FurbleConsole.cpp:353-418`, `src/FurbleUI.cpp:11198-11224`, `:11248-11261`, `:2214-2218`, `src/FurbleControl.cpp:255`, `:1428-1431` | The bullet names both maps and their source files, the five rows that disagree between the maps, and the five rows the column takes from runtime behaviour. Review round 1 added Button Mode to that second list. |
| 13 | The gps receiver value list omitted `unknown`. | `GPS::receiverStateName()` returns `unknown` for `UNKNOWN` and as its fallback. | `src/FurbleGPS.cpp:3156-3168`, printed at `src/FurbleConsole.cpp:1406` | `unknown` added to the list. |
| 14 | `docs/console-commands.md:5-6` and `README.md:342-343` said no release build contains the console. | The six release board images carry no console, and the non-debug `waveshare-s3-eth` env drops the one its headless parent passes down. The release workflow also builds and publishes the `-debug` image for five of the six boards (matrix variant `-debug`, `m5stack-core-debug` excluded), every `-debug` env sets `-DFURBLE_CONSOLE=1`, and `esp32-s3-headless` is a release-profile env with the console that the reproducible workflow builds. | `platformio.ini:70`, `:88`, `:108`, `:124`, `:142`, `:167`, `:183`, `:204`, `.github/workflows/release.yml:16-30`, `:84-94`, `.github/workflows/reproducible.yml:71`, `tools/reproducible_build.py:524` | Both sentences say no release board image contains it, name the five published `-debug` images that do (`m5stack-core2`, `m5stick-c`, `m5stick-c-plus`, `m5stick-s3`, `waveshare-s3-eth`), keep the headless hub image, and say only the non-debug `waveshare-s3-eth` env drops it. The README carries the same facts. |
| 15 | The quick reference omitted the `Level` top-level entry and the `IR` position. (The reported lines 62 to 64 were wrong; the top-level menu paragraph is at lines 24 to 25.) | The home menu is built as Connect, Scan, Delete, IR, Settings, Level, Off. On 320x240 the same entries use a four-column, two-row grid: Connect (0,0), Scan (1,0), Delete (2,0), Settings (3,0), IR (0,1), Level (1,1), Off (3,1). `IR` is shown only with an IR LED and Infrared enabled; `Level` is hidden when the IMU is off or its sensor is absent. | `src/FurbleUI.cpp:3391-3408`, `:416-421`, `include/FurbleUI.h:953-958`, `src/FurbleUI.cpp:10435-10457` | The paragraph gives the navigation order, then the same entries with their grid cells on the 320x240 boards, plus both visibility conditions, in the page and the mirror. Review round 1 split order from position. |
| 16 | `docs/settings-and-controls.md:148` documented `GPS Satellites` next to `Satellites`. The reported label and registration lines were off by a few lines. | The label is `Satellites` and the page is added once. | `include/FurbleUI.h:843`, `src/FurbleUI.cpp:9813`, `:10300-10374` | The `GPS Satellites` row is removed and the single `Satellites` row carries its accurate description in both files. |
| 17 | `Text size` defaulted to `Normal` with three values on every board. | The 80x160 M5StickC defaults a fresh device to Small and offers only Small and Normal. It explains the missing option with "Large needs a bigger screen". Every other board keeps three sizes and Normal. | `include/FurbleTextSize.h:28-32`, `src/FurbleUI.cpp:11466-11485`, `src/FurbleSettings.cpp:631-636` | Default, value list and note made board conditional in both files. |
| 18 | `docs/console-commands.md:145-149` listed the reset reasons as a closed set. | The console name table also returns `unknown`, `external`, `deepsleep`, `sdio`, `efuse` and `other`, and the crash record uses the same names by numeric value. | `src/FurbleConsole.cpp:2423-2463`, `include/FurbleCrashRecord.h:125-162` | List completed, with the `other` fallback named. |
| 19 | `docs/console-commands.md:181` said `settings list` prints every setting. | It skips `COMPANION_PASSWORD`, `settings get` and `settings set` refuse it, and `companion password status` reports only `set`, `unset` or `unavailable`. | `src/FurbleConsole.cpp:954-962` (list skip), `:989-991` (get and set refuse), `:1068-1074` (companion password status) | The bullet names the skip and the command that reads the setting. |
| 20 | The `Screen off` value list used the wrong option labels. (The reported line 117 was wrong; the row is at line 86.) | The button-board roller is `Dim`, `Off`, `Off, remote on`, `Dim, then off`; the touch-board roller drops the remote-on entry. | `include/FurbleUI.h:761-762`, `src/FurbleUI.cpp:11273-11275` | Both sets are now quoted exactly, per board family. |
| 21 | The `Time zone` rows listed `Auto (GPS)` second. | The roller builds its options from the curated `ZONES` table in order and appends `Auto (GPS)` last: 39 zones plus Auto. | `src/FurbleTimezone.cpp:269-280`, `:31-80`, `include/FurbleTimezone.h:104` | Both rows list the zones in table order and put `Auto (GPS)` last. Found in review round 1 (N13). |
| 22 | `docs/ui-walkthrough.md` and `docs/wiki/UI-Walkthrough.md` listed a six-entry home menu, and `README.md` a five-entry one. | The menu has seven entries: IR after Delete and Level after Settings. Both walkthroughs already carried `IR` and were missing only `Level`; the README was missing both. | `src/FurbleUI.cpp:3391-3408`, `:9675-9691` | All three lists carry seven entries in navigation order, and both walkthroughs say the Level entry is also on the home menu. Found in review round 1 (N6 to N8). |
| 23 | The README console block's `power` line omitted `off`, and no page said `off` is refused on a display-less build. | `cmdPower` handles `off` and returns `not supported in this build` under `FURBLE_NO_DISPLAY`. | `src/FurbleConsole.cpp:1913-1932` | The README line and both console docs `power` rows carry `off` and the refusal. Found in review round 1 (N10, N11). |
| 24 | The README GPS support list read exhaustive and omitted Nikon (Smart). | The feature table crosses that row's GPS cell, and `NikonSmart::updateGeoData()` exists, so the table and the code disagree about a model whose Scanning cell is ticked while Pairing, Shutter Release, Focus and GPS are crossed. | `README.md:150-161`, `lib/furble/NikonSmart.cpp:183` | The README says the table is the support statement, names the ticked cell and the four crossed ones, and records the mismatch. Found in review round 1 (N12); the crossed cells were corrected in review round 2 from "that whole row is crossed". |
| 25 | `docs/ui-walkthrough.md:211` and `docs/wiki/UI-Walkthrough.md:181` (branch lines) headed a section `GPS Satellites`. | The menu label is `Satellites`: `m_GPSSatStr` is `Satellites` and `addGPSSatMenu()` is its only registration. Both walkthroughs are edited on this branch for the Level entry, so the heading sat in the touched files while the page it describes had already been corrected. | `include/FurbleUI.h:843`, `src/FurbleUI.cpp:9813`, `:10300-10374` | Both headings are renamed `Satellites`. The section prose, the screenshot path and the image alt text are unchanged. Found in review round 2. |

## Review rounds

### Review round 1

The first push was reviewed and returned changes required. Every finding was
re-checked against the code before editing.

| Item | Finding | Resolution | Evidence |
| :--- | :--- | :--- | :--- |
| B1 | The header said no image in the release matrix contains the console. | Fixed. It now says no release board image, names the five published `-debug` images that do carry it (`m5stack-core2`, `m5stick-c`, `m5stick-c-plus`, `m5stick-s3`, `waveshare-s3-eth`), keeps the `esp32-s3-headless` hub image, and states that only the non-debug `waveshare-s3-eth` env drops the console its parent passes down. The README says the same about the release images and the headless hub, without the image list or the `waveshare-s3-eth` drop. | `.github/workflows/release.yml:16-30` (matrix variants `""` and `-debug`), `:84-94` (artifact upload), `platformio.ini:88`, `:108`, `:124`, `:142`, `:167`, `:183`, `:204` |
| B2 | The IR and Level placement sentences were false on the 320x240 boards. | Fixed. The paragraph gives the navigation order first, then the four-column, two-row grid with each entry's cell, and says focus still follows the navigation order. | `src/FurbleUI.cpp:3383-3385`, `:416-421`, `:3404`, `include/FurbleUI.h:953-958` |
| B3 | The Applies bullet said four rows deviate; Button Mode is immediate. | Fixed. Five rows, with Button Mode named: it is re-read on every shutter press, so the row's `Now` is right and the settings map is the outlier. | `src/FurbleUI.cpp:2214-2218` |
| N1 | The wiki kept a thinner `GPS Data` row than the page. | Fixed. Both files now carry the same row, and the table above says so. | `docs/wiki/Settings-Reference.md` |
| N2, N3, N5 | Plan statements did not match the docs sentences. | Fixed. Every affected Truth and Fix cell was rewritten from the final sentence: rows 1, 4, 8, 9, 12, 14, 15 and 19. Rows 1 and 8 were rewritten again in review round 2, which is recorded there. | this document |
| N4 | Item 19 cited the wrong lines. | Fixed. The claim cites `docs/console-commands.md:181` and the code cites `:954-962`, `:989-991` and `:1068-1074`. | `git show origin/master:docs/console-commands.md` |
| N6, N7 | The two UI walkthroughs listed a six-entry menu, with Level missing. | Fixed. Both list seven entries, and their Level paragraphs now say the entry is also on the home menu. | `docs/ui-walkthrough.md`, `docs/wiki/UI-Walkthrough.md`, `src/FurbleUI.cpp:9675-9691` |
| N8 | The README menu list had five entries. | Fixed. Seven, in navigation order, with the IR and Level conditions. | `README.md` |
| N9 | "The other settings with no menu row" claimed too much. | Partly fixed, then rewritten in review round 2. The sentence named the five WiFi and NTP settings but still opened "Five settings have no menu row", which the page contradicts; it now says those five have no menu row at all and points the rest at the rows they already have. See review round 2, R1. | `docs/settings-and-controls.md`, `docs/wiki/Settings-Reference.md` |
| N10, N11 | The README `power` line omitted `off`, and no page said `off` is refused on a display-less build. | Fixed. Both console docs rows and the README line carry `off` and the refusal. | `src/FurbleConsole.cpp:1913-1932` |
| N12 | The README GPS list read exhaustive and omitted Nikon (Smart). | Fixed. The paragraph names the feature table as the support statement and records the table/code mismatch. Review round 2 corrected the row description it carried. | `lib/furble/NikonSmart.cpp:183`, `README.md:150-161` |
| N13 | The `Time zone` row put `Auto (GPS)` second. | Fixed. The row follows the table order and puts `Auto (GPS)` last. | `src/FurbleTimezone.cpp:269-280` |
| N14 | The WebUI was missing from the write paths. | Fixed. The sentence lists an authenticated WebUI settings write, which `POST /api/settings` applies through the same reload path as the console. | `src/FurbleWebUI.cpp:1105-1122`, `:1742-1751` |

### Review round 2

The amended push was reviewed again and returned two blocking statements and
five non-blocking items. Every finding was re-checked against the code and the
diff before editing. Items are numbered as the review listed them.

| Item | Finding | Resolution | Evidence |
| :--- | :--- | :--- | :--- |
| R1 (blocking) | `docs/settings-and-controls.md:404` and `docs/wiki/Settings-Reference.md:388` said "Five settings have no menu row", which is false and contradicts the same page: sixteen setting types in `src/FurbleSettings.cpp` have no `Settings::` reference in `src/FurbleUI.cpp`. | Fixed. The paragraph now says the WiFi and NTP settings are the ones with no menu row at all, names their five wire ids, keys and defaults, and keeps the write paths. A second paragraph points the other settings without a `Settings` page row at the rows and paragraphs already on the page, and states no count. | `src/FurbleSettings.cpp:55-57`, `:67-68`, `:92-100`, `:109-113`; `grep -n 'Settings::' src/FurbleUI.cpp` returns no line for any of the sixteen types |
| R2 (blocking) | `README.md:550` said "that whole row is crossed". The Nikon (Smart) row has Scanning ticked, and only Pairing, Shutter Release, Focus and GPS are crossed. | Fixed. The README names the ticked cell and the four crossed ones and keeps "The table is the support statement". Table row 24 says the same, including which cells are crossed. | `README.md:150-161`, `lib/furble/NikonSmart.cpp:183` |
| R3 | Table row 15 and the Deviations bullet put the top-level menu paragraph at `docs/settings-and-controls.md:26`; on `origin/master` it is lines 24 to 25. | Fixed. Both now say lines 24 to 25, and the Deviations bullet records that `:26` is the blank line after the paragraph. | `git show origin/master:docs/settings-and-controls.md` |
| R4 | Both walkthroughs still headed a section `GPS Satellites`, and the plan did not record it. (The review called this N6; the round 1 N6 in this plan was the walkthrough menu list.) | Fixed by renaming. Both headings are now `Satellites`, and table row 25 records the finding. The section prose, the screenshot path and the image alt text are unchanged. | `include/FurbleUI.h:843` (`m_GPSSatStr` is `Satellites`), `src/FurbleUI.cpp:9813`, `:10300-10374` |
| R5 | The "Not changed" bullet said `docs/ui-walkthrough.md` and `docs/img/` were untouched, but the branch edits both walkthrough files. | Fixed. The bullet now lists the two walkthrough edits and claims only `docs/img/` as unchanged. | `git diff origin/master...HEAD -- docs/ui-walkthrough.md docs/wiki/UI-Walkthrough.md docs/img` |
| R6 | The plan said the walkthroughs listed a five-entry menu. They listed six, with Level missing; only the README listed five. | Fixed. Table row 22, the round 1 N6/N7 cell and the R4 note above state which entries each file was missing. | `git show origin/master:docs/ui-walkthrough.md`, `git show origin/master:docs/wiki/UI-Walkthrough.md`, `git show origin/master:README.md` |
| R7 | Remaining closure claims in the review-round table did not match the diff. | Fixed. The round 1 cells for B1, N2/N3/N5, N9 and N12 now match the text: the README claim no longer says it carries the image list, the rewritten rows are dated to this round, and N9 and N12 point at the corrections above. | this document |

### Review round 3

The third push was reviewed and returned two non-blocking findings, both in this
plan. Both were re-checked against the branch tree and a gate run before editing.

| Item | Finding | Resolution | Evidence |
| :--- | :--- | :--- | :--- |
| S1 | Table row 25 labelled the wiki walkthrough heading `docs/wiki/UI-Walkthrough.md:179`, which is not the `### Satellites` line on this branch; that heading is line 181. | Fixed. The row now says `:181`. Its other half, `docs/ui-walkthrough.md:211`, was already correct and is unchanged. | `grep -n '^### Satellites' docs/ui-walkthrough.md docs/wiki/UI-Walkthrough.md` prints `211` and `181` |
| S2 | The Gates caveat still explained a diff path count the branch no longer has. It described the un-rebased tree, where `origin/master` moves 30 paths past `4af87727` and 3 of those are also on the branch, giving 36. | Fixed. The caveat now gives the count, 9, and says it equals this branch's own changed files because the branch sits on `origin/master`, and it keeps the warning that the gate compares against the base rather than the merge commit. | `python3 tools/check_settings_contracts.py --base origin/master --head HEAD` prints `diff paths: 9, default changes: 0`; `git merge-base HEAD origin/master` is `b57f0118`, which is `origin/master` |

The S1 report put the heading at 176 on `4af87727` and credited two lines to
master. On the tree it is 177 there and 178 at `b57f0118`: PR 471 adds one net
line above it and this branch's Level edits add three, which is the 181 the row
now carries.

## Re-verification commands

```
awk 'NR==541' README.md
sed -n '150,161p;505,520p' README.md
sed -n '9766,9776p;9809,9813p;11273,11275p;11466,11485p;11671,11800p' src/FurbleUI.cpp
sed -n '3383,3408p;416,421p;2214,2218p' src/FurbleUI.cpp
sed -n '841,843p;853,854p;761,762p;953,958p' include/FurbleUI.h
sed -n '954,962p;989,991p;1068,1074p;1405,1420p;1906,1930p;2421,2461p' src/FurbleConsole.cpp
sed -n '55,57p;67,68p;92,100p;109,113p;141,244p;631,636p;776,783p' src/FurbleSettings.cpp
grep -n 'Settings::' src/FurbleUI.cpp
grep -n '^### Satellites' docs/ui-walkthrough.md docs/wiki/UI-Walkthrough.md
git show origin/master:docs/settings-and-controls.md | sed -n '24,25p'
sed -n '3156,3168p' src/FurbleGPS.cpp
sed -n '269,280p;31,80p' src/FurbleTimezone.cpp
sed -n '1105,1122p;1742,1751p' src/FurbleWebUI.cpp
sed -n '125,162p' include/FurbleCrashRecord.h
sed -n '60,72p;84,90p;200,210p' platformio.ini
sed -n '14,30p;80,95p' .github/workflows/release.yml
grep -n 'esp32-s3-headless' .github/workflows/reproducible.yml
python3 tools/check_settings_contracts.py --root . --base origin/master --head HEAD
```

`check_settings_contracts.py` parses both settings references, so a moved
`Default` cell or a renamed label fails the pre-review gate. It reports 140
documented rows after this change, one more than before: the wiki gains the
three new rows and loses two duplicates.

## Deviations from the report

- Item 1 also said "the README feature table already marks them". That is true
  for Canon EOS (Smart) and Panasonic LUMIX but not for Nikon (Smart): the table
  crosses Nikon (Smart) in its GPS column, and `lib/furble/NikonSmart.cpp:183`
  does implement `updateGeoData()`. The prose was aligned with the table.
  Review round 1 asked for the mismatch to be visible rather than resolved, so
  the paragraph names Nikon (Smart) as a row whose GPS cell is crossed while the
  method exists, and says the table is the support statement. Review round 2
  caught that it then called the whole row crossed: the table ticks Scanning and
  crosses Pairing, Shutter Release, Focus and GPS, and the paragraph now says
  exactly that. Whether the row should change is a camera-support decision, not
  a documentation fix, and it is left to plan 61.
- Item 12 named brightness, inactivity, button mode and sleep-connected as the
  disagreements. Against the code, the immediate/restart disagreements are
  Button Mode (console says `immediately`) and TX Power, WiFi, NTP and NTP
  Server (console says `on reboot`). Brightness and Inactivity are restart-only
  in both maps; they differ from the column only because the Display page
  applies them itself, Button Mode is re-read on every shutter press, and
  Connection power save and Sleep while connected follow the connect path
  rather than either map. The doc describes the real split and keeps the
  existing `Next connection` wording for both of those rows. Review round 1
  caught that this is five column rows, not four, and that the Button Mode row
  is right.
- Items 15, 16 and 20 cited line numbers that do not point at the sentence they
  describe on `origin/master`: the top-level menu paragraph is at
  `docs/settings-and-controls.md:24-25`, the Satellites label at
  `include/FurbleUI.h:843`, the page registration at `src/FurbleUI.cpp:9813`,
  and the Screen off row at `docs/settings-and-controls.md:86`. Each claim held,
  and the fix sits at the location named here. This bullet first read `:26`,
  which is the blank line after that paragraph; review round 2 corrected it.

## Not changed

- The feature table in `README.md` and the camera classes. No row moved, and no
  camera support claim changed.
- No Nikon (Smart) support claim was added: the feature table crosses that row's
  GPS cell and `lib/furble/NikonSmart.cpp` implements the method, so the README
  now says the table is the support statement and names the crossed cells
  instead of choosing a side. That is recorded under Deviations, item 1 and
  table row 24.
- `docs/console-commands.md` has no `## ntp`, `## provision`, `## interval`,
  `## bulb` or `## display` section. Item 10 asked for summary rows, and the
  rows carry the registered usage strings. Adding sections would need new prose
  about behaviour that was not audited here.
- The `Raw NMEA` row says "error counts". The page prints `rx`, `ok` and `bad`
  sentence counters, so the wording is loose but not false, and it was not in
  the reported list.
- No screenshot was regenerated and nothing under `docs/img/` changed. Both
  walkthroughs are edited, because they repeat claims this audit covers: each
  Main menu list gains the `Level` entry, each Level paragraph says the entry is
  also on the home menu, and both `GPS Satellites` headings are renamed
  `Satellites`. No UI, string or behaviour changed, so those are the only edits
  in either walkthrough file.
- `docs/wiki/` pages other than Settings-Reference, Console-Commands and
  UI-Walkthrough, including `Home.md` and `Getting-Started.md`, which do not
  repeat any audited claim.

## Gates

- `python3 tools/pre_review.py --base origin/master`: PASS, 10 gates, on the
  original push and after each review round. Diff hygiene reports 0 failures and
  0 warnings. Settings contracts reports 0 FAIL and 6 WARN, all pre-existing
  parser limits (four aggregate defaults in `src/FurbleSettings.cpp` and two
  dynamic CMake test definitions), with 76 settings table rows, 140 documented
  rows parsed, 16 documented defaults compared and no default change. Its diff
  path count is 9, this branch's own changed files, because the branch sits on
  `origin/master`: the gate diffs the two trees rather than their merge base, so
  an older base would add every file merged since it.
- `sh sim/scripts/check-doc-tokens.sh`: PASS. It scans `docs/sim.md`, which this
  branch does not touch.
- No host or simulator test is needed: no code, string, or scenario changed.
