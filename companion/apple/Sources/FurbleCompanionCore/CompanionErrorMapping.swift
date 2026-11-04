import CoreBluetooth
import Foundation

/// Maps CoreBluetooth transport errors onto companion failures.
///
/// A protected characteristic read fails with ATT "insufficient encryption"
/// when the link is not secured. That is a pairing problem, not a malformed
/// packet, and the CLI needs to say so.
public enum CompanionErrorMapping {
  public static func failure(for error: Error) -> CompanionFailure {
    let nsError = error as NSError
    if nsError.domain == CBATTErrorDomain,
      let code = CBATTError.Code(rawValue: nsError.code) {
      switch code {
      case .insufficientAuthentication, .insufficientAuthorization,
        .insufficientEncryption, .insufficientEncryptionKeySize:
        return .linkSecurityFailed
      default:
        return .linkLost
      }
    }
    if nsError.domain == CBErrorDomain, let code = CBError.Code(rawValue: nsError.code) {
      switch code {
      case .encryptionTimedOut, .peerRemovedPairingInformation:
        return .linkSecurityFailed
      default:
        return .linkLost
      }
    }
    return .linkLost
  }
}
