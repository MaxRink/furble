import XCTest
@testable import FurbleCompanionCore
#if canImport(CryptoKit)
import CryptoKit
#endif

final class FurbleProtocolTests: XCTestCase {
  func testLocationRoundTripUsesFrozen42ByteLayout() throws {
    let fix = FurbleProtocol.LocationFix(
      positionValid: true, timeValid: true, altitudeValid: true,
      satellites: 7, accuracyMeters: 12, latitude: 12.25,
      longitude: -45.5, altitude: 123.75, year: 2026, month: 8,
      day: 16, hour: 14, minute: 15, second: 16, centisecond: 17,
      ageMilliseconds: 0x01020304)
    let data = try FurbleProtocol.encodeLocation(fix)
    XCTAssertEqual(data.count, 42)
    XCTAssertEqual(Array(data[37..<41]), [UInt8(4), 3, 2, 1])
    let decoded = try FurbleProtocol.decodeLocation(data)
    XCTAssertEqual(decoded.positionValid, fix.positionValid)
    XCTAssertEqual(decoded.timeValid, fix.timeValid)
    XCTAssertEqual(decoded.altitudeValid, fix.altitudeValid)
    XCTAssertEqual(decoded.satellites, fix.satellites)
    XCTAssertEqual(decoded.accuracyMeters, fix.accuracyMeters)
    XCTAssertEqual(decoded.latitude, fix.latitude)
    XCTAssertEqual(decoded.longitude, fix.longitude)
    XCTAssertEqual(decoded.altitude, fix.altitude)
    XCTAssertEqual(decoded.year, fix.year)
    XCTAssertEqual(decoded.month, fix.month)
    XCTAssertEqual(decoded.day, fix.day)
    XCTAssertEqual(decoded.hour, fix.hour)
    XCTAssertEqual(decoded.minute, fix.minute)
    XCTAssertEqual(decoded.second, fix.second)
    XCTAssertEqual(decoded.centisecond, fix.centisecond)
    XCTAssertEqual(decoded.ageMilliseconds, fix.ageMilliseconds)
  }

  func testLocationRejectsShortAndInvalidCalendarValues() throws {
    XCTAssertThrowsError(try FurbleProtocol.decodeLocation(Data(repeating: 0, count: 41)))
    let invalid = FurbleProtocol.LocationFix(
      positionValid: true, timeValid: true, altitudeValid: false,
      satellites: 0, accuracyMeters: nil, latitude: 0, longitude: 0,
      altitude: 0, year: 2026, month: 13, day: 1, hour: 0, minute: 0,
      second: 0, centisecond: 0, ageMilliseconds: 0)
    XCTAssertThrowsError(try FurbleProtocol.encodeLocation(invalid))
  }

  func testStatusDecodesSignedAndUnsignedFields() throws {
    let data = Data([1, 85, 0x18, 0x10, 0x88, 0xff, 3, 2, 1, 4, 2, 9, 5, 0xff, 0xff, 4, 3, 2, 1, 0])
    let status = try FurbleProtocol.decodeStatus(data)
    XCTAssertEqual(status.batteryPercent, 85)
    XCTAssertEqual(status.batteryMillivolts, 0x1018)
    XCTAssertEqual(status.batteryMilliamps, -120)
    XCTAssertEqual(status.intervalometerRemaining, 0xffff)
    XCTAssertEqual(status.uptimeSeconds, 0x01020304)
    XCTAssertTrue(status.charging)
    XCTAssertTrue(status.externalPower)
    XCTAssertThrowsError(try FurbleProtocol.decodeStatus(Data(repeating: 0, count: 19)))
  }

  func testCapabilityUsesFrozenCameraBit() throws {
    let capability = try FurbleProtocol.decodeCapability(Data([1, 2, 2, 0, 0, 0]))
    XCTAssertTrue(capability.supportsCameras)
    let settingsOnly = try FurbleProtocol.decodeCapability(Data([1, 2, 1, 0, 0, 0]))
    XCTAssertFalse(settingsOnly.supportsCameras)
  }

