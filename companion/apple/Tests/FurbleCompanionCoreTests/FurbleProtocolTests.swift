import CryptoKit
import Foundation
import Testing
@testable import FurbleCompanionCore

struct FurbleProtocolTests {
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

  @Test func canonicalAuthFixtureIsConsumed() throws {
    #expect(FurbleProtocol.authBegin() == (try canonicalFixture("begin")))
    let nonce = try canonicalFixture("nonce")
    #expect(try FurbleProtocol.decodeAuthChallenge(try canonicalFixture("challenge")) == nonce)
    let password = Data("correct horse battery staple".utf8)
    let proof = Data(HMAC<SHA256>.authenticationCode(
      for: nonce, using: SymmetricKey(data: password)).prefix(16))
    #expect(try FurbleProtocol.encodeAuthProof(proof) == (try canonicalFixture("proof")))
    #expect(try FurbleProtocol.decodeAuthResult(try canonicalFixture("result_authenticated")) == 1)
  }

  @Test func locationRoundTripUsesFrozen42ByteLayout() throws {
    let fix = FurbleProtocol.LocationFix(
      positionValid: true, timeValid: true, altitudeValid: true,
      satellites: 7, accuracyMeters: 12, latitude: 12.25,
      longitude: -45.5, altitude: 123.75, year: 2026, month: 8,
      day: 16, hour: 14, minute: 15, second: 16, centisecond: 17,
      ageMilliseconds: 0x01020304)
    let data = try FurbleProtocol.encodeLocation(fix)
    #expect(data.count == 42)
    #expect(Array(data[37..<41]) == [UInt8(4), 3, 2, 1])
    let decoded = try FurbleProtocol.decodeLocation(data)
    #expect(decoded.positionValid == fix.positionValid)
    #expect(decoded.timeValid == fix.timeValid)
    #expect(decoded.altitudeValid == fix.altitudeValid)
    #expect(decoded.satellites == fix.satellites)
    #expect(decoded.accuracyMeters == fix.accuracyMeters)
    #expect(decoded.latitude == fix.latitude)
    #expect(decoded.longitude == fix.longitude)
    #expect(decoded.altitude == fix.altitude)
    #expect(decoded.year == fix.year)
    #expect(decoded.month == fix.month)
    #expect(decoded.day == fix.day)
    #expect(decoded.hour == fix.hour)
    #expect(decoded.minute == fix.minute)
    #expect(decoded.second == fix.second)
    #expect(decoded.centisecond == fix.centisecond)
    #expect(decoded.ageMilliseconds == fix.ageMilliseconds)
  }

