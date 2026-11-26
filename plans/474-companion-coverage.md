# 474 - Companion coverage audit, screenshot gallery and the golden corpus

## Why

The Android companion gained its first UI snapshot test in plan 344 (PR 483) and
the Apple companion has an equivalent one in flight (PR 480). Both plans describe
their own test well. Neither answers the question a reader actually has: what
does each app test, which job runs it, and what reaches a user with nothing
behind it.

This plan is that audit. It reads the workflows instead of the plans, writes the
answers into [docs/companion-test-coverage.md](../docs/companion-test-coverage.md),
and closes the gaps that were cheap and verifiable on a host with a Java
toolchain, a Swift toolchain and clang, but no Xcode, no Android SDK platform
download and no emulator boot.

## The audit

The matrix in [docs/companion-test-coverage.md](../docs/companion-test-coverage.md)
is the deliverable. Its shape:

| App | Automated layers | CI job | Nothing |
| --- | --- | --- | --- |
| Android | 28 JVM tests, 6 golden-corpus tests, 6 UI snapshot baselines | `android.yml`, job `build`, one `./gradlew test` step | Instrumented tests, BLE, pairing, reconnect, settings sync, camera commands |
| Apple | 44 core tests, 59 CLI tests | `apple.yml`, jobs `swift-package` and `apple-project` | Any UI test on `master`, XCUITest, CoreBluetooth, Keychain, pairing |
| Both | 2 Python source contracts, the C++ golden-corpus suite | `main.yml` job `Python tooling tests`, `protocol-tests.yml` job `protocol` | An Android-only pull request runs neither Python module |

## Gaps found

1. **The Android comparison never reached a log.** The merged run for the plan
   344 commit, `android.yml` run `45726215825`, contains no occurrence of
   "screenshot" or "CompanionScreenshot" anywhere in 1,635 log lines, because
   Gradle discards test stdout unless the task asks for it. The test therefore
   passed silently and no observer, human or agent, could tell a runner-side
   match from a skipped test. This is the same defect PR 480 fixed on the Apple
   side by printing its comparison; the Android side never got the fix.

2. **The documented screenshot gallery was three UI changes stale.** Nine PNGs
   under `companion/android/docs/screenshots/` came from one emulator session on
   2026-08-16 and were referenced only from plan 50. `CompanionScreens.kt`
   changed four times after that commit (settings editors, camera catalog,
   camera transactions, framed auth). The images show a three-tab bar whose
   icons rendered as the letters `S`, `S` and `T`, a status screen with no
   Companion password card, an empty settings list, and no Cameras tab. Nothing
   in the repository checked them, and no test produced them.

3. **Five of the app's six screens had no visual coverage at all.** Plan 344
   closed with "Cameras, Settings and Trigger stay untested until a second
   baseline is added the same way", and the permission rationale was never
   captured either.

4. **The committed golden corpus disagreed with the firmware about the trigger
   framing.** `tests/protocol/generate_fixtures.cpp` emitted every trigger
   operation as a four-byte packet and marked `{1, 0}` (two bytes) invalid with
   the description "short trigger packet is rejected". `CompanionService::
   handleTrigger` does the opposite: `((op == 4) && (len != 4)) || ((op != 4) &&
   (len != 2))` drops anything that is not two bytes for operations 0 to 3.
   Both clients send the two-byte form (`FurbleProtocol.encodeTrigger` on
   Android, `Writer(capacity: operation == .timedShutter ? 4 : 2)` on Apple), so
   the corpus described a wire neither the firmware nor the apps use. A client
   written from the corpus would have sent four-byte press and release packets
   that the firmware silently discards, leaving the shutter held until the
   dead-man timeout.

5. **An Android-only pull request runs neither Python source contract.**
   `main.yml` ignores `companion/android/**` on pull requests and
   `pre-review.yml` does not list companion paths, so
   `tests/test_companion_location_contract.py`, whose subjects are the Android
   manifest and `docs/companion.md`, never runs on the pull request that changes
   them.

6. **A layout defect the first camera render exposed.** At 360 dp the Cameras
   header row held three labelled buttons with no wrapping: `Refresh` was
   squeezed to roughly one character per line, and an equal-weight attempt
   hyphenated `Disconnect` mid-word. The screenshot that proves the fix is the
   committed baseline.

## What this plan changed

- `companion/android/app/src/test/java/com/furble/companion/ui/`:
  `CompanionScreenGalleryTest` adds five baselines (rationale, status, cameras,
  settings, trigger) that click through the shipped navigation bar;
  `UiSnapshot` is the single comparator both snapshot classes use;
  `CompanionScreenshotTest` delegates to it with unchanged expectations.
- `CompanionShell` moves from `private` to `internal` so the gallery can render
  the shipped shell with a device state instead of a copy.
- `FurbleGoldenCorpusTest` checks the Android codec against all 384 fixtures in
  `tests/protocol/golden/` and against `companion_auth.json`. It walks up from
  the Gradle module directory, so it needs a full checkout and fails with that
  path instead of skipping when the corpus is absent.
