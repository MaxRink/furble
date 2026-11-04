import Foundation
import Testing
@testable import FurbleCompanionCore

struct CompanionStateMachineTests {
  @Test func restartWaitsForCancellationAndResumesOnce() {
    var gate = CompanionReconnectGate()
    gate.beginStop(hasPeripheral: true)
    let started = gate.requestStart()
    #expect(!started)
    #expect(gate.isCancelling)
    let cancelled = gate.didCancel()
    #expect(cancelled)
    #expect(!gate.isCancelling)
    let cancelledAgain = gate.didCancel()
    #expect(!cancelledAgain)
  }

  @Test func explicitStopDoesNotScheduleRestart() {
    var gate = CompanionReconnectGate()
    gate.beginStop(hasPeripheral: true)
    let cancelled = gate.didCancel()
    #expect(!cancelled)
  }

  @Test func authExchangeEnforcesBeginChallengeProofResultOrder() {
    var gate = CompanionAuthExchangeGate()
    let prematureResult = gate.resultReceived()
    #expect(!prematureResult)
    let began = gate.beginSent()
    #expect(began)
    let challenged = gate.challengeReceived()
    #expect(challenged)
    let resultBeforeProof = gate.resultReceived()
    #expect(!resultBeforeProof)
    let proof = gate.proofSent()
    #expect(proof)
    let result = gate.resultReceived()
    #expect(result)
    let replay = gate.resultReceived()
    #expect(!replay)
  }

  @Test func passwordlessAuthResultStillRequiresBegin() {
    var gate = CompanionAuthExchangeGate()
    let prematureResult = gate.resultReceived()
    #expect(!prematureResult)
    let began = gate.beginSent()
    #expect(began)
    let result = gate.resultReceived()
    #expect(result)
  }

  @Test func terminalPhasesDoNotReconnectAfterDisconnect() {
    #expect(!CompanionConnectionPhase.idle.shouldReconnectAfterDisconnect)
    #expect(!CompanionConnectionPhase.failed(.authenticationFailed).shouldReconnectAfterDisconnect)
    #expect(CompanionConnectionPhase.ready.shouldReconnectAfterDisconnect)
  }