  @Test func locationRejectsShortAndInvalidCalendarValues() throws {
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeLocation(Data(repeating: 0, count: 41))
    }
    let invalid = FurbleProtocol.LocationFix(
      positionValid: true, timeValid: true, altitudeValid: false,
      satellites: 0, accuracyMeters: nil, latitude: 0, longitude: 0,
      altitude: 0, year: 2026, month: 13, day: 1, hour: 0, minute: 0,
      second: 0, centisecond: 0, ageMilliseconds: 0)
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.encodeLocation(invalid)
    }
  }

  @Test func statusDecodesSignedAndUnsignedFields() throws {
    let data = Data([1, 85, 0x18, 0x10, 0x88, 0xff, 3, 2, 1, 4, 2, 9, 5, 0xff, 0xff, 4, 3, 2, 1, 0])
    let status = try FurbleProtocol.decodeStatus(data)
    #expect(status.batteryPercent == 85)
    #expect(status.batteryMillivolts == 0x1018)
    #expect(status.batteryMilliamps == -120)
    #expect(status.intervalometerRemaining == 0xffff)
    #expect(status.uptimeSeconds == 0x01020304)
    #expect(status.charging)
    #expect(status.externalPower)
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeStatus(Data(repeating: 0, count: 19))
    }
  }

  @Test func capabilityUsesFrozenCameraBit() throws {
    let capability = try FurbleProtocol.decodeCapability(Data([1, 2, 2, 0, 0, 0]))
    #expect(capability.supportsCameras)
    let settingsOnly = try FurbleProtocol.decodeCapability(Data([1, 2, 1, 0, 0, 0]))
    #expect(!settingsOnly.supportsCameras)
  }

  @Test func triggerUsesFirmwareLengthForEachOperation() {
    #expect(FurbleProtocol.encodeTrigger(.shutterRelease) == Data([1, 0]))
    #expect(FurbleProtocol.encodeTrigger(.focusPress) == Data([1, 2]))
    #expect(FurbleProtocol.encodeTrigger(.timedShutter, holdMilliseconds: 0x1234)
      == Data([1, 4, 0x34, 0x12]))
  }

  @Test func triggerHoldStatePairsEveryPressWithOneRelease() {
    var state = TriggerHoldState()
    let firstPress = state.pressShutter()
    #expect(firstPress == .shutterPress)
    let duplicatePress = state.pressShutter()
    #expect(duplicatePress == nil)
    let firstRelease = state.releaseShutter()
    #expect(firstRelease == .shutterRelease)
    let duplicateRelease = state.releaseShutter()
    #expect(duplicateRelease == nil)
    let focusPress = state.pressFocus()
    #expect(focusPress == .focusPress)
    let released = state.releaseAll()
    #expect(released == [.focusRelease])
    #expect(!state.shutterHeld)
    #expect(!state.focusHeld)
  }

  @Test func triggerHoldStateRestoresStateAfterFailedWrite() {
    var state = TriggerHoldState()
    let press = state.pressShutter()
    #expect(press == .shutterPress)
    state.restore(after: .shutterPress)
    #expect(!state.shutterHeld)
    let pressedAgain = state.pressShutter()
    #expect(pressedAgain == .shutterPress)
    state.restore(after: .shutterRelease)
    #expect(state.shutterHeld)
  }

  @Test func settingsTlvRoundTripsAndRejectsTrailingBytes() throws {
    #expect(FurbleProtocol.settingsListRequest() == Data([0, 0, 0]))
    let setting = try FurbleProtocol.settingsSet(id: 7, value: Data([0xff]))
    #expect(setting == Data([2, 7, 1, 0xff]))
    let response = try FurbleProtocol.decodeSettingResponse(Data([0, 7, 1, 1, 0xfe, 1]))
    #expect(response.id == 7)
    #expect(response.value == Data([0xfe]))
    #expect(response.isListRecord)
    #expect(response.needsRestart)
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeSettingResponse(Data([0, 7, 1, 1, 0xfe, 1, 2]))
    }
  }

  @Test func cameraRecordIsLengthCheckedAndUtf8Validated() throws {
    let record = try FurbleProtocol.decodeCameraRecord(Data([0, 3, 1, 9, 100, 0xf0, 2, 3, 0x46, 0x75, 0x6a]))
    #expect(record.name == "Fuj")
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeCameraRecord(Data([0, 3, 1, 9, 100, 0, 2, 4, 0x46]))
    }
  }

  @Test func cameraRecordExposesStableSelectionAndStateLabels() throws {
    let record = try FurbleProtocol.decodeCameraRecord(
      Data([0, 3, 1, 3, 25, 0xf0, 1, 3, 0x46, 0x75, 0x6a]))
    #expect(record.isSaved)
    #expect(record.isSelected)
    #expect(!record.isConnected)
    #expect(record.typeLabel == "Fujifilm")
    #expect(record.connectionStateLabel == "connecting 25%")
    #expect(record.operationStatusLabel == nil)

    let busy = try FurbleProtocol.decodeCameraRecord(
      Data([3, 3, 1, 8, 0, 0x80, 0, 3, 0x46, 0x75, 0x6a]))
    #expect(busy.operationStatusLabel == "camera operation busy")
  }

  @Test func cameraAcknowledgementIsNotARealCatalogRecord() throws {
    let acknowledgement = try FurbleProtocol.decodeCameraRecord(
      Data([3, 7, 0, 0, 0, 0x80, 0, 0]))
    #expect(acknowledgement.isOperationAcknowledgement)
    #expect(acknowledgement.operationStatusLabel == "camera operation busy")

    let record = try FurbleProtocol.decodeCameraRecord(
      Data([0, 7, 1, 1, 0, 0xe0, 2, 3, 0x58, 0x2d, 0x54]))
    #expect(!record.isOperationAcknowledgement)
  }

  @Test func hmacChallengeIsSingleUseAndLocksAfterFailures() throws {
    var auth = try FurbleAuthSession(password: "secret", maxFailures: 2)
    let nonce = Data((0..<16).map(UInt8.init))
    _ = try auth.begin(nonce: nonce)
    #expect(throws: FurbleProtocol.Error.self) {
      try auth.verify(proof: Data(repeating: 0, count: 16))
    }
    #expect(auth.state == .awaitingChallenge)
    _ = try auth.begin(nonce: nonce)
    #expect(throws: FurbleProtocol.Error.self) {
      try auth.verify(proof: Data(repeating: 0, count: 16))
    }
    #expect(auth.state == .lockedOut)
  }

  @Test func passwordUsesFirmwareUtf8ByteLimit() {
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleAuthSession(password: String(repeating: "é", count: 32))
    }
    #expect(throws: Never.self) {
      try FurbleAuthSession(password: String(repeating: "é", count: 31) + "a")
    }
  }

  @Test func passwordOnboardingValidatesBytesAndConfirmationWithoutStoringSecret() {
    var form = PasswordOnboardingState()
    let empty = form.validate(password: "", confirmation: "")
    #expect(!empty)
    #expect(form.validation == .empty)
    let tooLong = form.validate(password: String(repeating: "é", count: 32), confirmation: "")
    #expect(!tooLong)
    #expect(form.validation == .tooLong)
    let mismatch = form.validate(password: "secret", confirmation: "different")
    #expect(!mismatch)
    #expect(form.validation == .mismatch)
    let valid = form.validate(
      password: String(repeating: "é", count: 31) + "a",
      confirmation: String(repeating: "é", count: 31) + "a")
    #expect(valid)
    #expect(form.validation == .none)
    #expect(!form.hasStoredPassword)
  }

  @Test func passwordOnboardingClearAndDeleteForgetOnlyPresence() {
    var form = PasswordOnboardingState()
    form.setStoredPassword(true)
    #expect(form.hasStoredPassword)
    form.didDelete()
    #expect(!form.hasStoredPassword)
    #expect(form.validation == .none)
    form.didSave()
    #expect(form.hasStoredPassword)
    form.setStoredPassword(false)
    #expect(!form.hasStoredPassword)
  }

  @Test func authPacketsHaveExplicitOperationsAndFixedLengths() throws {
    #expect(FurbleProtocol.authBegin() == Data([1, 0]))
    let challenge = Data([1, 0]) + Data(repeating: 9, count: 16)
    #expect(try FurbleProtocol.decodeAuthChallenge(challenge) == Data(repeating: 9, count: 16))
    let proof = try FurbleProtocol.encodeAuthProof(Data(repeating: 8, count: 16))
    #expect(proof.count == 18)
    #expect(try FurbleProtocol.decodeAuthResult(Data([1, 2, 1])) == 1)
    #expect(try FurbleProtocol.decodeAuthResult(Data([1, 2, 4])) == 4)
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeAuthResult(Data([1, 2, 0]))
    }
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.encodeAuthProof(Data(repeating: 8, count: 15))
    }
  }

  @Test func authDecodersRejectWrongVersionAndOperation() {
    let nonce = Data(repeating: 9, count: FurbleProtocol.authNonceSize)
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeAuthChallenge(Data([2, 0]) + nonce)
    }
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeAuthChallenge(Data([1, 1]) + nonce)
    }
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeAuthResult(Data([2, 2, 1]))
    }
    #expect(throws: FurbleProtocol.Error.self) {
      try FurbleProtocol.decodeAuthResult(Data([1, 1, 1]))
    }
  }

  @Test func authProofMatchesAndroidFirmwareGoldenVector() throws {
    let nonce = Data((0..<16).map(UInt8.init))
    let password = Data("correct horse battery staple".utf8)
    let proof = Data(HMAC<SHA256>.authenticationCode(
      for: nonce, using: SymmetricKey(data: password)).prefix(16))
    let packet = try FurbleProtocol.encodeAuthProof(proof)
    #expect(packet == Data([
      1, 1, 0xc5, 0xdf, 0xbf, 0x65, 0x5b, 0xbc, 0xd0, 0x90,
      0xec, 0xb1, 0xa5, 0xbf, 0x71, 0x68, 0xb8, 0x43
    ]))
  }

  @Test func hmacCorrectProofAuthenticatesAndCannotBeReplayed() throws {
    var auth = try FurbleAuthSession(password: "secret")
    _ = try auth.begin(nonce: Data(repeating: 7, count: 16))
    let proof = Data(HMAC<SHA256>.authenticationCode(
      for: Data(repeating: 7, count: 16),
      using: SymmetricKey(data: Data("secret".utf8))).prefix(16))
    try auth.verify(proof: proof)
    #expect(auth.state == .authenticated)
    #expect(throws: FurbleProtocol.Error.self) {
      try auth.verify(proof: proof)
    }
  }
}
