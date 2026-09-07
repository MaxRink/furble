import Foundation

#if canImport(Security)
import Security
#endif

public protocol FurbleCredentialStore: Sendable {
  func readPassword() throws -> String?
  func savePassword(_ password: String) throws
  func deletePassword() throws
}

public enum CredentialStoreError: Error, Equatable {
  case unavailable
  case invalidPassword
  case keychain(OSStatus)
}

/// Passwords never live in UserDefaults or a plist. The access group can be
/// supplied by an app target when sharing credentials between its iOS and
/// macOS companion products.
public struct KeychainCredentialStore: FurbleCredentialStore, Sendable {
  public let service: String
  public let account: String
  public let accessGroup: String?

  public init(service: String = "com.furble.companion", account: String = "furble-password",
              accessGroup: String? = nil) {
    self.service = service
    self.account = account
    self.accessGroup = accessGroup
  }

  public func readPassword() throws -> String? {
    #if canImport(Security)
    var query = baseQuery
    query[kSecReturnData as String] = true
    query[kSecMatchLimit as String] = kSecMatchLimitOne
    var result: CFTypeRef?
    let status = SecItemCopyMatching(query as CFDictionary, &result)
    if status == errSecItemNotFound { return nil }
    guard status == errSecSuccess, let data = result as? Data,
      let value = String(data: data, encoding: .utf8) else {
      throw CredentialStoreError.keychain(status)
    }
    return value
    #else
    throw CredentialStoreError.unavailable
    #endif
  }

  public func savePassword(_ password: String) throws {
    guard !password.isEmpty, password.utf8.count <= 64 else {
      throw CredentialStoreError.invalidPassword
    }
    #if canImport(Security)
    let data = Data(password.utf8)
    let status = SecItemUpdate(baseQuery as CFDictionary,
      [kSecValueData as String: data] as CFDictionary)
    if status == errSecItemNotFound {
      var item = baseQuery
      item[kSecValueData as String] = data
      item[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
      let addStatus = SecItemAdd(item as CFDictionary, nil)
      guard addStatus == errSecSuccess else { throw CredentialStoreError.keychain(addStatus) }
    } else if status != errSecSuccess {
      throw CredentialStoreError.keychain(status)
    }
    #else
    throw CredentialStoreError.unavailable
    #endif
  }

  public func deletePassword() throws {
    #if canImport(Security)
    let status = SecItemDelete(baseQuery as CFDictionary)
    guard status == errSecSuccess || status == errSecItemNotFound else {
      throw CredentialStoreError.keychain(status)
    }
    #else
    throw CredentialStoreError.unavailable
    #endif
  }

  #if canImport(Security)
  private var baseQuery: [String: Any] {
    var query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: service,
      kSecAttrAccount as String: account
    ]
    if let accessGroup { query[kSecAttrAccessGroup as String] = accessGroup }
    return query
  }
  #endif
}
