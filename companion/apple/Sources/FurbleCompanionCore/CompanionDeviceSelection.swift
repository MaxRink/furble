import Foundation

/// A furble companion peripheral discovered during a scan. macOS exposes a
/// per-host peripheral UUID rather than the BLE MAC address, so the UUID is the
/// stable local identity. The advertised name is the fallback when a peripheral
/// does not publish a local name.
public struct FurbleDiscoveredDevice: Equatable, Sendable, Identifiable {
  public let id: UUID
  public let name: String?
  public let rssi: Int

  public init(id: UUID, name: String?, rssi: Int) {
    self.id = id
    self.name = name
    self.rssi = rssi
  }

  public var displayName: String {
    guard let name, !name.isEmpty else { return "unnamed furble" }
    return name
  }
}

/// Selects which discovered peripheral the client may connect to. The matcher
/// is pure so scan filtering and CLI device selection are host tested without
/// a radio.
public enum FurbleDeviceSelection: Equatable, Sendable {
  /// Connect to the first discovered furble peripheral.
  case any
  /// Connect when the advertised name contains the needle. Matching ignores
  /// case and diacritics. An empty needle never matches.
  case nameContains(String)
  /// Connect only to the CoreBluetooth peripheral UUID shown by a previous
  /// scan on this Mac.
  case identifier(UUID)

  public func matches(name: String?, identifier: UUID) -> Bool {
    switch self {
    case .any:
      return true
    case .identifier(let expected):
      return expected == identifier
    case .nameContains(let needle):
      guard !needle.isEmpty, let name, !name.isEmpty else { return false }
      return name.range(
        of: needle,
        options: [.caseInsensitive, .diacriticInsensitive]
      ) != nil
    }
  }

  public func matches(_ device: FurbleDiscoveredDevice) -> Bool {
    matches(name: device.name, identifier: device.id)
  }
}
