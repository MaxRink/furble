import Foundation

#if canImport(CoreBluetooth)
import CoreBluetooth
import Combine

/// Thin CoreBluetooth adapter. All policy and packet parsing stay in the
/// platform-neutral core and can therefore be tested without a radio.
@MainActor
public final class FurbleBLEClient: NSObject, ObservableObject {
  @Published public private(set) var state = CompanionStateMachine()
  @Published public private(set) var phase = CompanionConnectionPhase.idle
  @Published public private(set) var status: FurbleProtocol.Status?
  @Published public private(set) var error: CompanionFailure?

  private let credentialStore: FurbleCredentialStore
  private lazy var central = CBCentralManager(delegate: self, queue: nil,
    options: [CBCentralManagerOptionRestoreIdentifierKey: "com.furble.companion.central"])
  private var peripheral: CBPeripheral?
  private var characteristics: [CBUUID: CBCharacteristic] = [:]
  private var reconnectAttempt = 0

  public init(credentialStore: FurbleCredentialStore = KeychainCredentialStore()) {
    self.credentialStore = credentialStore
    super.init()
    _ = central
  }

  public func start() {
    guard central.state == .poweredOn else { return }
    if state.phase == .idle || state.phase.isFailure { beginScan() }
  }

  public func stop() {
    if let peripheral { central.cancelPeripheralConnection(peripheral) }
    central.stopScan()
    state = CompanionStateMachine()
    phase = .idle
  }

  public func writeLocation(_ fix: FurbleProtocol.LocationFix) {
    guard let characteristic = characteristics[CBUUID(string: FurbleProtocol.UUIDs.location)] else { return }
    guard let data = try? FurbleProtocol.encodeLocation(fix) else { return }
    write(data, to: characteristic, type: .withoutResponse)
  }

  public func writeSettings(_ data: Data) throws {
    _ = try state.privileged(.writeSettings(data))
    guard let characteristic = characteristics[CBUUID(string: FurbleProtocol.UUIDs.settings)] else {
      throw FurbleProtocol.Error.malformed
    }
    write(data, to: characteristic, type: .withResponse)
  }

  public func trigger(_ operation: FurbleProtocol.TriggerOperation, holdMilliseconds: UInt16 = 0) throws {
    let data = FurbleProtocol.encodeTrigger(operation, holdMilliseconds: holdMilliseconds)
    _ = try state.privileged(.writeTrigger(data))
    guard let characteristic = characteristics[CBUUID(string: FurbleProtocol.UUIDs.trigger)] else {
      throw FurbleProtocol.Error.malformed
    }
    write(data, to: characteristic, type: .withResponse)
  }

  private func beginScan() {
    _ = state.start(bluetoothAvailable: true)
    phase = state.phase
    central.scanForPeripherals(withServices: [CBUUID(string: FurbleProtocol.UUIDs.service)])
  }

  private func write(_ data: Data, to characteristic: CBCharacteristic, type: CBCharacteristicWriteType) {
    guard data.count <= FurbleProtocol.maxPayloadSize else { return }
    peripheral?.writeValue(data, for: characteristic, type: type)
  }

  private func fail(_ value: CompanionFailure) {
    error = value
    phase = .failed(value)
    state = CompanionStateMachine()
    if let peripheral { central.cancelPeripheralConnection(peripheral) }
  }

  private func characteristic(_ uuid: String) -> CBCharacteristic? {
    characteristics[CBUUID(string: uuid)]
  }

  private func authenticate() {
    guard let authCharacteristic = characteristic(FurbleProtocol.UUIDs.auth),
      let password = try? credentialStore.readPassword(), let password, !password.isEmpty else {
      fail(.authenticationUnavailable)
      return
    }
    // The firmware owns nonce generation. Never authenticate a locally
    // generated nonce or accept a proof without the firmware response.
    let begin = Data([FurbleProtocol.version, 0])
    write(begin, to: authCharacteristic, type: .withResponse)
  }

  private func handleAuth(_ data: Data) {
    if data.count == 3, data[0] == FurbleProtocol.version, data[1] == 2, data[2] == 0 {
      let commands = state.didAuthenticationAccepted()
      phase = state.phase
      subscribe(commands)
      return
    }
    guard data.count == FurbleProtocol.authNonceSize + 2,
      let password = try? credentialStore.readPassword(), let password, !password.isEmpty,
      let authCharacteristic = characteristic(FurbleProtocol.UUIDs.auth) else {
      fail(.authenticationUnavailable)
      return
    }
    let nonce = data.subdata(in: 2..<data.count)
    guard let command = state.beginAuthentication(password: password, nonce: nonce),
      case .writeAuthentication(let proof) = command else {
      fail(.authenticationUnavailable)
      return
    }
    write(Data([FurbleProtocol.version, 1]) + proof, to: authCharacteristic, type: .withResponse)
  }

