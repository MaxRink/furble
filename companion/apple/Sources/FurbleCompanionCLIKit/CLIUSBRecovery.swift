import Foundation
import FurbleCompanionCore

/// Recovery decisions for the USB console monitor (issue #381).
///
/// A failed sample is either a lost port (the node vanished or hung up after a
/// re-enumeration), a stalled endpoint (the port is open but no prompt comes
/// back), or a ROM boot banner. Reopening fixes a lost port. A chip parked in
/// ROM download mode never starts the app on its own, and a stall that
/// survives several reopens needs a reset too. A reset is only issued when the
/// operator passed `--usb-hard-reset`.
public struct CLIUSBRecovery: Equatable, Sendable {
  public enum Failure: Equatable, Sendable {
    case portLost
    case stalled
    case rom(FurbleROMBanner)
  }

  public enum Action: Equatable, Sendable {
    case reopen
    case hardReset
    /// A reset would help but was not allowed. The monitor keeps reopening.
    case needsHardReset
  }

  public let allowHardReset: Bool
  public let stallLimit: Int
  public private(set) var stalls = 0

  public init(allowHardReset: Bool, stallLimit: Int = 3) {
    self.allowHardReset = allowHardReset
    self.stallLimit = stallLimit
  }

  public static func classify(_ error: Error) -> Failure {
    switch error as? FurbleSerialError {
    case .romBoot(let banner)?: return .rom(banner)
    case .timeout?, .notFurble?: return .stalled
    default: return .portLost
    }
  }

  public mutating func next(after failure: Failure) -> Action {
    let needsReset: Bool
    switch failure {
    case .portLost:
      // Nothing to reset through until the node is back.
      return .reopen
    case .rom(let banner):
      needsReset = banner.downloadMode
    case .stalled:
      stalls += 1
      needsReset = stalls >= stallLimit
    }
    guard needsReset else { return .reopen }
    guard allowHardReset else { return .needsHardReset }
    stalls = 0
    return .hardReset
  }

  public mutating func recovered() {
    stalls = 0
  }

  /// A lower uptime than the previous sample means the app restarted between
  /// samples, even when the monitor never saw the port drop.
  public static func rebooted(previousUptime: UInt32?, uptime: UInt32) -> Bool {
    guard let previousUptime else { return false }
    return uptime < previousUptime
  }
}
