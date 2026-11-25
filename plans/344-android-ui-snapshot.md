# 344 - Android UI snapshot test

## Why

The Android companion's only automated coverage was JVM logic. Four test files
covered the camera catalog, the auth gate, the protocol encoder and the GATT
contract, and nothing rendered a Compose screen. A layout regression in
`CompanionScreens.kt` would therefore reach the emulator job only as a process
that is still alive.

## The gap

- `companion/android/app/src/test` held `CameraCatalogTest`, `AuthGateTest`,
  `FurbleProtocolTest` and `FurbleGattContractTest`. There was no `androidTest`
  source set and no test that started an Android runtime.
- `companion/android/app/build.gradle.kts` declared `junit:junit:4.13.2` as its
  only test dependency, so a Compose screen could not be composed off-device.
- The emulator job in `.github/workflows/android.yml` is scheduled and manual
  only. It installs the debug APK, launches it with `monkey`, greps logcat for
  `FATAL EXCEPTION` and checks that the pid exists. It cannot see a clipped
  card, a missing label or a shifted navigation bar.
- The firmware simulator has rendered pinned screenshots for a while (plans 76
  and 85, `.github/workflows/ui-screenshots.yml`), but those capture the LVGL UI
  on the host, not the companion.

## Design

One Robolectric test, `CompanionScreenshotTest`, in the existing `src/test`
source set:

- It builds the app's `FurbleApplication` from the merged manifest, so the
  screen receives the real `MainViewModel` and the repository's initial state.
- It composes the public main screen, `FurbleCompanionApp` from
  `CompanionScreens.kt`, with every permission granted so the shell (top bar,
  navigation bar and the Status tab) renders instead of the permission
  rationale.
- It captures the fixed 360dp x 720dp region at density 2 into a 720x1440
  bitmap and compares it with the committed baseline
  `app/src/test/screenshots/companion-main.png`.
- Two known nodes, the top bar title and the session state line, are asserted
  before the capture, so an asynchronous repository emission fails on the node
  that moved instead of on a pixel count.
- A per-channel tolerance of 2 and a whole-image limit of 0.1 percent of
  differing pixels decide the comparison, which is 1036 of the 1,036,800 pixels
  in the capture. A larger difference fails, and the failure writes the rendered
  bitmap to `app/build/outputs/screenshots/companion-main-actual.png` and names
  both the image and the regeneration command.
- Regeneration is opt-in:
  `./gradlew testDebugUnitTest --tests '*CompanionScreenshotTest*' -PupdateScreenshots`.

The test runs inside `./gradlew test`, which the `Run unit tests and the UI
snapshot test` step of the Android CI job already invokes before
`assembleDebug`, so no workflow and no new job are needed. AGP 9 runs only the
debug unit test variant for this module, which is why the Compose test activity
can be a `debugImplementation`.

## What the gate detects

The budget is a layout gate, not a copy gate. Measured against the committed
baseline in the same environment as the evidence below:

| Edit | Differing pixels | Result |
| --- | --- | --- |
| An appended period | 12 | passes |
| An appended short word, `Push fixes to furble today` | 654 | passes |
| `furble` changed to `Furble` | 1042 | fails at 0.101 percent |
| Five characters inserted mid-line, `Push fixes to your furble` | 1529 | fails |
| 1 dp more spacing in the status list | 27,667 | fails at 2.668 percent |

So a text change only fails once it reaches roughly a dozen appended characters,
while a single substitution can land on either side of 1036 pixels because the
count follows the glyphs that move, not the characters edited. A layout-scale
change fails with an order of magnitude to spare, and a clipped card, a reflow,
a dropped row or a shifted bar moves at least as many pixels as the 1 dp row
below, because all of them displace whole regions. An earlier draft of this plan
and of the README claimed the tolerance absorbed glyph edge noise between Skia
builds while a layout, text or colour change still failed. The layout and colour
halves hold; the glyph noise need is unproven, and the text half does not hold.

The tolerance is insurance for an unproven runner, not a measured requirement.
The only cross-host evidence is a 0-pixel difference between the macOS arm64
baseline and a linux/amd64 render, so nothing observed needs the slack. It
exists so the first CI run cannot fail on glyph noise before that run shows the
runner's pixels, and it is what lets small copy edits through.

## Dependencies

All new dependencies are test-only and are listed in
`docs/android-dependency-lock.md`:

| Artifact | Configuration | Version |
| --- | --- | --- |
| `org.robolectric:robolectric` | `testImplementation` | 4.18.1 |
| `androidx.compose.ui:ui-test-junit4` | `testImplementation` | 1.12.0, from Compose BOM 2026.08.00 |
| `androidx.compose.ui:ui-test-manifest` | `debugImplementation` | 1.12.0, from Compose BOM 2026.08.00 |

`ui-test-manifest` contributes one empty, exported, filterless
`androidx.activity.ComponentActivity` to the debug manifest, which the Compose
test rule launches. `app-debug.apk` therefore declares one extra exported
filterless activity, and that is the APK the emulator smoke job installs; the
release manifest is untouched. The launcher entry is still `MainActivity`, so
`monkey` and the logcat checks in the emulator job behave as before. The build
script also sets two JVM test options:

- `isIncludeAndroidResources = true`, so the merged manifest, the app theme and
  the resources are on disk for the Robolectric test.
- `robolectric.graphicsMode=NATIVE` for every unit test fork. The four existing
  tests never start Robolectric, so the property does not change them.

The baseline PNG is declared as a task input, so regenerating it reruns the
comparison instead of leaving `testDebugUnitTest` up to date. Update mode also
turns off that up-to-date check and the build cache, because regeneration writes
the input it just read, and the test replaces the baseline through a sibling
temp file so an interrupted run cannot leave a truncated image.

