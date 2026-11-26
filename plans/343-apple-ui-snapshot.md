# 343 - Apple companion UI snapshot

## Motivation

`companion/apple` had no test that rendered a view. The two host suites
(`FurbleCompanionCoreTests`, 44 cases, and `FurbleCompanionCLIKitTests`, 59
cases) cover the frozen wire records, the auth state machine, Keychain policy,
device selection, CLI parsing and USB recovery. The two `xcodebuild test` lanes
run the same Core suite against the iOS and macOS app targets. Nothing compared
a pixel, so a status row could lose its value, change its copy or swap position
with every test green.

The gap was structural, not just a missing assertion: the app's List body lives
in `Sources/FurbleCompanionApp/FurbleCompanionApp.swift`, which no package
target builds, so no test target could reach it. `project.yml` declares only
`bundle.unit-test` targets (lines 68 and 80) and no UI test bundle.

## Approach

1. Extract the Status section body into
   `Sources/FurbleCompanionCore/CompanionStatusView.swift`. It is production
   view code that takes `FurbleProtocol.Status?` and nothing else: no BLE
   client, no Keychain, no location provider. The app passes `client.status` to
   it, so the app and the test render the same SwiftUI code. `gpsStatusLabel`
   moved with the view.
2. Add `Tests/FurbleCompanionCoreTests/CompanionStatusViewSnapshotTests.swift`
   to the existing unit test target. `swift test` already runs that target, so
   there is no new bundle, no `project.yml` target and no new CI step.
3. Render through SwiftUI `ImageRenderer` at 360 by 168 points with scale 1,
   then compare decoded sRGB pixels against the committed
   `Tests/Snapshots/CompanionStatusView.1x.png`.
4. Tolerate at most 8/255 per channel on at most 1% of the pixels (604 of the
   60,480). That is antialiasing noise; a reflowed row, a changed colour or a
   longer text edit exceeds it, and the cost of that hedge is recorded under the
   open gates below.
5. On a mismatch the test writes `CompanionStatusView.1x.actual.png` beside the
   baseline, and the failure message names the differing pixel count, the
   largest channel delta, both absolute paths and the regenerate command.
6. `FURBLE_APPLE_SNAPSHOT_UPDATE=1` rewrites the baseline from the current
   render. The run refuses to do that when `CI` is set, so CI cannot replace the
   image it compares against.
7. A blank-render floor rejects any render with less than 1% non-background
   pixels. Without it, a renderer that silently produces an empty image could be
   committed as the baseline and would then pass forever.
8. Add `Tests/FurbleCompanionCoreTests/CompanionStatusListRowTests.swift` for the
   step the pixels cannot see: the app puts the view inside a `List` section.
   See "List row flattening" below.

The baseline directory is `Tests/Snapshots`, a sibling of the test target
directory. SwiftPM does not scan it, so it adds no unhandled-resource warning,
and the generated Xcode test targets do not copy it. The test finds it through
`#filePath`, which is the checkout path in both `swift test` and
`xcodebuild test`.

## Determinism measures

| Source of variation | Measure |
| --- | --- |
| Renderer | macOS host only (`#if os(macOS)`); the iOS bundle compiles the file out instead of rendering a second platform |
| Scale | `ImageRenderer.scale = 1`, so no Retina sampling |
| Size | Fixed frame and `ProposedViewSize` of 360 by 168 points |
| Text layout | `Locale("en_US_POSIX")`, Gregorian calendar, UTC, `.leftToRight`, `.dynamicTypeSize(.large)` |
| Colours | `\.colorScheme = .light` plus `NSAppearance(named: .aqua)` as the current drawing appearance, so semantic colours cannot follow a dark host |
| Content | Fixed `FurbleProtocol.Status` literal: 87%, 4012 mV, 2 of 3 cameras, receiver with 9 satellites |
| Clock | The view reads no date or clock, and the test injects none |
| Animation | One static frame, no timers, no transitions |
| Live state | No CoreBluetooth, CoreLocation, Keychain, network or file input |
| PNG encoder | Both sides are decoded into the same sRGB context before comparison, so encoder bytes and metadata do not matter |
| Background | Explicit `Color.white` behind the view, so antialiasing blends against a fixed colour |
| Degenerate render | 1% ink floor before the baseline can be written or compared |

## List row flattening

The snapshot renders `CompanionStatusView` on its own, and a body of four views
stacks vertically whether or not it is wrapped in a container, so the pixels
cannot distinguish a flattened body from a `VStack`. The app does not render the
view on its own: `FurbleCompanionApp.swift` renders
`Section("Status") { CompanionStatusView(status: client.status) }` inside a
`List`. If SwiftUI stopped flattening the body, the app would show one row
holding four stacked values, with the same pixels in the snapshot test and no
failure anywhere.

`CompanionStatusListRowTests` covers that step directly. It hosts the shipped
section in an `NSHostingView`, lays it out, finds the single `NSTableView` that
SwiftUI's macOS `List` is built on and reads `numberOfRows`. No window and no
running `NSApplication` are required. Two controls make the number meaningful:

