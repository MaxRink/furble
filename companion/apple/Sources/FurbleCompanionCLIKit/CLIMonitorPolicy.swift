import Foundation

/// Pure stop conditions for a long battery observation. Keeping these separate
/// from the CoreBluetooth loop lets the duration, end-of-discharge and retry
/// behavior be tested on the host.
public enum CLIMonitorPolicy {
  public static func reachedDuration(
    elapsed: TimeInterval, duration: TimeInterval?
  ) -> Bool {
    guard let duration else { return false }
    return elapsed >= duration
  }

  /// Percent 255 means the firmware does not know the charge level and must
  /// never trigger an end-of-discharge stop.
  public static func reachedEmpty(
    batteryPercent: UInt8, untilEmpty: Bool
  ) -> Bool {
    untilEmpty && batteryPercent <= 1
  }

  public static func retryExhausted(
    lastActivity: Date, now: Date, retryWindow: TimeInterval
  ) -> Bool {
    now.timeIntervalSince(lastActivity) >= retryWindow
  }
}
