import Foundation

public enum CompanionConnectionPhase: Equatable, Sendable {
  case idle
  case scanning
  case connecting
  case discovering
  case awaitingAuthentication
  case ready
  case reconnecting(attempt: Int)
  case failed(CompanionFailure)

  public var shouldReconnectAfterDisconnect: Bool {
    switch self {
    case .idle, .failed: return false
    default: return true
    }
  }
}

public enum CompanionFailure: Error, Equatable, Sendable {
  case bluetoothUnavailable
  case serviceMissing
  case requiredCharacteristicMissing(String)
  case authenticationUnavailable
  case authenticationFailed
  case authenticationRequired
  case malformedPacket
  case payloadTooLarge
  case linkLost
  case cameraTransactionTimedOut
}

/// Serializes an explicit stop followed by a requested restart. CoreBluetooth
/// can deliver callbacks from the canceled connection after stop returns.
public struct CompanionReconnectGate: Sendable {
  public private(set) var isCancelling = false
  private var restartPending = false

  public init() {}

  public mutating func beginStop(hasPeripheral: Bool) {
    restartPending = false
    isCancelling = isCancelling || hasPeripheral
  }

  @discardableResult
  public mutating func requestStart() -> Bool {
    guard !isCancelling else {
      restartPending = true
      return false
    }
    return true
  }

  @discardableResult
  public mutating func didCancel() -> Bool {
    guard isCancelling else { return false }
    isCancelling = false
    defer { restartPending = false }
    return restartPending
  }
}

/// Keeps inbound AUTH indications tied to the begin/proof exchange that the
/// client actually sent. Packet codecs validate bytes; this gate validates
/// their order.
public struct CompanionAuthExchangeGate: Sendable {
  public enum State: Equatable, Sendable {
    case idle
    case awaitingResponse
    case awaitingProof
    case awaitingResult
    case complete
  }

  public private(set) var state: State = .idle

  public init() {}

  @discardableResult
  public mutating func beginSent() -> Bool {
    guard state == .idle else { return false }
    state = .awaitingResponse
    return true
  }

  @discardableResult
  public mutating func challengeReceived() -> Bool {
    guard state == .awaitingResponse else { return false }
    state = .awaitingProof
    return true
  }

  @discardableResult
  public mutating func proofSent() -> Bool {
    guard state == .awaitingProof else { return false }
    state = .awaitingResult
    return true
  }

  @discardableResult
  public mutating func resultReceived() -> Bool {
    guard state == .awaitingResponse || state == .awaitingResult else { return false }
    state = .complete
    return true
  }

  public mutating func reset() { state = .idle }
}

public enum CompanionCommand: Equatable, Sendable {
  case scan
  case connect
  case discover
  case beginAuthentication
  case writeAuthentication(Data)
  case subscribeStatus
  case subscribeSettings
  case subscribeCameras
  case readCapability
  case readStatus
  case writeLocation(Data)
  case writeSettings(Data)
  case writeTrigger(Data)
  case writeCamera(Data)
  case disconnect
}

public struct CompanionCameraTransaction: Equatable, Sendable {
  public enum Kind: Equatable, Sendable {
    case list
    case operation(FurbleProtocol.CameraOperation, id: UInt8)
  }

  public enum Inbound: Equatable, Sendable {
    case listRecord
    case listTerminator(status: UInt8)
    case operationAcknowledgement(status: UInt8)
    case unsolicited
    case ignored
  }

  public private(set) var kind: Kind?

  public init() {}

  @discardableResult
  public mutating func begin(_ kind: Kind) -> Bool {
    guard self.kind == nil else { return false }
    self.kind = kind
    return true
  }

  public mutating func finish() { kind = nil }
  public mutating func cancel() { kind = nil }

  public func classify(_ record: FurbleProtocol.CameraRecord) -> Inbound {
    switch kind {
    case .list:
      if record.isTerminator { return .listTerminator(status: record.status) }
      return record.isOperationAcknowledgement ? .ignored : .listRecord
    case .operation(_, let id):
      if record.isOperationAcknowledgement {
        return record.cameraID == id ? .operationAcknowledgement(status: record.status) : .ignored
      }
      return .unsolicited
    case nil:
      return record.isOperationAcknowledgement ? .ignored : .unsolicited
    }
  }
}