| Hosted content | Rows |
| --- | --- |
| `List { Section("Status") { CompanionStatusView(status:) } }` | 5 |
| `List { Section("Status") { Text("Battery") } }` | 2 |
| `List { Section("Status") { VStack { CompanionStatusView(status:) } } }` | 2 |

The counts are the same on macOS 27.2.1 and on the runner image's macOS 15.8.2.
The section header accounts for one row: a one-view section is two rows, and the
four status values replace its single row with four, so the section gains
three. The `VStack` control is the shape a refactoring would produce, and it
collapses to the one-view count, so the test can tell the two apart instead of
merely counting something. The assertions are differential (the section gains
three rows over the one-view control, and the `VStack` body equals the one-view
control) rather than the absolute 5 and 2, so a change in section chrome cannot
silently weaken the test.

The test names the view classes it walked if no `NSTableView` appears, and fails
rather than skipping. A future SwiftUI that builds macOS lists from something
other than a table then fails loudly with the evidence needed to update the
test, instead of passing while measuring nothing. `ImageRenderer` cannot render
a `List`, and an XCUITest bundle would need a new target, an app launch and a
simulator, which is why the row count is read from AppKit instead.

## Rejected alternatives

- Snapshot the whole `ContentView`. It owns a `FurbleBLEClient` state object, a
  `KeychainCredentialStore` and a `FurbleLocationProvider`, so a render would
  start CoreBluetooth and CoreLocation. Moving it into the package would be a
  much larger change than this gap needs.
- Snapshot the List and its section chrome. `List` renders through a platform
  table view, and its row metrics and backgrounds are exactly the platform
  detail a baseline should not freeze. The shipped view is snapshotted without
  List chrome; the section headers and the connection row are still uncovered.
- Compare PNG bytes. Encoder details and metadata are not UI.
- A UI test bundle with XCUITest screenshots. It would need a new target, a
  simulator in the swift-test-free lane and an app launch, and the repository
  already warns that capture byte-equality only holds for the capturing binary
  (`.github/workflows/ui-screenshots.yml`).

## Baseline provenance

The committed baseline was authored on a developer host running macOS 27.2.1
with Swift 6.4.2, because that host had no Xcode. It is compared, never
regenerated, on the CI image:

| Side | Environment |
| --- | --- |
| Baseline authored on | macOS 27.2.1, build 26B131, Swift 6.4.2 (Command Line Tools) |
| Compared on | `macos-15-arm64`, image 20261118.0550.1, macOS 15.8.2, build 24H232, Xcode 16.4 |

Two jobs compare it on that image: `swift-package` (`swift test --configuration
release`, so the release configuration is covered) and the `apple-project`
macOS `xcodebuild test` lane. A passing run prints the comparison, so a green
job states what it compared instead of only that a suite passed:

```
snapshot comparison: <checkout path>/Tests/Snapshots/CompanionStatusView.1x.png
matched a fresh render on Version 15.8.2 (Build 24H232): 360x168, 60480 pixels,
49 beyond 8/255 (0.08%, budget 1.00%), largest channel delta 46
```

That is the whole cross-image question answered with a number: the two images
disagree on 49 of 60,480 pixels, 0.08% against a 1.00% budget, and the largest
channel delta anywhere is 46 of 255. For scale, this plan measures one glyph
substitution at about 80 pixels and the `Battery level` edit at 715, so 49 is
less than a changed glyph and far below a shifted row. The disagreement is font
rasterisation at glyph edges, not layout, order or colour. If a future image
exceeds the budget,
regenerate the baseline on that image, record its version here and review the
PNG. The 1% budget exists so a difference this size does not force a
regeneration; it is not a licence to accept a visible change.

## Implementation state

Done on `test/apple-snapshot`, rebased onto `origin/master`.

## Evidence

All commands run in `companion/apple` unless stated.

