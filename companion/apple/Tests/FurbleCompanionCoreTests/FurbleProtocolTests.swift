import XCTest
@testable import FurbleCompanionCore
#if canImport(CryptoKit)
import CryptoKit
#endif

final class FurbleProtocolTests: XCTestCase {
  private func canonicalFixture(_ name: String) throws -> Data {
    var root = URL(fileURLWithPath: #filePath)
    let relativePath = "tests/protocol/golden/companion_auth.json"
    var url = root.appendingPathComponent(relativePath)
    for _ in 0..<8 where !FileManager.default.fileExists(atPath: url.path) {
      root.deleteLastPathComponent()
      url = root.appendingPathComponent(relativePath)
    }
    guard FileManager.default.fileExists(atPath: url.path) else {
      throw FurbleProtocol.Error.malformed
    }
    let text = try String(contentsOf: url, encoding: .utf8)
    let pattern = "\"\(name)\"\\s*:\\s*\"([^\"]+)\""
    let regex = try NSRegularExpression(pattern: pattern)
    let range = NSRange(text.startIndex..<text.endIndex, in: text)
    guard let match = regex.firstMatch(in: text, range: range),
      let valueRange = Range(match.range(at: 1), in: text) else {
      throw FurbleProtocol.Error.malformed
    }
    let value = String(text[valueRange])
    var data = Data()
    for index in stride(from: 0, to: value.count, by: 2) {
      let start = value.index(value.startIndex, offsetBy: index)
      let end = value.index(start, offsetBy: 2)
      guard let byte = UInt8(value[start..<end], radix: 16) else {
        throw FurbleProtocol.Error.malformed
      }
      data.append(byte)
    }
    return data
  }

  func testCanonicalAuthFixtureIsConsumed() throws {
    XCTAssertEqual(FurbleProtocol.authBegin(), try canonicalFixture("begin"))
    let nonce = try canonicalFixture("nonce")
    XCTAssertEqual(try FurbleProtocol.decodeAuthChallenge(try canonicalFixture("challenge")), nonce)
    #if canImport(CryptoKit)
    let password = Data("correct horse battery staple".utf8)
    let proof = Data(HMAC<SHA256>.authenticationCode(
      for: nonce, using: SymmetricKey(data: password)).prefix(16))
    XCTAssertEqual(try FurbleProtocol.encodeAuthProof(proof), try canonicalFixture("proof"))
    #else
    throw XCTSkip("CryptoKit is unavailable on this host")
    #endif
    XCTAssertEqual(try FurbleProtocol.decodeAuthResult(try canonicalFixture("result_authenticated")), 1)
  }

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

  func testPasswordUsesFirmwareUtf8ByteLimit() {
    XCTAssertThrowsError(try FurbleAuthSession(password: String(repeating: "é", count: 32)))
    XCTAssertNoThrow(try FurbleAuthSession(password: String(repeating: "é", count: 31) + "a"))
  }

  func testPasswordOnboardingValidatesBytesAndConfirmationWithoutStoringSecret() {
    var form = PasswordOnboardingState()
    XCTAssertFalse(form.validate(password: "", confirmation: ""))
    XCTAssertEqual(form.validation, .empty)
    XCTAssertFalse(form.validate(password: String(repeating: "é", count: 32), confirmation: ""))
    XCTAssertEqual(form.validation, .tooLong)
    XCTAssertFalse(form.validate(password: "secret", confirmation: "different"))
    XCTAssertEqual(form.validation, .mismatch)
    XCTAssertTrue(form.validate(password: String(repeating: "é", count: 31) + "a",
      confirmation: String(repeating: "é", count: 31) + "a"))
    XCTAssertEqual(form.validation, .none)
    XCTAssertFalse(form.hasStoredPassword)
  }

  func testPasswordOnboardingClearAndDeleteForgetOnlyPresence() {
    var form = PasswordOnboardingState()
    form.setStoredPassword(true)
    XCTAssertTrue(form.hasStoredPassword)
    form.didDelete()
    XCTAssertFalse(form.hasStoredPassword)
    XCTAssertEqual(form.validation, .none)
    form.didSave()
    XCTAssertTrue(form.hasStoredPassword)
    form.setStoredPassword(false)
    XCTAssertFalse(form.hasStoredPassword)
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

  func testAuthProofMatchesAndroidFirmwareGoldenVector() throws {
    #if canImport(CryptoKit)
    let nonce = Data((0..<16).map(UInt8.init))
    let password = Data("correct horse battery staple".utf8)
    let proof = Data(HMAC<SHA256>.authenticationCode(
      for: nonce, using: SymmetricKey(data: password)).prefix(16))
    let packet = try FurbleProtocol.encodeAuthProof(proof)
    XCTAssertEqual(packet, Data([
      1, 1, 0xc5, 0xdf, 0xbf, 0x65, 0x5b, 0xbc, 0xd0, 0x90,
      0xec, 0xb1, 0xa5, 0xbf, 0x71, 0x68, 0xb8, 0x43
    ]))
    #else
    throw XCTSkip("CryptoKit is unavailable on this host")
    #endif
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