/// BLE callbacks are translated into this deterministic state machine. It has
/// no CoreBluetooth dependency, so reconnect and security behavior are tested
/// on the host without a radio or a UI.
public struct CompanionStateMachine: Sendable {
  public private(set) var phase: CompanionConnectionPhase = .idle
  public private(set) var capability: FurbleProtocol.Capability?
  public private(set) var status: FurbleProtocol.Status?
  public private(set) var cameras: [FurbleProtocol.CameraRecord] = []
  public private(set) var lastError: CompanionFailure?
  public private(set) var cameraListError: UInt8?
  public private(set) var requiresAuthentication = true
  public var supportsCameras: Bool { hasCameras && capability?.supportsCameras == true }
  public var cameraListPending: Bool { cameraListRecords != nil }
  private var auth: FurbleAuthSession?
  private var hasStatus = false
  private var hasSettings = false
  private var hasTrigger = false
  private var hasAuth = false
  private var hasCameras = false
  private var cameraListRecords: [UInt8: FurbleProtocol.CameraRecord]?
  private var cameraEventsDuringList: [UInt8: FurbleProtocol.CameraRecord] = [:]

  public init() {}

  public mutating func start(bluetoothAvailable: Bool) -> CompanionCommand? {
    guard bluetoothAvailable else {
      return fail(.bluetoothUnavailable)
    }
    phase = .scanning
    lastError = nil
    capability = nil
    status = nil
    cameras = []
    cameraListError = nil
    auth = nil
    requiresAuthentication = true
    hasStatus = false
    hasSettings = false
    hasTrigger = false
    hasAuth = false
    hasCameras = false
    cameraListRecords = nil
    cameraEventsDuringList.removeAll()
    return .scan
  }

  public mutating func didFindPeripheral() -> CompanionCommand {
    phase = .connecting
    return .connect
  }

  public mutating func didConnect() -> CompanionCommand {
    phase = .discovering
    return .discover
  }

  public mutating func didDiscover(
    serviceFound: Bool,
    status: Bool,
    settings: Bool,
    trigger: Bool,
    auth: Bool,
    cameras: Bool,
    capability: Bool = true
  ) -> CompanionCommand? {
    guard serviceFound else { return fail(.serviceMissing) }
    guard status else { return fail(.requiredCharacteristicMissing("status")) }
    guard settings else { return fail(.requiredCharacteristicMissing("settings")) }
    guard trigger else { return fail(.requiredCharacteristicMissing("trigger")) }
    guard capability else { return fail(.requiredCharacteristicMissing("capability")) }
    // A secure client must never silently downgrade to firmware that lacks
    // the application auth characteristic. BLE link encryption remains a
    // separate requirement enforced by CoreBluetooth and firmware.
    guard auth else { return fail(.authenticationUnavailable) }
    hasStatus = status
    hasSettings = settings
    hasTrigger = trigger
    hasAuth = auth
    hasCameras = cameras
    phase = .awaitingAuthentication
    return .readCapability
  }

  public mutating func didReadCapability(_ data: Data) -> CompanionCommand? {
    do {
      let value = try FurbleProtocol.decodeCapability(data)
      capability = value
      // Authentication is tied to the discovered Auth characteristic. The
      // capability bit layout reserves bit 1 for cameras, so it cannot also
      // be used as an authentication flag.
      requiresAuthentication = hasAuth
      guard requiresAuthentication else { return fail(.authenticationUnavailable) }
      hasSettings = hasSettings && value.supportsSettings
      hasCameras = hasCameras && value.supportsCameras
      phase = .awaitingAuthentication
      return .beginAuthentication
    } catch {
      return fail(.malformedPacket)
    }
  }

  public mutating func beginAuthentication(password: String, nonce: Data) -> CompanionCommand? {
    guard phase == .awaitingAuthentication, hasAuth, capability != nil else {
      return fail(.authenticationRequired)
    }
    do {
      auth = try FurbleAuthSession(password: password)
      guard let auth else { return fail(.authenticationUnavailable) }
      var session = auth
      let proof = try session.begin(nonce: nonce)
      self.auth = session
      return .writeAuthentication(proof)
    } catch {
      return fail(.authenticationFailed)
    }
  }

  public mutating func didAuthenticationAccepted() -> [CompanionCommand] {
    guard phase == .awaitingAuthentication, hasAuth, capability != nil else {
      _ = fail(.authenticationFailed)
      return []
    }
    guard var auth else {
      _ = fail(.authenticationFailed)
      return []
    }
    do { try auth.markAuthenticated() } catch {
      _ = fail(.authenticationFailed)
      return []
    }
    self.auth = auth
    phase = .ready
    var commands: [CompanionCommand] = [.subscribeStatus, .readStatus]
    if hasSettings { commands.append(.subscribeSettings) }
    if hasCameras { commands.append(.subscribeCameras) }
    return commands
  }