- `app/build.gradle.kts` prints test stdout so every comparison appears in the
  CI log.
- `CompanionScreens.kt`: the Cameras header keeps `Refresh` beside the title and
  gives the two remaining actions a shared row.
- `tests/protocol/generate_fixtures.cpp` and `protocol_test.cpp` emit and check
  the lengths `handleTrigger` accepts: two bytes for operations 0 to 3, four for
  the timed shutter, plus a new `trigger/overlong-release.bin` fixture that pins
  the rejected four-byte release. `make -C tests/protocol generate` regenerated
  the corpus and `make -C tests/protocol test` passes.
- `docs/companion.md` documents the trigger framing for the first time, which is
  the documentation gap behind the corpus defect.
- The stale `companion/android/docs/screenshots/` set is deleted. The baselines
  under `app/src/test/screenshots/` are the gallery and the Android README shows
  them in a table. Recoverable from git history at `699b3284`.
- Android and Apple READMEs state where each suite runs, what the emulator job
  and the simulator lane do not prove, and which flows need hardware.

## Verification

Run on this host (macOS arm64, JDK 21, Swift 6.4.2, Apple clang 21, no Xcode, no
emulator):

```text
cd companion/android && ANDROID_HOME=~/Library/Android/sdk ./gradlew test
  BUILD SUCCESSFUL, 40 tests, 0 failures
  The main screen matches companion-main.png at 720x1440 with 0 differing pixels.
  The permission rationale matches companion-rationale.png at 720x1440 with 0 differing pixels.
  The connected status screen matches companion-status.png at 720x1440 with 0 differing pixels.
  The camera screen matches companion-cameras.png at 720x1440 with 0 differing pixels.
  The settings screen matches companion-settings.png at 720x1440 with 0 differing pixels.
  The trigger screen matches companion-trigger.png at 720x1440 with 0 differing pixels.

cd companion/apple && swift test --configuration release
  Test run with 44 tests in 4 suites passed
  Test run with 59 tests in 9 suites passed

make -C tests/protocol CXX=clang++ generate && make -C tests/protocol CXX=clang++ test
  generated 384 golden fixtures
  protocol conformance passed: 42-byte location, 20-byte status, settings TLVs, triggers, and UUIDs
```

Two identical `-PupdateScreenshots` runs wrote byte-identical files; the six
hashes are `companion-main 2a5a82fb`, `companion-rationale 80a878cb`,
`companion-status 8ceb89b4`, `companion-cameras 9d4896c1`,
`companion-settings 0ffbc770` and `companion-trigger de79f96d`. The `main`
baseline keeps the `2a5a82fb...08c0d` hash plan 344 recorded, so moving the
comparison into `UiSnapshot` did not change a single pixel of it. The commit
that adds this plan is the first run in which the Android workflow prints a
comparison line; its log is the runner-side evidence that the baselines
reproduce off this host.

## Left alone, with the reason

- **The Apple UI snapshot.** PR 480 owns it. Its two open findings are resolved
  on that branch and I confirmed both from the run logs rather than the body:
  `apple.yml` run `45742432751` on `macos-15` printed
  `snapshot comparison: ... matched a fresh render on Version 15.8.2 (Build
  24H232): 360x168, 60480 pixels, 49 beyond 8/255 (0.08%, budget 1.00%), largest
  channel delta 46` and `list row comparison: Status section 5 rows, one-view
  control 2 rows, VStack-wrapped control 2 rows` in both the `swift-package` job
  and the `apple-project` macOS `xcodebuild` lane. No Apple baseline, gallery or
  test is duplicated here.
- **Apple screenshot gallery.** The app has one view under test on that branch
  and no full-screen renderer; building one needs Xcode, which this host does
  not have. The Apple README says so instead of showing a gallery.
- **Instrumented Android tests and an XCUITest target.** Both need an emulator
  boot or a full Xcode plus a simulator, so a test written here could not be run
  once before merging. Adding an unrunnable suite to a coverage plan is worse
  than naming the gap.
- **The Python trigger gap in item 5.** The fix is a workflow trigger change
  that would start the whole scanner suite on every companion pull request, and
  `check_ci_workflows.py` polices the trigger and job contract. It is documented
  in both READMEs and in the matrix; it belongs in a CI-scope PR with its own
  validation, not in a coverage PR.
- **`docs/ui-walkthrough.md`.** Those are firmware simulator screenshots, owned
  by a separate change. Nothing here touches them.

## Still open

- The Android `smoke` job cannot fail a change (`continue-on-error`) and cannot
  see a screen. Making it assert a node or a screenshot would need an emulator
  run per pull request, about ten minutes of runner time.
- Failure images are still only named in the log. An `if: failure()` artifact
  upload of `app/build/outputs/screenshots` remains a follow-up, as plan 344
  recorded.
- The iOS simulator lane proves the app compiles and its unit tests pass. It
  never launches the app.
