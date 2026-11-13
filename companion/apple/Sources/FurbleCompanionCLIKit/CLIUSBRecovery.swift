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
  /// samples, even when the monitor never saw the port drop. A missing
  /// uptime proves nothing.
  public static func rebooted(previousUptime: UInt32?, uptime: UInt32?) -> Bool {
    guard let previousUptime, let uptime else { return false }
    return uptime < previousUptime
  }
}

/// One USB monitor sample and its recovery. The CLI runs it on its sample
/// timer and owns only the backoff, the output rows and Ctrl-C; tests drive
/// the same code through a fake transport.
public final class CLIUSBMonitorSession {
  public enum Outcome {
    case sample(status: [String: String], gps: [String: String])
    case failed(Error)
  }

  public let console: FurbleSerialConsole
  public private(set) var recovery: CLIUSBRecovery
  public private(set) var lost = false
  public var commandTimeout: TimeInterval = 5
  public var resetDwell: TimeInterval = 0.1
  private var resetAdvised = false
  private var sampled = false
  private var lastUptime: UInt32?
  private let marker: (String) -> Void
  private let note: (String) -> Void

  public init(
    console: FurbleSerialConsole, allowHardReset: Bool,
    marker: @escaping (String) -> Void, note: @escaping (String) -> Void
  ) {
    self.console = console
    self.recovery = CLIUSBRecovery(allowHardReset: allowHardReset)
    self.marker = marker
    self.note = note
  }

  public func step() -> Outcome {
    do {
      try console.open()
      // The debug UI occasionally holds its lock across a display flush.
      // A 2 s command timeout made the monitor close and reopen the CDC
      // port during those windows, which could leave the endpoint wedged.
      let status = try Self.parseKeyValues(console.command("status", timeout: commandTimeout))
      guard status["version"] != nil else { throw FurbleSerialError.notFurble }
      let gps = try Self.parseKeyValues(console.command("gps", timeout: commandTimeout))
      if lost {
        marker("reconnected")
        lost = false
        resetAdvised = false
        recovery.recovered()
      }
      let uptime = status["uptime"].flatMap { UInt32($0) }
      if !sampled {
        reportReset(status, marker: false)
      } else if CLIUSBRecovery.rebooted(previousUptime: lastUptime, uptime: uptime) {
        reportReset(status, marker: true)
      }
      sampled = true
      if let uptime { lastUptime = uptime }
      return .sample(status: status, gps: gps)
    } catch {
      recover(from: error)
      return .failed(error)
    }
  }

  private func recover(from error: Error) {
    console.close()
    if !lost {
      marker("link-lost")
      lost = true
    }
    let failure = CLIUSBRecovery.classify(error)
    if case .rom(let banner) = failure {
      marker("rom-reset:\(banner.reason ?? "unknown")")
      note("ROM banner: \(banner.resetLine)")
    }
    switch recovery.next(after: failure) {
    case .reopen:
      break
    case .hardReset:
      do {
        if try console.hardReset(dwell: resetDwell) == .pulsedPortLost {
          note("The reset dropped the USB port; reopening.")
        }
        marker("hard-reset")
      } catch {
        note("USB hard reset failed: \(error)")
      }
      // Each S3 port open resets the chip, so reopen on the next sample only.
      console.close()
    case .needsHardReset:
      if !resetAdvised {
        marker("hard-reset-needed")
        resetAdvised = true
      }
      note(
        "The chip is stuck in ROM or the console stays silent. Rerun with "
          + "--usb-hard-reset, or run esptool.py --chip esp32s3 --port \(console.path) "
          + "--after hard_reset chip_id.")
    }
  }

  // The reset reason and the previous run's crash record are the only
  // evidence of why the app restarted; the USB endpoint drops the panic.
  private func reportReset(_ status: [String: String], marker emit: Bool) {
    let reason = status["reset"] ?? "unknown"
    if emit { marker("device-reset:\(reason)") }
    note("Device reset reason: \(reason), uptime \(status["uptime"] ?? "unknown") s.")
    if let crash = try? Self.parseKeyValues(console.command("crash", timeout: commandTimeout)),
      let summary = crash["crash.summary"] {
      note("Previous run crash record: \(summary)")
    }
  }

  public static func parseKeyValues(_ text: String) throws -> [String: String] {
    var values: [String: String] = [:]
    for line in text.split(whereSeparator: { $0.isNewline }) {
      guard let separator = line.firstIndex(of: ":") else { continue }
      let key = line[..<separator].trimmingCharacters(in: .whitespaces)
      let value = line[line.index(after: separator)...].trimmingCharacters(in: .whitespaces)
      if !key.isEmpty { values[key] = value }
    }
    guard !values.isEmpty else { throw FurbleSerialError.notFurble }
    if ProcessInfo.processInfo.environment["FURBLE_COMPANION_DEBUG"] != nil {
      FileHandle.standardError.write(Data("[usb] keys: \(values.keys.sorted())\n".utf8))
    }
    return values
  }
}