  private func subscribe(_ commands: [CompanionCommand]) {
    guard let peripheral else { return }
    for command in commands {
      let uuid: String?
      switch command {
      case .subscribeStatus: uuid = FurbleProtocol.UUIDs.status
      case .subscribeSettings: uuid = FurbleProtocol.UUIDs.settings
      case .subscribeCameras: uuid = FurbleProtocol.UUIDs.cameras
      case .readStatus:
        if let status = characteristic(FurbleProtocol.UUIDs.status) { peripheral.readValue(for: status) }
        uuid = nil
      default: uuid = nil
      }
      if let uuid, let characteristic = characteristic(uuid) {
        peripheral.setNotifyValue(true, for: characteristic)
      }
    }
  }
}

extension FurbleBLEClient: CBCentralManagerDelegate {
  public func centralManagerDidUpdateState(_ central: CBCentralManager) {
    if central.state == .poweredOn { beginScan() }
    else if central.state != .unknown { fail(.bluetoothUnavailable) }
  }

  public func central(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                     advertisementData: [String: Any], rssi RSSI: NSNumber) {
    self.peripheral = peripheral
    central.stopScan()
    _ = state.didFindPeripheral()
    phase = state.phase
    peripheral.delegate = self
    central.connect(peripheral)
  }

  public func central(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
    _ = state.didConnect()
    phase = state.phase
    peripheral.discoverServices([CBUUID(string: FurbleProtocol.UUIDs.service)])
  }

  public func central(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
    reconnectAttempt += 1
    phase = .reconnecting(attempt: reconnectAttempt)
    beginScan()
  }

  public func central(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
    state.didDisconnect()
    phase = state.phase
    reconnectAttempt += 1
  }

  public func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
    if let restored = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
      let restoredPeripheral = restored.first {
      peripheral = restoredPeripheral
      restoredPeripheral.delegate = self
    }
  }
}

extension FurbleBLEClient: CBPeripheralDelegate {
  public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
    guard error == nil, let service = peripheral.services?.first(where: {
      $0.uuid == CBUUID(string: FurbleProtocol.UUIDs.service)
    }) else { fail(.serviceMissing); return }
    let uuids = [
      FurbleProtocol.UUIDs.location, FurbleProtocol.UUIDs.status,
      FurbleProtocol.UUIDs.settings, FurbleProtocol.UUIDs.trigger,
      FurbleProtocol.UUIDs.capability, FurbleProtocol.UUIDs.auth,
      FurbleProtocol.UUIDs.cameras
    ].map(CBUUID.init(string:))
    peripheral.discoverCharacteristics(uuids, for: service)
  }

  public func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                         error: Error?) {
    guard error == nil else { fail(.requiredCharacteristicMissing("discovery")); return }
    characteristics = Dictionary(uniqueKeysWithValues: (service.characteristics ?? []).map { ($0.uuid, $0) })
    let command = state.didDiscover(
      serviceFound: true,
      status: characteristic(FurbleProtocol.UUIDs.status) != nil,
      settings: characteristic(FurbleProtocol.UUIDs.settings) != nil,
      trigger: characteristic(FurbleProtocol.UUIDs.trigger) != nil,
      auth: characteristic(FurbleProtocol.UUIDs.auth) != nil,
      cameras: characteristic(FurbleProtocol.UUIDs.cameras) != nil
    )
    guard let command else { fail(state.lastError ?? .malformedPacket); return }
    phase = state.phase
    if case .readCapability = command,
      let capability = characteristic(FurbleProtocol.UUIDs.capability) {
      peripheral.readValue(for: capability)
    }
  }

  public func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
    guard error == nil, let data = characteristic.value else { fail(.malformedPacket); return }
    if characteristic.uuid == CBUUID(string: FurbleProtocol.UUIDs.capability) {
      guard let command = state.didReadCapability(data) else { fail(state.lastError ?? .malformedPacket); return }
      if case .beginAuthentication = command { authenticate() }
    } else if characteristic.uuid == CBUUID(string: FurbleProtocol.UUIDs.auth) {
      handleAuth(data)
    } else if characteristic.uuid == CBUUID(string: FurbleProtocol.UUIDs.status) {
      guard state.didReceiveStatus(data), let status = state.status else { fail(.malformedPacket); return }
      self.status = status
    }
  }

  public func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                         error: Error?) {
    if error != nil { fail(.malformedPacket) }
  }
}

private extension CompanionConnectionPhase {
  var isFailure: Bool {
    if case .failed = self { return true }
    return false
  }
}
#endif
