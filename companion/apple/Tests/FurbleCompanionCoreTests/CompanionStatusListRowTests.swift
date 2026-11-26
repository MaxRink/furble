#if os(macOS)
import AppKit
import Foundation
import SwiftUI
import Testing
import FurbleCompanionCore

/// The app puts `CompanionStatusView` inside a `List` section and nothing else:
/// `Section("Status") { CompanionStatusView(status: client.status) }`. The pixel
/// snapshot cannot cover that step, because it renders the view outside a List
/// and a four-row body looks the same there whether or not the List flattens it.
///
/// This test hosts the shipped section in AppKit, lets SwiftUI build the list and
/// counts the rows the list actually created. SwiftUI flattens a body that
/// produces several views into one row per view, which is the mechanism `Group`
/// relies on; a `VStack` body would arrive as a single row instead. Both shapes
/// are measured here, so the test fails if the flattening stops happening and
/// also fails if it stops being able to tell the two shapes apart.
///
/// The renderer in `CompanionStatusViewSnapshotTests` still owns the pixels:
/// row content, order and spelling. This test owns the row count.
struct CompanionStatusListRowTests {
  @Test @MainActor func statusSectionBuildsOneRowPerValue() throws {
    let flattened = try ListRows.statusSectionRows()
    let singleRowSection = try ListRows.singleRowSectionRows()
    let collapsed = try ListRows.containerWrappedStatusRows()

    // A section with one view is the control: it measures the section header and
    // any list chrome the row count includes, so the assertion below is about the
    // four values rather than about the list's own overhead. Four values replace
    // the control's single row, so they add three rows to it.
    let extraRows = flattened - singleRowSection
    #expect(
      extraRows == ListRows.valueRowCount - 1,
      """
      The Status section built \(flattened) rows where a one-view section built \
      \(singleRowSection), so the body added \(extraRows) rows instead of \
      \(ListRows.valueRowCount - 1). The VStack-wrapped control built \(collapsed) \
      rows on \(ProcessInfo.processInfo.operatingSystemVersionString).
      """)

    // The same content inside a container is one row. Without this, a row count
    // that ignored the view entirely would still satisfy the assertion above.
    #expect(
      collapsed == singleRowSection,
      """
      Wrapping the Status section body in a VStack built \(collapsed) rows, not the \
      \(singleRowSection) a single-row section builds, so this test can no longer \
      tell a flattened body from a container.
      """)

    print(
      "list row comparison: Status section \(flattened) rows, one-view control "
        + "\(singleRowSection) rows, VStack-wrapped control \(collapsed) rows, "
        + "on \(ProcessInfo.processInfo.operatingSystemVersionString)")
  }
}

/// Hosts a SwiftUI view in AppKit and counts the table rows SwiftUI's macOS list
/// creates. No window and no running `NSApplication` are needed: a laid-out
/// `NSHostingView` already builds the list's row storage, which is what the row
/// count comes from.
private enum ListRows {
  /// `Battery`, `Voltage`, `Cameras` and `GPS`.
  static let valueRowCount = 4

  static let size = NSSize(width: 360, height: 400)

  static func statusSectionRows() throws -> Int {
    try rowCount(of: List { Section("Status") { CompanionStatusView(status: status) } })
  }

  static func singleRowSectionRows() throws -> Int {
    try rowCount(of: List { Section("Status") { Text("Battery") } })
  }

  static func containerWrappedStatusRows() throws -> Int {
    try rowCount(of: List { Section("Status") { VStack { CompanionStatusView(status: status) } } })
  }

  /// Fixed status, the same literal the snapshot test renders, so both tests
  /// describe the same section and the row count cannot drift onto an empty
  /// placeholder row.
  static var status: FurbleProtocol.Status {
    FurbleProtocol.Status(
      version: 1,
      batteryPercent: 87,
      batteryMillivolts: 4012,
      batteryMilliamps: 0,
      powerFlags: 0,
      cameraTotal: 3,
      cameraConnected: 2,
      controlState: 0,
      gpsSource: 1,
      gpsSatellites: 9,
      intervalometerState: 0,
      intervalometerRemaining: 0,
      uptimeSeconds: 0)
  }

  static func rowCount<V: View>(of view: V) throws -> Int {
    let host = NSHostingView(rootView: view)
    host.frame = NSRect(origin: .zero, size: size)
    host.layoutSubtreeIfNeeded()

    var tables: [NSTableView] = []
    walk(host) { node in
      if let table = node as? NSTableView { tables.append(table) }
    }
    guard let table = tables.first else {
      throw ListRowsError(description: """
        The hosted List produced no NSTableView, so the row count cannot be read. \
        SwiftUI's macOS List may no longer be table backed. View classes seen: \
        \(classes(in: host).joined(separator: ", "))
        """)
    }
    guard tables.count == 1 else {
      throw ListRowsError(description: """
        The hosted List produced \(tables.count) NSTableViews with row counts \
        \(tables.map(\.numberOfRows)), so a single row count is ambiguous.
        """)
    }
    return table.numberOfRows
  }

  static func walk(_ view: NSView, _ visit: (NSView) -> Void) {
    visit(view)
    for subview in view.subviews { walk(subview, visit) }
  }

  static func classes(in view: NSView, depth: Int = 0) -> [String] {
    guard depth < 12 else { return [] }
    return [String(describing: type(of: view))]
      + view.subviews.flatMap { classes(in: $0, depth: depth + 1) }
  }
}

private struct ListRowsError: Error, CustomStringConvertible {
  let description: String
}
#endif
