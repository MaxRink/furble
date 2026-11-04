import CoreBluetooth
import Foundation
import Testing

@testable import FurbleCompanionCore

@Suite("Companion BLE error mapping")
struct CompanionErrorMappingTests {
  @Test func insufficientEncryptionIsASecurityFailure() {
    let error = NSError(
      domain: CBATTErrorDomain,
      code: CBATTError.Code.insufficientEncryption.rawValue)
    #expect(CompanionErrorMapping.failure(for: error) == .linkSecurityFailed)
  }

  @Test func insufficientAuthenticationIsASecurityFailure() {
    let error = NSError(
      domain: CBATTErrorDomain,
      code: CBATTError.Code.insufficientAuthentication.rawValue)
    #expect(CompanionErrorMapping.failure(for: error) == .linkSecurityFailed)
  }

  @Test func encryptionTimeoutIsASecurityFailure() {
    let error = NSError(
      domain: CBErrorDomain,
      code: CBError.Code.encryptionTimedOut.rawValue)
    #expect(CompanionErrorMapping.failure(for: error) == .linkSecurityFailed)
  }

  @Test func otherTransportErrorsAreLinkLosses() {
    let error = NSError(
      domain: CBATTErrorDomain,
      code: CBATTError.Code.attributeNotFound.rawValue)
    #expect(CompanionErrorMapping.failure(for: error) == .linkLost)
    #expect(CompanionErrorMapping.failure(for: NSError(domain: "test", code: 1)) == .linkLost)
  }
}
