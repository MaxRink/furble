import Foundation

#if canImport(CryptoKit)
import CryptoKit
#endif

/// The versioned BLE contract shared by iOS, macOS, Android and firmware.
/// Every decoder is length checked before reading a field. Unknown trailing
/// fields are ignored, but unknown versions and malformed records are rejected.
public enum FurbleProtocol {
  public static let version: UInt8 = 1
  public static let capabilityVersion: UInt8 = 1
  public static let settingsWireVersion: UInt8 = 2
  public static let locationPacketSize = 42
  public static let statusPacketSize = 20
  public static let triggerPacketSize = 4
  public static let capabilityPacketSize = 6
  public static let authNonceSize = 16
  public static let authProofSize = 16
  public static let maxPayloadSize = 512

  public enum UUIDs {
    public static let service = "b57f4f5e-087b-4740-b71d-8262cf26ebbc"
    public static let location = "b57f4f5f-087b-4740-b71d-8262cf26ebbc"
    public static let status = "b57f4f60-087b-4740-b71d-8262cf26ebbc"
    public static let settings = "b57f4f61-087b-4740-b71d-8262cf26ebbc"
    public static let trigger = "b57f4f62-087b-4740-b71d-8262cf26ebbc"
    public static let capability = "b57f4f64-087b-4740-b71d-8262cf26ebbc"
    public static let otaControl = "b57f4f6d-087b-4740-b71d-8262cf26ebbc"
    public static let otaData = "b57f4f6e-087b-4740-b71d-8262cf26ebbc"
    /// Reserved by the Apple client for the password challenge follow-up.
    /// A client must not treat a missing Auth characteristic as success.
    public static let auth = "b57f4f6f-087b-4740-b71d-8262cf26ebbc"
    public static let cameras = "b57f4f63-087b-4740-b71d-8262cf26ebbc"
  }

  public enum CapabilityFeature {
    public static let settingsV2: UInt32 = 1 << 0
    public static let authHMAC: UInt32 = 1 << 1
    public static let cameras: UInt32 = 1 << 2
  }

  public enum Error: Swift.Error, Equatable {
    case malformed
    case unsupportedVersion(UInt8)
    case invalidValue(String)
    case authenticationRequired
    case authenticationFailed
    case authenticationUnavailable
    case payloadTooLarge
  }

  public struct LocationFix: Equatable, Sendable {
    public var positionValid: Bool
    public var timeValid: Bool
    public var altitudeValid: Bool
    public var satellites: UInt8
    public var accuracyMeters: UInt8?
    public var latitude: Double
    public var longitude: Double
    public var altitude: Double
    public var year: UInt16
    public var month: UInt8
    public var day: UInt8
    public var hour: UInt8
    public var minute: UInt8
    public var second: UInt8
    public var centisecond: UInt8
    public var ageMilliseconds: UInt32

    public init(
      positionValid: Bool,
      timeValid: Bool,
      altitudeValid: Bool,
      satellites: UInt8,
      accuracyMeters: UInt8?,
      latitude: Double,
      longitude: Double,
      altitude: Double,
      year: UInt16,
      month: UInt8,
      day: UInt8,
      hour: UInt8,
      minute: UInt8,
      second: UInt8,
      centisecond: UInt8,
      ageMilliseconds: UInt32
    ) {
      self.positionValid = positionValid
      self.timeValid = timeValid
      self.altitudeValid = altitudeValid
      self.satellites = satellites
      self.accuracyMeters = accuracyMeters
      self.latitude = latitude
      self.longitude = longitude
      self.altitude = altitude
      self.year = year
      self.month = month
      self.day = day
      self.hour = hour
      self.minute = minute
      self.second = second
      self.centisecond = centisecond
      self.ageMilliseconds = ageMilliseconds
    }
  }

  public struct Status: Equatable, Sendable {
    public let version: UInt8
    public let batteryPercent: UInt8
    public let batteryMillivolts: UInt16
    public let batteryMilliamps: Int16
    public let powerFlags: UInt8
    public let cameraTotal: UInt8
    public let cameraConnected: UInt8
    public let controlState: UInt8
    public let gpsSource: UInt8
    public let gpsSatellites: UInt8
    public let intervalometerState: UInt8
    public let intervalometerRemaining: UInt16
    public let uptimeSeconds: UInt32

    public var charging: Bool { powerFlags & 1 != 0 }
    public var externalPower: Bool { powerFlags & 2 != 0 }
  }