  @Test func discoveryRejectsMissingAuthInsteadOfDowngrading() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    let command = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: false, cameras: false)
    #expect(command == nil)
    #expect(machine.phase == .failed(.authenticationUnavailable))
  }

  @Test func authenticatedFlowSubscribesAndDecodesStatus() throws {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    let discoverCommand = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true)
    #expect(discoverCommand == .readCapability)
    let capability = Data([1, 2, 3, 0, 0, 0])
    let capabilityCommand = machine.didReadCapability(capability)
    #expect(capabilityCommand == .beginAuthentication)
    let authCommand = machine.beginAuthentication(
      password: "test", nonce: Data(repeating: 1, count: 16))
    #expect(authCommand?.isAuthWrite == true)
    let accepted = machine.didAuthenticationAccepted()
    #expect(accepted == [.subscribeStatus, .readStatus, .subscribeSettings, .subscribeCameras])
    #expect(machine.phase == .ready)
    #expect(machine.authenticationMode == .password)
    #expect(machine.supportsCameras)
    let status = Data([1, 85, 0x18, 0x10, 0, 0, 1, 0, 0, 0, 0, 0, 0xff, 0xff, 1, 0, 0, 0, 0, 0])
    let received = machine.didReceiveStatus(status)
    #expect(received)
    #expect(machine.status?.batteryPercent == 85)
  }

  @Test func cameraSubscriptionRequiresTheFrozenCapabilityBit() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    let discoverCommand = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true)
    #expect(discoverCommand == .readCapability)
    let capabilityCommand = machine.didReadCapability(Data([1, 2, 1, 0, 0, 0]))
    #expect(capabilityCommand == .beginAuthentication)
    _ = machine.beginAuthentication(password: "test", nonce: Data(repeating: 2, count: 16))
    let accepted = machine.didAuthenticationAccepted()
    #expect(accepted == [.subscribeStatus, .readStatus, .subscribeSettings])
    #expect(!machine.supportsCameras)
  }

  @Test func cameraListReplacesStaleCatalogAndKeepsLaterEvents() {
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
    let staleAccepted = machine.didReceiveCamera(stale)
    #expect(staleAccepted)
    #expect(machine.cameras.map(\.cameraID) == [1])
    let acknowledgement = Data([0, 1, 0, 0, 0, 0x80, 0, 0])
    let acknowledgementAccepted = machine.didReceiveCameraEvent(acknowledgement)
    #expect(acknowledgementAccepted)
    #expect(machine.cameras.map(\.cameraID) == [1])

    let listStarted = machine.beginCameraList()
    #expect(listStarted)
    #expect(machine.cameraListPending)
    let secondList = machine.beginCameraList()
    #expect(!secondList)
    let interleaved = Data([0, 9, 1, 1, 100, 0xd0, 2, 1, 0x49])
    let interleavedAccepted = machine.didReceiveCameraEvent(interleaved)
    #expect(interleavedAccepted)
    let listed = Data([0, 2, 1, 1, 100, 0xf0, 2, 1, 0x42])
    let terminator = Data([0, 0xff, 0, 0, 0, 0, 0, 0])
    let listedAccepted = machine.didReceiveCamera(listed)
    #expect(listedAccepted)
    let terminatorAccepted = machine.didReceiveCamera(terminator)
    #expect(terminatorAccepted)
    #expect(!machine.cameraListPending)
    #expect(machine.cameras.map(\.cameraID) == [2, 9])

    let event = Data([0, 2, 1, 9, 100, 0xe0, 2, 1, 0x42])
    let eventAccepted = machine.didReceiveCamera(event)
    #expect(eventAccepted)
    #expect(machine.cameras.first?.rssi == -32)

    let retryList = machine.beginCameraList()
    #expect(retryList)
    let previousCatalog = machine.cameras
    let failedTerminator = Data([2, 0xff, 0, 0, 0, 0x80, 0, 0])
    let failedAccepted = machine.didReceiveCamera(failedTerminator)
    #expect(failedAccepted)
    #expect(!machine.cameraListPending)
    #expect(machine.cameraListError == 2)
    #expect(machine.cameras == previousCatalog)

    let finalList = machine.beginCameraList()
    #expect(finalList)
    machine.didDisconnect()
    #expect(!machine.cameraListPending)
    #expect(machine.cameraListError == nil)
  }

  @Test func cameraTransactionSerializesAndClassifiesResponsesBeforeCatalog() throws {
    var transaction = CompanionCameraTransaction()
    let listStarted = transaction.begin(.list)
    #expect(listStarted)
    let secondBegin = transaction.begin(.operation(.connect, id: 7))
    #expect(!secondBegin)

    let specificAcknowledgement = try FurbleProtocol.decodeCameraRecord(
      Data([0, 7, 0, 0, 0, 0x80, 0, 0]))
    #expect(transaction.classify(specificAcknowledgement) == .ignored)
    let failedTerminator = try FurbleProtocol.decodeCameraRecord(
      Data([2, 0xff, 0, 0, 0, 0x80, 0, 0]))
    #expect(transaction.classify(failedTerminator) == .listTerminator(status: 2))

    transaction.finish()
    let operationStarted = transaction.begin(.operation(.connect, id: 7))
    #expect(operationStarted)
    #expect(transaction.classify(specificAcknowledgement) == .operationAcknowledgement(status: 0))
    let liveRecord = try FurbleProtocol.decodeCameraRecord(
      Data([0, 7, 1, 8, 100, 0xe0, 2, 1, 0x58]))
    #expect(transaction.classify(liveRecord) == .unsolicited)
    transaction.cancel()
    #expect(transaction.kind == nil)
    let restarted = transaction.begin(.list)
    #expect(restarted)
  }

  @Test func passwordlessResultCompletesOnlyAfterFirmwareNotRequired() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    _ = machine.didFindPeripheral()
    _ = machine.didConnect()
    _ = machine.didDiscover(serviceFound: true, status: true, settings: true,
      trigger: true, auth: true, cameras: true)
    _ = machine.didReadCapability(Data([1, 2, 3, 0, 0, 0]))
    let accepted = machine.didAuthenticationNotRequired()
    #expect(accepted == [.subscribeStatus, .readStatus, .subscribeSettings, .subscribeCameras])
    #expect(machine.phase == .ready)
    #expect(machine.authenticationMode == .notRequired)
  }

  @Test func privilegedCommandsRequireTheirAdvertisedCapability() throws {
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
    let accepted = machine.didAuthenticationAccepted()
    #expect(accepted == [.subscribeStatus, .readStatus])
    #expect(throws: FurbleProtocol.Error.self) {
      try machine.privileged(.writeSettings(Data([0])))
    }
    #expect(throws: FurbleProtocol.Error.self) {
      try machine.privileged(.writeCamera(Data([0, 0xff])))
    }
    #expect(throws: Never.self) {
      try machine.privileged(.writeTrigger(Data([1, 1])))
    }
  }

  @Test func disconnectClearsSensitiveStateAndReconnects() {
    var machine = CompanionStateMachine()
    _ = machine.start(bluetoothAvailable: true)
    machine.didDisconnect()
    #expect(machine.phase == .reconnecting(attempt: 1))
    #expect(machine.status == nil)
    #expect(machine.authenticationMode == nil)
    let retry = machine.retry(attempt: 2)
    #expect(retry == .scan)
  }

  @Test func reconnectRequiresFreshCapabilityAndAuthentication() {
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
    #expect(machine.phase == .reconnecting(attempt: 1))
    let accepted = machine.didAuthenticationAccepted()
    #expect(accepted == [])
    #expect(machine.phase == .failed(.authenticationFailed))
  }
}

private extension CompanionCommand {
  var isAuthWrite: Bool {
    if case .writeAuthentication = self { return true }
    return false
  }
}
