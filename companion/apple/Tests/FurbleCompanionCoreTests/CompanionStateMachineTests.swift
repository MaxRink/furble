import XCTest
@testable import FurbleCompanionCore

final class CompanionStateMachineTests: XCTestCase {
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
  }

  func testDisconnectClearsSensitiveStateAndReconnects() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    machine.didDisconnect()
    XCTAssertEqual(machine.phase, .reconnecting(attempt: 1))
    XCTAssertNil(machine.status)
    XCTAssertEqual(machine.retry(attempt: 2), .scan)
  }
}

private extension CompanionCommand {
  var isAuthWrite: Bool {
    if case .writeAuthentication = self { return true }
    return false
  }
}