  public struct Capability: Equatable, Sendable {
    public let version: UInt8
    public let wireVersion: UInt8
    public let features: UInt32

    public var supportsSettings: Bool {
      version >= capabilityVersion && wireVersion >= settingsWireVersion &&
        features & CapabilityFeature.settingsV2 != 0
    }

    public var supportsAuthentication: Bool {
      version >= capabilityVersion && features & CapabilityFeature.authHMAC != 0
    }
  }

  public enum SettingType: UInt8, Sendable {
    case bool = 0
    case uint8 = 1
    case uint32 = 2
    case string = 3
    case blob = 4
  }

  public struct SettingResponse: Equatable, Sendable {
    public let status: UInt8
    public let id: UInt8
    public let type: SettingType?
    public let value: Data
    public let flags: UInt8
    public let isListRecord: Bool

    public var isTerminator: Bool { id == 0xff }
    public var needsRestart: Bool { flags & 1 != 0 }
    public var dangerous: Bool { flags & 2 != 0 }
  }

  public enum TriggerOperation: UInt8, Sendable {
    case shutterRelease = 0
    case shutterPress = 1
    case focusPress = 2
    case focusRelease = 3
    case timedShutter = 4
  }

  public struct CameraRecord: Equatable, Sendable {
    public let status: UInt8
    public let cameraID: UInt8
    public let cameraType: UInt8
    public let flags: UInt8
    public let progress: UInt8
    public let rssi: Int8
    public let state: UInt8
    public let name: String
    public var isTerminator: Bool { cameraID == 0xff }
  }

  public enum CameraOperation: UInt8, Sendable {
    case list = 0
    case connect = 1
    case disconnect = 2
    case select = 3
    case deselect = 4
  }

  public static func encodeLocation(_ fix: LocationFix) throws -> Data {
    guard fix.month <= 12, fix.day <= 31, fix.hour <= 23,
      fix.minute <= 59, fix.second <= 60, fix.centisecond <= 99,
      fix.latitude.isFinite, fix.longitude.isFinite, fix.altitude.isFinite else {
      throw Error.invalidValue("location fields out of range")
    }
    var writer = Writer(capacity: locationPacketSize)
    let flags = (fix.positionValid ? 1 : 0) | (fix.timeValid ? 2 : 0) |
      (fix.altitudeValid ? 4 : 0)
    writer.u8(version)
    writer.u8(UInt8(flags))
    writer.u8(fix.satellites)
    writer.u8(fix.accuracyMeters ?? 255)
    writer.f64(fix.latitude)
    writer.f64(fix.longitude)
    writer.f64(fix.altitude)
    writer.u16(fix.year)
    writer.u8(fix.month)
    writer.u8(fix.day)
    writer.u8(fix.hour)
    writer.u8(fix.minute)
    writer.u8(fix.second)
    writer.u8(fix.centisecond)
    writer.u8(0)
    writer.u32(fix.ageMilliseconds)
    writer.u8(0)
    return writer.data
  }

  public static func decodeLocation(_ data: Data) throws -> LocationFix {
    guard data.count >= locationPacketSize else { throw Error.malformed }
    var reader = Reader(data)
    let wireVersion = try reader.u8()
    guard wireVersion >= version else { throw Error.unsupportedVersion(wireVersion) }
    let flags = try reader.u8()
    let satellites = try reader.u8()
    let accuracy = try reader.u8()
    let fix = LocationFix(
      positionValid: flags & 1 != 0,
      timeValid: flags & 2 != 0,
      altitudeValid: flags & 4 != 0,
      satellites: satellites,
      accuracyMeters: accuracy == 255 ? nil : accuracy,
      latitude: try reader.f64(),
      longitude: try reader.f64(),
      altitude: try reader.f64(),
      year: try reader.u16(),
      month: try reader.u8(),
      day: try reader.u8(),
      hour: try reader.u8(),
      minute: try reader.u8(),
      second: try reader.u8(),
      centisecond: try reader.u8(),
      ageMilliseconds: 0
    )
    _ = try reader.u8()
    let age = try reader.u32()
    _ = try reader.u8()
    guard fix.month <= 12, fix.day <= 31, fix.hour <= 23,
      fix.minute <= 59, fix.second <= 60, fix.centisecond <= 99,
      fix.latitude.isFinite, fix.longitude.isFinite, fix.altitude.isFinite else {
      throw Error.invalidValue("location fields out of range")
    }
    var result = fix
    result.ageMilliseconds = age
    return result
  }

