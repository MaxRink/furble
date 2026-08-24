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
}

public enum CompanionFailure: Error, Equatable, Sendable {
  case bluetoothUnavailable
  case serviceMissing
  case requiredCharacteristicMissing(String)
  case authenticationUnavailable
  case authenticationFailed
  case malformedPacket
  case payloadTooLarge
  case linkLost
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

/// BLE callbacks are translated into this deterministic state machine. It has
/// no CoreBluetooth dependency, so reconnect and security behavior are tested
/// on the host without a radio or a UI.
public struct CompanionStateMachine: Sendable {
  public private(set) var phase: CompanionConnectionPhase = .idle
  public private(set) var capability: FurbleProtocol.Capability?
  public private(set) var status: FurbleProtocol.Status?
  public private(set) var cameras: [FurbleProtocol.CameraRecord] = []
  public private(set) var lastError: CompanionFailure?
  public private(set) var requiresAuthentication = true
  private var auth: FurbleAuthSession?
  private var hasStatus = false
  private var hasSettings = false
  private var hasTrigger = false
  private var hasAuth = false
  private var hasCameras = false

  public init() {}

  public mutating func start(bluetoothAvailable: Bool) -> CompanionCommand? {
    guard bluetoothAvailable else {
      return fail(.bluetoothUnavailable)
    }
    phase = .scanning
    lastError = nil
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
      hasCameras = hasCameras && value.supportsCameras
      phase = .awaitingAuthentication
      return .beginAuthentication
    } catch {
      return fail(.malformedPacket)
    }
  }

  public mutating func beginAuthentication(password: String, nonce: Data) -> CompanionCommand? {
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
      if camera.isTerminator { return true }
      cameras.removeAll { $0.cameraID == camera.cameraID }
      cameras.append(camera)
      return true
    } catch {
      lastError = .malformedPacket
      return false
    }
  }

  public func privileged(_ command: CompanionCommand) throws -> CompanionCommand {
    guard phase == .ready else { throw FurbleProtocol.Error.authenticationRequired }
    guard hasSettings || hasTrigger || hasCameras else {
      throw FurbleProtocol.Error.authenticationUnavailable
    }
    return command
  }

  public mutating func didDisconnect() {
    phase = .reconnecting(attempt: 1)
    lastError = .linkLost
    status = nil
    cameras = []
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