| Command | Result |
| --- | --- |
| `swift test` with no baseline | `Missing snapshot baseline .../Tests/Snapshots/CompanionStatusView.1x.png.` The render was written to `CompanionStatusView.1x.actual.png`. The other 44 Core tests and all 59 `FurbleCompanionCLIKitTests` passed |
| `FURBLE_APPLE_SNAPSHOT_UPDATE=1 swift test --filter CompanionStatusViewSnapshotTests` | `wrote snapshot baseline .../Tests/Snapshots/CompanionStatusView.1x.png`, `Test run with 1 test in 1 suite passed` |
| The same regeneration run twice in a row | `cmp` clean and one SHA-256 for both files, `303e8b9374f9f970b9a6497a5f152543211043cfcc3bade8257217ed5b4af28a`. The render and the PNG encoder are deterministic on this host |
| `Battery` label temporarily renamed to `Battery level` | Fails with `715 of 60480 pixels differ (1.18%, budget 1.00% at 8/255 per channel). Largest channel delta: 216`, and writes `CompanionStatusView.1x.actual.png` beside the baseline. Reverted |
| `CI=1 FURBLE_APPLE_SNAPSHOT_UPDATE=1 swift test --filter CompanionStatusViewSnapshotTests` | Fails with `FURBLE_APPLE_SNAPSHOT_UPDATE is set on CI. Refusing to rewrite ...` and leaves the baseline unchanged |
| Canvas temporarily forced to 3x3 | Fails with `rendered 0 of 9 non-background pixels (0.00%), under the 1.00% floor` |
| `swift test` final run, macOS 27.2.1 | `Test run with 46 tests in 6 suites passed`, `Test run with 59 tests in 9 suites passed`, exit 0 |
| `swift test` comparison line on macOS 27.2.1 | `snapshot comparison: .../Tests/Snapshots/CompanionStatusView.1x.png matched a fresh render on Version 27.2.1 (Build 26B131): 360x168, 60480 pixels, 0 beyond 8/255 (0.00%, budget 1.00%), largest channel delta 0` |
| `swift test --filter CompanionStatusListRowTests` on macOS 27.2.1 | `list row comparison: Status section 5 rows, one-view control 2 rows, VStack-wrapped control 2 rows`, `Test statusSectionBuildsOneRowPerValue() passed` |
| `swiftc -parse` on both app sources and `CompanionStatusView.swift` | exit 0 |
| Public API typecheck of `CompanionStatusView(status:)` from a separate module | exit 0 |
| Committed baseline | `Tests/Snapshots/CompanionStatusView.1x.png`, 360 by 168, 8-bit RGBA, 8,790 bytes |

The CI evidence below is from the Apple workflow on the PR head.

| Command | Result |
| --- | --- |
| `swift-package` job on head `6eb2fd08`, `macos-15-arm64` image 20261118.0550.1, macOS 15.8.2, Xcode 16.4 | `Test statusViewMatchesCommittedBaseline() passed after 0.659 seconds`, `Test run with 104 tests passed`. No baseline was written and none was missing, so the pass is a comparison |
| `apple-project` iOS simulator lane | 44 tests; both macOS-only test files compile into `FurbleCompanionCoreTests-iOS` and out again through `#if os(macOS)`, with no diagnostics |
| `apple-project` macOS `xcodebuild test` lane | `Test statusViewMatchesCommittedBaseline() passed`, `Test run with 46 tests passed`, `** TEST SUCCEEDED **`, so `#filePath` resolves to the checkout in the Xcode build too |
| `swift-package` job after the reporting change | `snapshot comparison: /Users/runner/work/furble/furble/companion/apple/Tests/Snapshots/CompanionStatusView.1x.png matched a fresh render on Version 15.8.2 (Build 24H232): 360x168, 60480 pixels, 49 beyond 8/255 (0.08%, budget 1.00%), largest channel delta 46`, `Test run with 105 tests passed` |
| `swift-package` job, row count on the runner image | `list row comparison: Status section 5 rows, one-view control 2 rows, VStack-wrapped control 2 rows, on Version 15.8.2 (Build 24H232)`, `Test statusSectionBuildsOneRowPerValue() passed` |
| Standalone AppKit probe of the row shapes on macOS 27.2.1 | `Section` with the four-value body 5 rows, with a `VStack` body 2 rows, with one `Text` 2 rows, and the same counts with and without a window and with a 30 point list height |

## Closed and open gates

Closed by the CI runs above:

- No Xcode on the authoring host. Both `xcodebuild test` lanes ran on
  `macos-15`: the iOS lane builds the test file and compiles it out, and the
  macOS lane runs the comparison.
- Cross-image font rasterisation. The `macOS 27.2.1` baseline was compared on
  `macOS 15.8.2` with the deltas in the evidence table, inside the 1% budget.
- Only the debug configuration was exercised locally. `swift-package` runs
  `swift test --configuration release` and compares the same baseline.
- The app target was not built and List row flattening was unproven.
  `CompanionStatusListRowTests` counts the rows on both the `swift test` path
  and the macOS `xcodebuild test` lane, and both app lanes build the app target
  that hosts the section.
- The job could render without reporting a comparison. A passing run now prints
  the baseline path, the host version and the measured deltas.

Still open, with the reason it is acceptable:

- The 1% budget is a cross-image hedge, and it has a cost. A single glyph
  substitution moves about 80 pixels, 0.13% of the canvas. The measured
  `Battery level` edit moved 715 pixels, 1.18%, about 120 pixels per glyph
  because the following glyphs shift. Text edits shorter than roughly four
  glyphs can stay under the budget. The snapshot fails on row order, row
  presence, colour, layout and longer copy edits. The frozen records stay
  covered by the protocol tests, which parse and compare them.
- `List` chrome is not covered: section headers, row separators, row insets and
  the Connection section are outside the rendered view by design. The row count
  test covers how many rows the section produces, not how they are decorated.