  func testTriggerUsesFirmwareLengthForEachOperation() {
    XCTAssertEqual(FurbleProtocol.encodeTrigger(.shutterRelease), Data([1, 0]))
    XCTAssertEqual(FurbleProtocol.encodeTrigger(.focusPress), Data([1, 2]))
    XCTAssertEqual(FurbleProtocol.encodeTrigger(.timedShutter, holdMilliseconds: 0x1234),
      Data([1, 4, 0x34, 0x12]))
  }

  func testSettingsTlvRoundTripsAndRejectsTrailingBytes() throws {
    XCTAssertEqual(FurbleProtocol.settingsListRequest(), Data([0, 0, 0]))
    let setting = try FurbleProtocol.settingsSet(id: 7, value: Data([0xff]))
    XCTAssertEqual(setting, Data([2, 7, 1, 0xff]))
    let response = try FurbleProtocol.decodeSettingResponse(Data([0, 7, 1, 1, 0xfe, 1]))
    XCTAssertEqual(response.id, 7)
    XCTAssertEqual(response.value, Data([0xfe]))
    XCTAssertTrue(response.isListRecord)
    XCTAssertTrue(response.needsRestart)
    XCTAssertThrowsError(try FurbleProtocol.decodeSettingResponse(Data([0, 7, 1, 1, 0xfe, 1, 2])))
  }

  func testCameraRecordIsLengthCheckedAndUtf8Validated() throws {
    let record = try FurbleProtocol.decodeCameraRecord(Data([0, 3, 1, 9, 100, 0xf0, 2, 3, 0x46, 0x75, 0x6a]))
    XCTAssertEqual(record.name, "Fuj")
    XCTAssertThrowsError(try FurbleProtocol.decodeCameraRecord(Data([0, 3, 1, 9, 100, 0, 2, 4, 0x46])))
  }

  func testHmacChallengeIsSingleUseAndLocksAfterFailures() throws {
    var auth = try FurbleAuthSession(password: "secret", maxFailures: 2)
    let nonce = Data((0..<16).map(UInt8.init))
    _ = try auth.begin(nonce: nonce)
    XCTAssertThrowsError(try auth.verify(proof: Data(repeating: 0, count: 16)))
    XCTAssertEqual(auth.state, .awaitingChallenge)
    _ = try auth.begin(nonce: nonce)
    XCTAssertThrowsError(try auth.verify(proof: Data(repeating: 0, count: 16)))
    XCTAssertEqual(auth.state, .lockedOut)
  }

  func testAuthPacketsHaveExplicitOperationsAndFixedLengths() throws {
    XCTAssertEqual(FurbleProtocol.authBegin(), Data([1, 0]))
    let challenge = Data([1, 0]) + Data(repeating: 9, count: 16)
    XCTAssertEqual(try FurbleProtocol.decodeAuthChallenge(challenge), Data(repeating: 9, count: 16))
    let proof = try FurbleProtocol.encodeAuthProof(Data(repeating: 8, count: 16))
    XCTAssertEqual(proof.count, 18)
    XCTAssertEqual(try FurbleProtocol.decodeAuthResult(Data([1, 2, 1])), 1)
    XCTAssertEqual(try FurbleProtocol.decodeAuthResult(Data([1, 2, 4])), 4)
    XCTAssertThrowsError(try FurbleProtocol.decodeAuthResult(Data([1, 2, 0])))
    XCTAssertThrowsError(try FurbleProtocol.encodeAuthProof(Data(repeating: 8, count: 15)))
  }

  func testHmacCorrectProofAuthenticatesAndCannotBeReplayed() throws {
    var auth = try FurbleAuthSession(password: "secret")
    _ = try auth.begin(nonce: Data(repeating: 7, count: 16))
    #if canImport(CryptoKit)
    let proof = Data(HMAC<SHA256>.authenticationCode(for: Data(repeating: 7, count: 16), using: SymmetricKey(data: Data("secret".utf8))).prefix(16))
    try auth.verify(proof: proof)
    XCTAssertEqual(auth.state, .authenticated)
    XCTAssertThrowsError(try auth.verify(proof: proof))
    #else
    throw XCTSkip("CryptoKit is unavailable on this host")
    #endif
  }
}
