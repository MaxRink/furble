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

## Implementation state

Done on `test/apple-snapshot`, committed locally, not pushed.

## Evidence

All commands run in `companion/apple`.

| Command | Result |
| --- | --- |
| `swift test` with no baseline | `Missing snapshot baseline .../Tests/Snapshots/CompanionStatusView.1x.png.` The render was written to `CompanionStatusView.1x.actual.png`. The other 44 Core tests and all 59 `FurbleCompanionCLIKitTests` passed |
| `FURBLE_APPLE_SNAPSHOT_UPDATE=1 swift test --filter CompanionStatusViewSnapshotTests` | `wrote snapshot baseline .../CompanionStatusView.1x.png`, `Test run with 1 test in 1 suite passed` |
| The same regeneration run twice in a row | `cmp` clean and one SHA-256 for both files, `303e8b9374f9f970b9a6497a5f152543211043cfcc3bade8257217ed5b4af28a`. The render and the PNG encoder are deterministic on this host |
| `Battery` label temporarily renamed to `Battery level` | Fails with `715 of 60480 pixels differ (1.18%, budget 1.00% at 8/255 per channel). Largest channel delta: 216`, and writes `CompanionStatusView.1x.actual.png` beside the baseline. Reverted |
| `CI=1 FURBLE_APPLE_SNAPSHOT_UPDATE=1 swift test --filter CompanionStatusViewSnapshotTests` | Fails with `FURBLE_APPLE_SNAPSHOT_UPDATE is set on CI. Refusing to rewrite ...` and leaves the baseline unchanged |
| Canvas temporarily forced to 3x3 | Fails with `rendered 0 of 9 non-background pixels (0.00%), under the 1.00% floor` |
| `swift test` final run | `Test run with 45 tests in 5 suites passed`, `Test run with 59 tests in 9 suites passed`, exit 0 |
| `swiftc -parse` on both app sources and `CompanionStatusView.swift` | exit 0 |
| Public API typecheck of `CompanionStatusView(status:)` from a separate module | exit 0 |
| Committed baseline | `Tests/Snapshots/CompanionStatusView.1x.png`, 360 by 168, 8-bit RGBA, 8,790 bytes |

## Gates still open

- No Xcode on the host that produced this change. `xcodebuild -version` fails
  with "requires Xcode, but active developer directory is a command line tools
  instance", so neither the iOS simulator lane nor the macOS Xcode lane ran.
  Both must be run. The macOS lane has to prove that the Xcode-built test bundle
  resolves `#filePath` to the checkout and renders the same pixels, and the iOS
  lane has to prove that the file compiles out with no diagnostics.
- The baseline was generated on macOS 27.2.1 with the macOS 27.2 SDK and Swift
  6.4.2. The `swift-package` CI job runs `swift test --configuration release` on
  `macos-15` with Xcode 16.4. Cross-image font rasterisation equality is
  unproven. If the pixels differ beyond the tolerance, regenerate the baseline
  on an image that matches the lane and record the image version in the commit.
  Only the debug configuration was exercised here; release must match it.
- The app target was not built. The Status section now contains one view,
  `CompanionStatusView`, whose body is the four former rows. SwiftUI list
  containers flatten a nested body into one row per view, which is the same
  mechanism `Group` relies on, but no run here proves the four rows, their
  separators and their spacing. The macOS and iOS app lanes must show the
  section unchanged.
- Whole-file typechecking of the app target is not possible here: the
  unmodified `origin/master` app source fails the same `swiftc -typecheck`
  invocation with 10 `cannot assign to property: 'self' is immutable` errors, so
  that check is an artifact of typechecking SwiftUI outside an Xcode app target,
  not a result of this change. Only syntax parsing was checked for that file.
- The 1% budget is a cross-image hedge, and it has a cost. A single glyph
  substitution moves about 80 pixels, 0.13% of the canvas. The measured
  `Battery level` edit moved 715 pixels, 1.18%, about 120 pixels per glyph
  because the following glyphs shift. Text edits shorter than roughly four
  glyphs can stay under the budget. The snapshot fails on row order, row
  presence, colour, layout and longer copy edits. The frozen records stay
  covered by the protocol tests, which parse and compare them.
- `List` chrome is not covered: section headers, row separators, row insets and
  the Connection section are outside the single rendered view by design.
