import FurbleCompanionCore

/// Camera catalog completion policy.
///
/// The BLE client clears its pending flag when its transaction timer fails, so
/// the failed phase is the only evidence that a catalog response timed out.
public enum CLICameraCatalogPolicy {
  public static func timedOut(
    pending: Bool, phase: CompanionConnectionPhase
  ) -> Bool {
    if pending { return true }
    if case .failed(.cameraTransactionTimedOut) = phase { return true }
    return false
  }
}