  /// Completes the AUTH exchange when firmware has no configured password.
  /// The result indication is still required. A missing local credential is
  /// not itself permission to skip the firmware handshake.
  public mutating func didAuthenticationNotRequired() -> [CompanionCommand] {
    guard phase == .awaitingAuthentication, hasAuth, capability != nil else {
      _ = fail(.authenticationFailed)
      return []
    }
    auth = nil
    phase = .ready
    var commands: [CompanionCommand] = [.subscribeStatus, .readStatus]
    if hasSettings { commands.append(.subscribeSettings) }
    if hasCameras { commands.append(.subscribeCameras) }
    return commands
  }

  public mutating func didAuthenticationRejected() -> CompanionCommand? {
    _ = fail(.authenticationFailed)
    return .disconnect
  }

  public mutating func didReceiveStatus(_ data: Data) -> Bool {
    guard phase == .ready else { return false }
    do {
      status = try FurbleProtocol.decodeStatus(data)
      return true
    } catch {
      lastError = .malformedPacket
      return false
    }
  }

  public mutating func didReceiveCamera(_ data: Data) -> Bool {
    guard phase == .ready, hasCameras else { return false }
    do {
      let camera = try FurbleProtocol.decodeCameraRecord(data)
      if camera.isTerminator {
        guard let records = cameraListRecords else { return true }
        guard camera.status == 0 else {
          cameraListRecords = nil
          cameraEventsDuringList.removeAll()
          cameraListError = camera.status
          return true
        }
        var merged = records.keys.sorted().compactMap { records[$0] }
        for event in cameraEventsDuringList.values {
          merged.removeAll { $0.cameraID == event.cameraID }
          merged.append(event)
        }
        cameras = merged.sorted { $0.cameraID < $1.cameraID }
        cameraListRecords = nil
        cameraEventsDuringList.removeAll()
        cameraListError = nil
        return true
      }
      if camera.isOperationAcknowledgement { return true }
      if cameraListRecords != nil {
        cameraListRecords?[camera.cameraID] = camera
        cameraEventsDuringList[camera.cameraID] = camera
        return true
      }
      upsertCamera(camera)
      return true
    } catch {
      lastError = .malformedPacket
      return false
    }
  }

  @discardableResult
  public mutating func beginCameraList() -> Bool {
    guard phase == .ready, supportsCameras else { return false }
    guard cameraListRecords == nil else { return false }
    cameraListRecords = [:]
    cameraEventsDuringList.removeAll()
    cameraListError = nil
    return true
  }

  public mutating func cancelCameraList() {
    cameraListRecords = nil
    cameraEventsDuringList.removeAll()
    cameraListError = nil
  }

  public mutating func didReceiveCameraEvent(_ data: Data) -> Bool {
    guard phase == .ready, hasCameras else { return false }
    do {
      let camera = try FurbleProtocol.decodeCameraRecord(data)
      guard !camera.isTerminator else { return true }
      guard !camera.isOperationAcknowledgement else { return true }
      if cameraListRecords != nil { cameraEventsDuringList[camera.cameraID] = camera }
      upsertCamera(camera)
      return true
    } catch {
      lastError = .malformedPacket
      return false
    }
  }

  private mutating func upsertCamera(_ camera: FurbleProtocol.CameraRecord) {
    cameras.removeAll { $0.cameraID == camera.cameraID }
    cameras.append(camera)
    cameras.sort { $0.cameraID < $1.cameraID }
  }

  public func privileged(_ command: CompanionCommand) throws -> CompanionCommand {
    guard phase == .ready else { throw FurbleProtocol.Error.authenticationRequired }
    switch command {
    case .writeSettings:
      guard hasSettings, capability?.supportsSettings == true else {
        throw FurbleProtocol.Error.authenticationUnavailable
      }
    case .writeTrigger:
      guard hasTrigger else { throw FurbleProtocol.Error.authenticationUnavailable }
    case .writeCamera:
      guard hasCameras, capability?.supportsCameras == true else {
        throw FurbleProtocol.Error.authenticationUnavailable
      }
    default:
      break
    }
    return command
  }

  public mutating func didDisconnect() {
    phase = .reconnecting(attempt: 1)
    lastError = .linkLost
    capability = nil
    status = nil
    cameras = []
    auth = nil
    requiresAuthentication = true
    hasStatus = false
    hasSettings = false
    hasTrigger = false
    hasAuth = false
    hasCameras = false
    cameraListRecords = nil
    cameraEventsDuringList.removeAll()
    cameraListError = nil
  }

  public mutating func retry(attempt: Int) -> CompanionCommand? {
    guard attempt > 0 else { return fail(.linkLost) }
    phase = .reconnecting(attempt: attempt)
    return .scan
  }

  private mutating func fail(_ failure: CompanionFailure) -> CompanionCommand? {
    lastError = failure
    phase = .failed(failure)
    return nil
  }
}