  public static func decodeStatus(_ data: Data) throws -> Status {
    guard data.count >= statusPacketSize - 1 else { throw Error.malformed }
    var reader = Reader(data)
    let wireVersion = try reader.u8()
    guard wireVersion >= version else { throw Error.unsupportedVersion(wireVersion) }
    return Status(
      version: wireVersion,
      batteryPercent: try reader.u8(),
      batteryMillivolts: try reader.u16(),
      batteryMilliamps: try reader.i16(),
      powerFlags: try reader.u8(),
      cameraTotal: try reader.u8(),
      cameraConnected: try reader.u8(),
      controlState: try reader.u8(),
      gpsSource: try reader.u8(),
      gpsSatellites: try reader.u8(),
      intervalometerState: try reader.u8(),
      intervalometerRemaining: try reader.u16(),
      uptimeSeconds: try reader.u32()
    )
  }

  public static func decodeCapability(_ data: Data) throws -> Capability {
    guard data.count >= capabilityPacketSize else { throw Error.malformed }
    var reader = Reader(data)
    let wireVersion = try reader.u8()
    guard wireVersion >= capabilityVersion else { throw Error.unsupportedVersion(wireVersion) }
    return Capability(version: wireVersion, wireVersion: try reader.u8(), features: try reader.u32())
  }

  public static func encodeTrigger(_ operation: TriggerOperation, holdMilliseconds: UInt16 = 0) -> Data {
    var writer = Writer(capacity: triggerPacketSize)
    writer.u8(version)
    writer.u8(operation.rawValue)
    writer.u16(holdMilliseconds)
    return writer.data
  }

  public static func settingsListRequest() -> Data { settingsRequest(op: 0, id: 0, value: Data()) }
  public static func settingsGet(id: UInt8) -> Data { settingsRequest(op: 1, id: id, value: Data()) }
  public static func settingsSet(id: UInt8, value: Data) throws -> Data {
    guard value.count <= 255 else { throw Error.payloadTooLarge }
    return settingsRequest(op: 2, id: id, value: value)
  }

  private static func settingsRequest(op: UInt8, id: UInt8, value: Data) -> Data {
    var writer = Writer(capacity: 3 + value.count)
    writer.u8(op)
    writer.u8(id)
    writer.u8(UInt8(value.count))
    writer.bytes(value)
    return writer.data
  }

  public static func decodeSettingResponse(_ data: Data) throws -> SettingResponse {
    guard data.count >= 4 else { throw Error.malformed }
    var reader = Reader(data)
    let status = try reader.u8()
    let id = try reader.u8()
    let rawType = try reader.u8()
    let type = SettingType(rawValue: rawType)
    let length = Int(try reader.u8())
    guard length <= reader.remaining else { throw Error.malformed }
    let value = try reader.bytes(length)
    let isList = reader.remaining == 1 || id == 0xff
    let flags = reader.remaining == 1 ? try reader.u8() : 0
    guard reader.remaining == 0 else { throw Error.malformed }
    return SettingResponse(status: status, id: id, type: type, value: value, flags: flags, isListRecord: isList)
  }

  public static func cameraListRequest() -> Data { cameraRequest(.list, id: 0xff) }
  public static func cameraRequest(_ operation: CameraOperation, id: UInt8 = 0xff) -> Data {
    Data([operation.rawValue, id])
  }

  public static func decodeCameraRecord(_ data: Data) throws -> CameraRecord {
    guard data.count >= 8 else { throw Error.malformed }
    var reader = Reader(data)
    let status = try reader.u8()
    let cameraID = try reader.u8()
    let cameraType = try reader.u8()
    let flags = try reader.u8()
    let progress = try reader.u8()
    let rssi = try reader.i8()
    let state = try reader.u8()
    let nameLength = Int(try reader.u8())
    let rawName = try reader.bytes(nameLength)
    guard let name = String(data: rawName, encoding: .utf8) else { throw Error.malformed }
    return CameraRecord(status: status, cameraID: cameraID, cameraType: cameraType, flags: flags,
      progress: progress, rssi: rssi, state: state, name: name)
  }

  private struct Writer {
    var data = Data()
    init(capacity: Int) { data.reserveCapacity(capacity) }
    mutating func u8(_ value: UInt8) { data.append(value) }
    mutating func u16(_ value: UInt16) { data.append(contentsOf: [UInt8(value & 0xff), UInt8(value >> 8)]) }
    mutating func u32(_ value: UInt32) { data.append(contentsOf: (0..<4).map { UInt8(value >> (8 * $0)) }) }
    mutating func f64(_ value: Double) { u64(value.bitPattern) }
    mutating func u64(_ value: UInt64) { data.append(contentsOf: (0..<8).map { UInt8(value >> (8 * $0)) }) }
    mutating func bytes(_ value: Data) { data.append(value) }
  }