## Determinism

| Input | How it is pinned |
| --- | --- |
| Android platform | `@Config(sdk = [35])` |
| Locale | `@Config(qualifiers = "en-rUS-...")` and `Locale.setDefault(Locale.US)`, with the calling JVM locale restored in `@After` |
| Density and font scale | `LocalDensity` provided as `Density(density = 2f, fontScale = 1f)` |
| Pixel size | 360dp x 720dp capture region at density 2, so 720x1440 pixels |
| Clock | The main screen renders no time. `CompanionUiState` carries no timestamp and the status card shows "Waiting for a status notification" until a device notifies. |
| Network and device state | The snapshot starts from the repository's empty state: no association, no capability, no camera and no setting. Nothing in the screen is read from a socket or from a BLE characteristic. |
| Asynchronous repository state | The test asserts the top bar and the session state line before the capture, so a late emission from the repository fails on the node that moved instead of on a pixel count. |
| Material theme | `CompanionScreens.kt` reads `MaterialTheme` without wrapping the content, so the app and the test both render the Material3 default light scheme, not the XML activity theme. |
| Platform jar | Robolectric resolves the fixed Android 15 jar for SDK 35 and reuses the Maven cache afterwards. |

API 35 is deliberate. Robolectric refuses SDK 36 and above on Java 17, and the
module pins its unit test toolchain to Java 17 with `jvmToolchain(17)`. Bumping
the whole test fork to Java 21 to gain SDK 36 would change how the existing
tests run, so the snapshot stays on 35 and the app keeps targeting 37.

## Evidence

Environment: macOS 27.2.1 arm64, Temurin JDK 21.0.13.1 for the Gradle daemon
and the repo's Temurin JDK 17.0.21.1 toolchain for the test fork, Android SDK
with platform `android-37.0`, Gradle 9.4.1, AGP 9.2.1.

| Command | Result |
| --- | --- |
| `./gradlew test` before the change | 28 tests pass. The task list is `:app:testDebugUnitTest` then `:app:test`. |
| `./gradlew test` after the change | 29 tests pass, no failure. The new test prints `The main screen matches companion-main.png at 720x1440 with 0 differing pixels.` The four existing classes are unchanged. |
| `./gradlew testDebugUnitTest --tests '*CompanionScreenshotTest*' -PupdateScreenshots`, twice | Two separate forks wrote a byte-identical PNG: sha256 `2a5a82fb9f08b801cd7cf7d94db4202f6f39e5a9d731594d822072682ca08c0d`, 110,306 bytes. |
| Baseline with a painted 60x60 red block | Fails with `differs from companion-main.png in 3600 pixels (0.347 percent, allowed 0.1 percent)`, writes `app/build/outputs/screenshots/companion-main-actual.png` and names the regeneration command. Restoring the baseline returns the hash and the pass. |
| Baseline moved aside | Fails with the missing-file message, still writes the rendered image and names the regeneration command. |
| Same test in a `linux/amd64` container, Temurin JDK 17, project copy at `/work` | `The main screen matches companion-main.png at 720x1440 with 0 differing pixels.` The macOS baseline is byte-compatible with the Linux x86_64 render. |
| Five baseline edits in `What the gate detects` | `./gradlew testDebugUnitTest --tests '*CompanionScreenshotTest*'` passes at 12 and 654 differing pixels and fails at 1042, 1529 and 27,667. |
| `-PupdateScreenshots` twice in a row | The test task runs both times instead of reporting up to date, and both forks write the same PNG through the temp file. |

The last cross-host row is the important one for CI: Robolectric bundles its own
Skia and FreeType and takes the fonts from the Android 15 platform jar, so glyph
rasterization does not depend on the host. The committed baseline was
generated on macOS arm64 and still matched exactly on Linux x86_64, which is
what the `ubuntu-latest` runner uses. That 0-pixel result is why the tolerance
is described as insurance for the unproven runner rather than a measured
requirement.

## Implementation state

Landed with this PR: the test dependencies, the Robolectric snapshot test, the
committed baseline, the task-input declaration, the two-node assertion before
the capture, the JVM locale restore, the uncacheable update mode with its temp
file write, the `companion/android/README.md` test section,
`docs/android-dependency-lock.md` and `tools/android-dependency-lock.json`. The
review findings and their resolutions are in
[344-review-feedback.md](344-review-feedback.md).

## Still open

- Only the workflow itself can prove its runner produces the same pixels as the
  Linux x86_64 container above. If the first CI run fails, its message records
  the differing pixel count, so a host difference can be separated from a real
  UI change before anyone regenerates the baseline.
- The `smoke` job is scheduled and manual only, so the debug APK manifest
  change is confirmed by inspection rather than by a run: `aapt2 dump xmltree`
  shows the extra exported filterless `androidx.activity.ComponentActivity` and
  an unchanged `MainActivity` launcher entry.
- The first run on a cold runner downloads `android-all-instrumented`
  `15-robolectric-14663667-2-i7`, about 197 MB, from Maven Central into
  `~/.m2/repository/org/robolectric`. `gradle/actions/setup-gradle` caches the
  Gradle module cache, not that directory, so the download repeats whenever the
  runner has no warm home directory. Caching `~/.m2/repository/org/robolectric`
  would remove it.
- The failure image is only named in the log today. An `if: failure()` upload of
  `companion/android/app/build/outputs/screenshots` would keep it for review;
  that belongs to `.github/workflows/android.yml` in a follow-up.
- Only the main screen is covered. Cameras, Settings and Trigger stay untested
  until a second baseline is added the same way.
