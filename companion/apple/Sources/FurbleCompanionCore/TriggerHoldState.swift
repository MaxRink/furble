import Foundation

/// Tracks the two stateful trigger outputs without depending on CoreBluetooth
/// or SwiftUI. A press is emitted once and must be paired with a release. This
/// keeps a view disappearing, or a repeated touch callback, from leaving an
/// output held indefinitely.
public struct TriggerHoldState: Equatable, Sendable {
  public private(set) var shutterHeld = false
  public private(set) var focusHeld = false

  public init() {}

  public mutating func pressShutter() -> FurbleProtocol.TriggerOperation? {
    guard !shutterHeld else { return nil }
    shutterHeld = true
    return .shutterPress
  }

  public mutating func releaseShutter() -> FurbleProtocol.TriggerOperation? {
    guard shutterHeld else { return nil }
    shutterHeld = false
    return .shutterRelease
  }

  public mutating func pressFocus() -> FurbleProtocol.TriggerOperation? {
    guard !focusHeld else { return nil }
    focusHeld = true
    return .focusPress
  }

  public mutating func releaseFocus() -> FurbleProtocol.TriggerOperation? {
    guard focusHeld else { return nil }
    focusHeld = false
    return .focusRelease
  }

  public mutating func releaseAll() -> [FurbleProtocol.TriggerOperation] {
    var operations: [FurbleProtocol.TriggerOperation] = []
    if let operation = releaseShutter() { operations.append(operation) }
    if let operation = releaseFocus() { operations.append(operation) }
    return operations
  }

  /// Restores the held bit when a Bluetooth write failed before reaching the
  /// firmware. The caller passes the operation returned by a press or release.
  public mutating func restore(after operation: FurbleProtocol.TriggerOperation) {
    switch operation {
    case .shutterPress: shutterHeld = false
    case .shutterRelease: shutterHeld = true
    case .focusPress: focusHeld = false
    case .focusRelease: focusHeld = true
    case .timedShutter: break
    }
  }
}