  private struct Reader {
    let data: Data
    var offset = 0
    init(_ data: Data) { self.data = data }
    var remaining: Int { data.count - offset }
    mutating func take(_ count: Int) throws -> Data {
      guard count >= 0, count <= remaining else { throw Error.malformed }
      let result = data.subdata(in: offset..<(offset + count))
      offset += count
      return result
    }
    mutating func u8() throws -> UInt8 { try take(1)[0] }
    mutating func i8() throws -> Int8 { Int8(bitPattern: try u8()) }
    mutating func u16() throws -> UInt16 {
      let b = try take(2); return UInt16(b[0]) | UInt16(b[1]) << 8
    }
    mutating func i16() throws -> Int16 { Int16(bitPattern: try u16()) }
    mutating func u32() throws -> UInt32 {
      let b = try take(4)
      return UInt32(b[0]) | UInt32(b[1]) << 8 | UInt32(b[2]) << 16 | UInt32(b[3]) << 24
    }
    mutating func f64() throws -> Double { Double(bitPattern: try u64()) }
    mutating func u64() throws -> UInt64 {
      let b = try take(8)
      return (0..<8).reduce(UInt64(0)) { $0 | UInt64(b[$1]) << (8 * $1) }
    }
    mutating func bytes(_ count: Int) throws -> Data { try take(count) }
  }
}

public struct FurbleAuthSession: Sendable {
  public enum State: Equatable, Sendable {
    case awaitingChallenge
    case challenged
    case authenticated
    case lockedOut
  }

  public private(set) var state: State = .awaitingChallenge
  public private(set) var failures = 0
  public let maxFailures: Int
  private let password: Data
  private var nonce: Data?

  public init(password: String, maxFailures: Int = 3) throws {
    guard !password.isEmpty else { throw FurbleProtocol.Error.authenticationUnavailable }
    guard maxFailures > 0 else { throw FurbleProtocol.Error.invalidValue("maxFailures") }
    self.password = Data(password.utf8)
    self.maxFailures = maxFailures
  }

  public mutating func begin(nonce: Data) throws -> Data {
    guard nonce.count == FurbleProtocol.authNonceSize else { throw FurbleProtocol.Error.malformed }
    guard state != .lockedOut else { throw FurbleProtocol.Error.authenticationFailed }
    self.nonce = nonce
    state = .challenged
    return try Self.proof(password: password, nonce: nonce)
  }

  public mutating func verify(proof: Data) throws {
    guard state == .challenged, let nonce else { throw FurbleProtocol.Error.authenticationRequired }
    let expected = try Self.proof(password: password, nonce: nonce)
    self.nonce = nil
    guard proof.count == expected.count, Self.constantTimeEqual(proof, expected) else {
      failures += 1
      state = failures >= maxFailures ? .lockedOut : .awaitingChallenge
      throw FurbleProtocol.Error.authenticationFailed
    }
    state = .authenticated
  }

  /// Used after the firmware has accepted the proof. The phone never marks
  /// itself ready from a local comparison, only from the authenticated BLE
  /// response.
  public mutating func markAuthenticated() throws {
    guard state == .challenged else { throw FurbleProtocol.Error.authenticationRequired }
    state = .authenticated
    nonce = nil
  }

  public func requireAuthenticated() throws {
    guard state == .authenticated else { throw FurbleProtocol.Error.authenticationRequired }
  }

  #if canImport(CryptoKit)
  private static func proof(password: Data, nonce: Data) throws -> Data {
    let mac = HMAC<SHA256>.authenticationCode(for: nonce, using: SymmetricKey(data: password))
    return Data(mac.prefix(FurbleProtocol.authProofSize))
  }
  #else
  private static func proof(password: Data, nonce: Data) throws -> Data {
    throw FurbleProtocol.Error.authenticationUnavailable
  }
  #endif

  private static func constantTimeEqual(_ lhs: Data, _ rhs: Data) -> Bool {
    guard lhs.count == rhs.count else { return false }
    var result: UInt8 = 0
    for (left, right) in zip(lhs, rhs) { result |= left ^ right }
    return result == 0
  }
}
