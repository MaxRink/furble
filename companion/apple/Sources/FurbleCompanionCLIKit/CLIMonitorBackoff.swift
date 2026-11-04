import Foundation

/// Exponential reconnect backoff used by the long-run monitor. The first
/// retry is quick, then the delay doubles to a cap so a powered-off device does
/// not keep the Mac scanning at full rate for the whole retry window.
public struct CLIMonitorBackoff: Equatable, Sendable {
  public private(set) var attempt = 0
  public private(set) var delay: TimeInterval = 1
  public let maximumDelay: TimeInterval

  public init(maximumDelay: TimeInterval = 30) {
    self.maximumDelay = maximumDelay
  }

  @discardableResult
  public mutating func nextDelay() -> TimeInterval {
    attempt += 1
    if attempt == 1 {
      delay = 1
    } else {
      delay = min(delay * 2, maximumDelay)
    }
    return delay
  }

  public mutating func reset() {
    attempt = 0
    delay = 1
  }
}
