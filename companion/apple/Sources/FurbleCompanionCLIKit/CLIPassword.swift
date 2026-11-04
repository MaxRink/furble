import Foundation
import FurbleCompanionCore

public enum CLIPasswordOrigin: String, Equatable, Sendable {
  case argument
  case environment
  case file
  case stdin
  case keychain
  case none
}

public struct CLIPasswordResolution: Equatable, Sendable {
  public let password: String?
  public let origin: CLIPasswordOrigin

  public init(password: String?, origin: CLIPasswordOrigin) {
    self.password = password
    self.origin = origin
  }
}

/// Resolves the explicit password sources without touching the Keychain. The
/// automatic case returns nil so the runner can fall back to the platform
/// Keychain. Errors never include the supplied value.
public enum CLIPasswordResolver {
  public static let defaultEnvironmentVariable = "FURBLE_COMPANION_PASSWORD"

  public static func resolve(
    _ source: CLIPasswordSource,
    environment: [String: String],
    readFile: (String) throws -> String,
    readStdin: () throws -> String
  ) throws -> CLIPasswordResolution? {
    switch source {
    case .automatic:
      let value = environment[defaultEnvironmentVariable] ?? ""
      guard !value.isEmpty else { return nil }
      return try resolution(value, origin: .environment)
    case .argument(let value):
      return try resolution(value, origin: .argument)
    case .environment(let name):
      guard let value = environment[name], !value.isEmpty else {
        throw CLIUsageError.message("environment variable \(name) is not set or is empty")
      }
      return try resolution(value, origin: .environment)
    case .file(let path):
      guard let line = firstLine(try readFile(path)) else {
        throw CLIUsageError.message("password file \(path) is empty")
      }
      return try resolution(line, origin: .file)
    case .stdin:
      guard let line = firstLine(try readStdin()) else {
        throw CLIUsageError.message("standard input did not contain a password")
      }
      return try resolution(line, origin: .stdin)
    }
  }

  /// Keeps a password file's spaces but drops the line terminator. A file or
  /// stdin stream is treated as one password line; later lines are ignored.
  public static func firstLine(_ text: String) -> String? {
    // Swift treats CRLF as a single Character, so normalize line endings
    // before splitting. Only the first password line is used.
    let normalized = text
      .replacingOccurrences(of: "\r\n", with: "\n")
      .replacingOccurrences(of: "\r", with: "\n")
    let line = normalized.split(separator: "\n", maxSplits: 1, omittingEmptySubsequences: false)
      .first.map(String.init) ?? ""
    return line.isEmpty ? nil : line
  }

  private static func resolution(
    _ value: String, origin: CLIPasswordOrigin
  ) throws -> CLIPasswordResolution {
    guard !value.isEmpty else {
      throw CLIUsageError.message("companion password must not be empty")
    }
    guard FurblePasswordPolicy.isValid(value) else {
      throw CLIUsageError.message(
        "companion password exceeds \(FurblePasswordPolicy.maximumUTF8Bytes) UTF-8 bytes")
    }
    return CLIPasswordResolution(password: value, origin: origin)
  }
}

/// A scan-only store. The client never authenticates while automatic connect
/// is disabled, so it never reads this value.
public struct EmptyCredentialStore: FurbleCredentialStore, Sendable {
  public init() {}

  public func readPassword() throws -> String? { nil }

  public func savePassword(_ password: String) throws {
    throw CredentialStoreError.unavailable
  }

  public func deletePassword() throws {
    throw CredentialStoreError.unavailable
  }
}

/// Returns an explicit password without falling back to the Keychain. Only the
/// shared auth path reads it, and it is never formatted into CLI output.
public struct CLICredentialStore: FurbleCredentialStore, Sendable {
  private let password: String

  public init(password: String) {
    self.password = password
  }

  public func readPassword() throws -> String? { password }

  public func savePassword(_ password: String) throws {
    throw CredentialStoreError.unavailable
  }

  public func deletePassword() throws {
    throw CredentialStoreError.unavailable
  }
}
