import Foundation

/// The firmware stores the companion password as a length-delimited UTF-8
/// value. Keep this policy in the shared core so both native targets validate
/// exactly the same bytes before touching the keychain or starting HMAC.
public enum FurblePasswordPolicy {
  public static let maximumUTF8Bytes = FurbleProtocol.authPasswordMaxBytes

  public static func isValid(_ password: String) -> Bool {
    !password.isEmpty && password.utf8.count <= maximumUTF8Bytes
  }

  public static func byteCount(_ password: String) -> Int {
    password.utf8.count
  }
}

public enum PasswordOnboardingValidation: Equatable, Sendable {
  case none
  case empty
  case tooLong
  case mismatch
  case storageUnavailable
  case saveFailed
}

/// Small, platform-neutral state holder for the password form. Keeping the
/// form transitions here makes clear/delete behavior testable without a
/// keychain, SwiftUI, or a Bluetooth radio. It intentionally contains only
/// whether a credential exists, never the credential itself.
public struct PasswordOnboardingState: Equatable, Sendable {
  public private(set) var hasStoredPassword = false
  public private(set) var validation: PasswordOnboardingValidation = .none

  public init() {}

  public mutating func setStoredPassword(_ present: Bool) {
    hasStoredPassword = present
    validation = .none
  }

  @discardableResult
  public mutating func validate(password: String, confirmation: String) -> Bool {
    guard !password.isEmpty else {
      validation = .empty
      return false
    }
    guard FurblePasswordPolicy.isValid(password) else {
      validation = .tooLong
      return false
    }
    guard password == confirmation else {
      validation = .mismatch
      return false
    }
    validation = .none
    return true
  }

  public mutating func didSave() {
    hasStoredPassword = true
    validation = .none
  }

  public mutating func didDelete() {
    hasStoredPassword = false
    validation = .none
  }

  public mutating func storageUnavailable() {
    validation = .storageUnavailable
  }

  public mutating func saveFailed() {
    validation = .saveFailed
  }
}
