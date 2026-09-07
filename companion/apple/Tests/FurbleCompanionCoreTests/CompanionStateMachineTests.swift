import XCTest
@testable import FurbleCompanionCore

final class CompanionStateMachineTests: XCTestCase {
  func testRestartWaitsForCancellationAndResumesOnce() {
    var gate = CompanionReconnectGate()
    gate.beginStop(hasPeripheral: true)
    XCTAssertFalse(gate.requestStart())
    XCTAssertTrue(gate.isCancelling)
    XCTAssertTrue(gate.didCancel())
    XCTAssertFalse(gate.isCancelling)
    XCTAssertFalse(gate.didCancel())
  }

  func testExplicitStopDoesNotScheduleRestart() {
    var gate = CompanionReconnectGate()
    gate.beginStop(hasPeripheral: true)
    XCTAssertFalse(gate.didCancel())
  }

  func testAuthExchangeEnforcesBeginChallengeProofResultOrder() {
    var gate = CompanionAuthExchangeGate()
    XCTAssertFalse(gate.resultReceived())
    XCTAssertTrue(gate.beginSent())
    XCTAssertTrue(gate.challengeReceived())
    XCTAssertFalse(gate.resultReceived())
    XCTAssertTrue(gate.proofSent())
    XCTAssertTrue(gate.resultReceived())
    XCTAssertFalse(gate.resultReceived())
  }

  func testPasswordlessAuthResultStillRequiresBegin() {
    var gate = CompanionAuthExchangeGate()
    XCTAssertFalse(gate.resultReceived())
    XCTAssertTrue(gate.beginSent())
    XCTAssertTrue(gate.resultReceived())
  }

  func testTerminalPhasesDoNotReconnectAfterDisconnect() {
    XCTAssertFalse(CompanionConnectionPhase.idle.shouldReconnectAfterDisconnect)
    XCTAssertFalse(CompanionConnectionPhase.failed(.authenticationFailed).shouldReconnectAfterDisconnect)
    XCTAssertTrue(CompanionConnectionPhase.ready.shouldReconnectAfterDisconnect)
  }

  func testDiscoveryRejectsMissingAuthInsteadOfDowngrading() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    XCTAssertNil(machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: false, cameras: false))
    XCTAssertEqual(machine.phase, .failed(.authenticationUnavailable))
  }

  func testAuthenticatedFlowSubscribesAndDecodesStatus() throws {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    XCTAssertEqual(machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true), .readCapability)
    let capability = Data([1, 2, 3, 0, 0, 0])
    XCTAssertEqual(machine.didReadCapability(capability), .beginAuthentication)
    XCTAssertEqual(machine.beginAuthentication(password: "test", nonce: Data(repeating: 1, count: 16))?.isAuthWrite, true)
    XCTAssertEqual(machine.didAuthenticationAccepted(), [.subscribeStatus, .readStatus, .subscribeSettings, .subscribeCameras])
    XCTAssertEqual(machine.phase, .ready)
    XCTAssertTrue(machine.supportsCameras)
    let status = Data([1, 85, 0x18, 0x10, 0, 0, 1, 0, 0, 0, 0, 0, 0xff, 0xff, 1, 0, 0, 0, 0, 0])
    XCTAssertTrue(machine.didReceiveStatus(status))
    XCTAssertEqual(machine.status?.batteryPercent, 85)
  }

  func testCameraSubscriptionRequiresTheFrozenCapabilityBit() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    XCTAssertEqual(machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true), .readCapability)
    XCTAssertEqual(machine.didReadCapability(Data([1, 2, 1, 0, 0, 0])), .beginAuthentication)
    _ = machine.beginAuthentication(password: "test", nonce: Data(repeating: 2, count: 16))
    XCTAssertEqual(machine.didAuthenticationAccepted(), [.subscribeStatus, .readStatus, .subscribeSettings])
    XCTAssertFalse(machine.supportsCameras)
  }

  func testCameraListReplacesStaleCatalogAndKeepsLaterEvents() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    _ = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true)
    _ = machine.didReadCapability(Data([1, 2, 3, 0, 0, 0]))
    _ = machine.beginAuthentication(password: "test", nonce: Data(repeating: 1, count: 16))
    _ = machine.didAuthenticationAccepted()

    let stale = Data([0, 1, 1, 1, 0, 0, 0, 1, 0x41])
    XCTAssertTrue(machine.didReceiveCamera(stale))
    XCTAssertEqual(machine.cameras.map(\.cameraID), [1])

    machine.beginCameraList()
    let listed = Data([0, 2, 1, 1, 100, 0xf0, 2, 1, 0x42])
    let terminator = Data([0, 0xff, 0, 0, 0, 0, 0, 0])
    XCTAssertTrue(machine.didReceiveCamera(listed))
    XCTAssertTrue(machine.didReceiveCamera(terminator))
    XCTAssertEqual(machine.cameras.map(\.cameraID), [2])

    let event = Data([0, 2, 1, 9, 100, 0xe0, 2, 1, 0x42])
    XCTAssertTrue(machine.didReceiveCamera(event))
    XCTAssertEqual(machine.cameras.first?.rssi, -32)
  }

  func testPasswordlessResultCompletesOnlyAfterFirmwareNotRequired() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    _ = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true)
    _ = machine.didReadCapability(Data([1, 2, 3, 0, 0, 0]))
    XCTAssertEqual(machine.didAuthenticationNotRequired(),
      [.subscribeStatus, .readStatus, .subscribeSettings, .subscribeCameras])
    XCTAssertEqual(machine.phase, .ready)
  }

  func testPrivilegedCommandsRequireTheirAdvertisedCapability() throws {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    _ = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true)
    // No settings or cameras feature bits: only the discovered trigger
    // characteristic is available for a privileged command.
    _ = machine.didReadCapability(Data([1, 2, 0, 0, 0, 0]))
    _ = machine.beginAuthentication(password: "test", nonce: Data(repeating: 1, count: 16))
    XCTAssertEqual(machine.didAuthenticationAccepted(), [.subscribeStatus, .readStatus])
    XCTAssertThrowsError(try machine.privileged(.writeSettings(Data([0]))) )
    XCTAssertThrowsError(try machine.privileged(.writeCamera(Data([0, 0xff]))) )
    XCTAssertNoThrow(try machine.privileged(.writeTrigger(Data([1, 1]))) )
  }

  func testDisconnectClearsSensitiveStateAndReconnects() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    machine.didDisconnect()
    XCTAssertEqual(machine.phase, .reconnecting(attempt: 1))
    XCTAssertNil(machine.status)
    XCTAssertEqual(machine.retry(attempt: 2), .scan)
  }

  func testReconnectRequiresFreshCapabilityAndAuthentication() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    _ = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: false)
    _ = machine.didReadCapability(Data([1, 2, 1, 0, 0, 0]))
    _ = machine.beginAuthentication(password: "test", nonce: Data(repeating: 3, count: 16))
    _ = machine.didAuthenticationAccepted()
    machine.didDisconnect()
    XCTAssertEqual(machine.phase, .reconnecting(attempt: 1))
    XCTAssertEqual(machine.didAuthenticationAccepted(), [])
    XCTAssertEqual(machine.phase, .failed(.authenticationFailed))
  }
}

private extension CompanionCommand {
  var isAuthWrite: Bool {
    if case .writeAuthentication = self { return true }
    return false
  }
}
