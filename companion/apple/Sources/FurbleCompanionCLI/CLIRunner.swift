import CoreBluetooth
import Darwin
import Dispatch
import Foundation
import FurbleCompanionCLIKit
import FurbleCompanionCore

@MainActor
enum CLIEntry {
  static func run(arguments: [String], environment: [String: String]) -> Int32 {
    do {
      let invocation = try CLIArgumentParser.parse(arguments)
      switch invocation.command {
      case .help:
        print(CLIArgumentParser.usage)
        return CLIExitCode.success.rawValue
      case .scan, .status, .cameras, .location, .trigger, .monitor, .password:
        if invocation.connection.transport == .usb {
          return USBConsoleRunner.run(
            invocation.command, connection: invocation.connection)
        }
        switch invocation.command {
        case .password(let command):
          return runPassword(command, connection: invocation.connection)
        case .scan:
          return runScan(connection: invocation.connection)
        case .status, .cameras, .location, .trigger, .monitor:
          return runConnected(
            invocation.command,
            connection: invocation.connection,
            environment: environment)
        case .help:
          print(CLIArgumentParser.usage)
          return CLIExitCode.success.rawValue
        }
      }
    } catch let error as CLIUsageError {
      return emit(error.description, code: .usage)
    } catch let error as RunError {
      return emit(error.message, code: error.code)
    } catch {
      return emit("unexpected failure: \(error)", code: .failure)
    }
  }

  // MARK: - Password

  private static func runPassword(
    _ command: CLIPasswordCommand, connection: CLIConnectionOptions
  ) -> Int32 {
    let store = KeychainCredentialStore(
      service: connection.keychainService,
      account: connection.keychainAccount)
    switch command {
    case .status:
      do {
        let password = try store.readPassword()
        print(password?.isEmpty == false ? "set" : "unset")
        return CLIExitCode.success.rawValue
      } catch {
        return emit("Keychain read failed: \(error)", code: .failure)
      }
    case .delete:
      do {
        try store.deletePassword()
        print(
          "Companion password removed from the login Keychain "
            + "(service=\(connection.keychainService), account=\(connection.keychainAccount)).")
        return CLIExitCode.success.rawValue
      } catch {
        return emit("Keychain delete failed: \(error)", code: .failure)
      }
    case .set:
      guard let password = readSecret("Companion password: ") else {
        return emit("no companion password was entered", code: .usage)
      }
      guard let confirmation = readSecret("Confirm companion password: ") else {
        return emit("no confirmation was entered", code: .usage)
      }
      guard password == confirmation else {
        return emit("the passwords do not match", code: .usage)
      }
      guard FurblePasswordPolicy.isValid(password) else {
        return emit(
          "companion password must be 1...\(FurblePasswordPolicy.maximumUTF8Bytes) UTF-8 bytes",
          code: .usage)
      }
      do {
        try store.savePassword(password)
        print(
          "Companion password stored in the login Keychain "
            + "(service=\(connection.keychainService), account=\(connection.keychainAccount)).")
        return CLIExitCode.success.rawValue
      } catch {
        return emit("Keychain save failed: \(error)", code: .failure)
      }
    }
  }

  private static func readSecret(_ prompt: String) -> String? {
    if isatty(STDIN_FILENO) == 0 {
      return readLine(strippingNewline: true)
    }
    guard let buffer = getpass(prompt) else { return nil }
    return String(cString: buffer)
  }

  // MARK: - Scan

  private static func runScan(connection: CLIConnectionOptions) -> Int32 {
    let result = scanDevices(connection: connection)
    if let failure = result.failure {
      return emitScanFailure(failure)
    }
    let devices = result.devices.filter(connection.device.matches)
    guard !devices.isEmpty else {
      return emit(
        "no furble companion device matched. Open Companion mode on the device; it advertises for 60 seconds.",
        code: .deviceNotFound)
    }
    print(CLIReport.scanHeader(count: devices.count))
    for device in devices {
      print(CLIReport.deviceLine(device))
    }
    return CLIExitCode.success.rawValue
  }

  private static func scanDevices(
    connection: CLIConnectionOptions
  ) -> (devices: [FurbleDiscoveredDevice], failure: CompanionFailure?) {
    note("Scanning for \(formatSeconds(connection.scanTimeout)) s...")
    let client = FurbleBLEClient(
      credentialStore: EmptyCredentialStore(),
      options: FurbleBLEClientOptions(
        deviceSelection: .any,
        connectsAutomatically: false,
        reconnectsAutomatically: false))
    client.start()
    let deadline = Date().addingTimeInterval(connection.scanTimeout)
    _ = pump(until: deadline) {
      client.phase.isFailure
    }
    let failure: CompanionFailure?
    if case .failed(let value) = client.phase { failure = value } else { failure = nil }
    let neverStarted = failure == nil && client.phase == .idle
    let devices = client.discoveredDevices
    client.stop()
    if let failure { return ([], failure) }
    if neverStarted { return ([], .bluetoothUnavailable) }
    return (devices, nil)
  }

  private static func emitScanFailure(_ failure: CompanionFailure) -> Int32 {
    switch failure {
    case .bluetoothUnavailable:
      return emit(bluetoothUnavailableMessage(), code: .bluetoothUnavailable)
    default:
      return emit("scan failed: \(failure)", code: .failure)
    }
  }

  // MARK: - Connected commands

  private static func runConnected(
    _ command: CLICommand,
    connection: CLIConnectionOptions,
    environment: [String: String]
  ) -> Int32 {
    do {
      let password = try passwordSetup(connection, environment: environment)
      let device = try resolveDevice(connection)
      note("Connecting to \(device.displayName) (\(device.id.uuidString))...")
      let client = FurbleBLEClient(
        credentialStore: password.store,
        options: FurbleBLEClientOptions(
          deviceSelection: .identifier(device.id),
          connectsAutomatically: true,
          reconnectsAutomatically: false))
      defer {
        try? client.releaseAllTriggers()
        client.stop()
      }
      client.start()
      try waitUntilReady(
        client, device: device, passwordOrigin: password.origin,
        timeout: connection.connectionTimeout)
      note("Authenticated: \(CLIReport.authenticationLabel(client.state.authenticationMode))")

      switch command {
      case .status, .cameras:
        return try printStatus(client, device: device, passwordOrigin: password.origin)
      case .location(let options):
        return try sendLocation(options, client: client)
      case .trigger(let options):
        return try sendTrigger(options, client: client)
      case .monitor(let options):
        return try monitor(options, client: client)
      case .help, .scan, .password:
        return CLIExitCode.success.rawValue
      }
    } catch let error as CLIUsageError {
      return emit(error.description, code: .usage)
    } catch let error as RunError {
      return emit(error.message, code: error.code)
    } catch {
      return emit("unexpected failure: \(error)", code: .failure)
    }
  }

  private static func resolveDevice(
    _ connection: CLIConnectionOptions
  ) throws -> FurbleDiscoveredDevice {
    let result = scanDevices(connection: connection)
    if let failure = result.failure {
      switch failure {
      case .bluetoothUnavailable:
        throw RunError(message: bluetoothUnavailableMessage(), code: .bluetoothUnavailable)
      default:
        throw RunError(message: "scan failed: \(failure)", code: .failure)
      }
    }
    let devices = result.devices.filter(connection.device.matches)
    guard !devices.isEmpty else {
      throw RunError(
        message: "no furble companion device matched. Open Companion mode on the device; "
          + "it advertises for 60 seconds.",
        code: .deviceNotFound)
    }
    guard devices.count == 1 else {
      let list = ([CLIReport.scanHeader(count: devices.count)]
        + devices.map(CLIReport.deviceLine)).joined(separator: "\n")
      throw RunError(
        message: "multiple furble companion devices matched; pass --device NAME or --device UUID:\n\(list)",
        code: .deviceNotFound)
    }
    return devices[0]
  }

  private static func waitUntilReady(
    _ client: FurbleBLEClient,
    device: FurbleDiscoveredDevice,
    passwordOrigin: CLIPasswordOrigin,
    timeout: TimeInterval
  ) throws {
    let deadline = Date().addingTimeInterval(timeout)
    _ = pump(until: deadline) {
      client.phase.isFailure || client.phase == .ready
    }
    if case .failed(let failure) = client.phase {
      throw failureError(failure, passwordOrigin: passwordOrigin)
    }
    guard client.phase == .ready else {
      throw RunError(
        message: "timed out after \(formatSeconds(timeout)) s while connecting to "
          + "\(device.displayName); last phase: \(phaseLabel(client.phase))",
        code: .timeout)
    }
  }

  private static func failureError(
    _ failure: CompanionFailure, passwordOrigin: CLIPasswordOrigin
  ) -> RunError {
    switch failure {
    case .bluetoothUnavailable:
      return RunError(message: bluetoothUnavailableMessage(), code: .bluetoothUnavailable)
    case .authenticationUnavailable:
      return RunError(
        message: "authentication is unavailable. The device does not expose the companion "
          + "Auth characteristic or rejected the secure link.",
        code: .authenticationFailed)
    case .authenticationRequired:
      if passwordOrigin == .none {
        return RunError(
          message: "the device requires a companion password, but no password was supplied. "
            + "Set FURBLE_COMPANION_PASSWORD, use --password-env/--password-file/--password-stdin, "
            + "or run 'furble-companion-cli password set'. A device whose password is unset "
            + "does not need one.",
          code: .authenticationFailed)
      }
      return RunError(
        message: "authentication failed. Check the companion password; the firmware may drop "
          + "the link after repeated failures.",
        code: .authenticationFailed)
    case .authenticationFailed:
      return RunError(
        message: "authentication failed. Check the companion password; the firmware may drop "
          + "the link after repeated failures.",
        code: .authenticationFailed)
    case .linkSecurityFailed:
      return RunError(
        message: "the BLE link could not be encrypted. Confirm the pairing prompt on both "
          + "the Mac and the device, then retry. If no prompt appears, run 'companion start' "
          + "again on the device and retry within the 60-second pairing window.",
        code: .authenticationFailed)
    case .linkLost, .cameraTransactionTimedOut:
      return RunError(
        message: "the BLE link was lost or the camera transaction timed out (\(failure)).",
        code: .timeout)
    case .serviceMissing:
      return RunError(
        message: "the firmware companion service is missing. Enable Companion mode.",
        code: .failure)
    case .requiredCharacteristicMissing(let name):
      return RunError(
        message: "the firmware companion service is missing the \(name) characteristic.",
        code: .failure)
    case .malformedPacket:
      return RunError(message: "the firmware sent a malformed companion packet.", code: .failure)
    case .payloadTooLarge:
      return RunError(message: "a companion packet exceeded the BLE payload limit.", code: .failure)
    }
  }

  private static func printStatus(
    _ client: FurbleBLEClient,
    device: FurbleDiscoveredDevice,
    passwordOrigin: CLIPasswordOrigin
  ) throws -> Int32 {
    let status = try waitForStatus(client)
    guard let capability = client.state.capability else {
      throw RunError(message: "the device did not report companion capabilities", code: .failure)
    }
    // The BLE client resets its state machine when a camera transaction fails.
    // Snapshot the authenticated report fields first, or a timeout would print
    // "not authenticated" and "cameras unsupported" alongside the timeout.
    let authentication = client.state.authenticationMode
    let cameraListSupported = client.state.supportsCameras
    let cameras = loadCameraCatalog(client)
    let report = CLIStatusReport(
      device: device,
      status: status,
      capability: capability,
      authentication: authentication,
      passwordOrigin: passwordOrigin,
      cameras: client.cameras,
      cameraListSupported: cameraListSupported,
      cameraListError: client.state.cameraListError,
      cameraOperationError: cameras.operationError)
    print(CLIReport.status(report))
    if let failure = cameras.failure {
      throw failureError(failure, passwordOrigin: passwordOrigin)
    }
    if cameras.timedOut {
      throw RunError(message: "timed out while waiting for the camera catalog", code: .timeout)
    }
    return CLIExitCode.success.rawValue
  }

  private static func waitForStatus(
    _ client: FurbleBLEClient
  ) throws -> FurbleProtocol.Status {
    if let status = client.status { return status }
    let deadline = Date().addingTimeInterval(3)
    _ = pump(until: deadline) {
      client.status != nil || client.phase.isFailure
    }
    if let status = client.status { return status }
    if case .failed(let failure) = client.phase {
      throw failureError(failure, passwordOrigin: .none)
    }
    throw RunError(
      message: "timed out waiting for the firmware status notification", code: .timeout)
  }

  private struct CameraLoad {
    let timedOut: Bool
    let operationError: String?
    let failure: CompanionFailure?
  }

  private static func loadCameraCatalog(_ client: FurbleBLEClient) -> CameraLoad {
    guard client.state.supportsCameras else {
      return CameraLoad(timedOut: false, operationError: nil, failure: nil)
    }
    do {
      try client.requestCameras()
    } catch {
      return CameraLoad(
        timedOut: false, operationError: cameraErrorDescription(error), failure: nil)
    }
    let deadline = Date().addingTimeInterval(7)
    _ = pump(until: deadline) {
      !client.state.cameraListPending || client.phase.isFailure
    }
    let timedOut = CLICameraCatalogPolicy.timedOut(
      pending: client.state.cameraListPending, phase: client.phase)
    var failure: CompanionFailure?
    if case .failed(let phaseFailure) = client.phase {
      failure = phaseFailure
    }
    return CameraLoad(
      timedOut: timedOut, operationError: client.cameraOperationError, failure: failure)
  }

  private static func cameraErrorDescription(_ error: Error) -> String {
    guard let error = error as? FurbleProtocol.Error else {
      return error.localizedDescription
    }
    switch error {
    case .authenticationRequired: return "authenticate before requesting cameras"
    case .authenticationUnavailable: return "camera controls are unavailable on this firmware"
    case .payloadTooLarge: return "camera request exceeded the BLE payload limit"
    case .malformed: return "camera request could not be encoded"
    case .unsupportedVersion(let version): return "unsupported camera protocol version \(version)"
    case .invalidValue(let message): return "invalid camera request: \(message)"
    case .authenticationFailed: return "authentication failed"
    case .cameraListInProgress: return "a camera list request is already in progress"
    case .cameraOperationInProgress: return "another camera request is already in progress"
    }
  }

  private static func sendLocation(
    _ options: CLILocationOptions, client: FurbleBLEClient
  ) throws -> Int32 {
    let fix = try options.makeFix()
    try client.sendLocation(fix)
    let accuracy = fix.accuracyMeters.map { "\($0) m" } ?? "unknown"
    let altitude = fix.altitudeValid ? "\(fix.altitude) m" : "none"
    print(
      "Sent location fix: latitude=\(fix.latitude) longitude=\(fix.longitude) "
        + "altitude=\(altitude) accuracy=\(accuracy) age_ms=\(fix.ageMilliseconds) "
        + "satellites=\(fix.satellites)")
    wait(seconds: 1)
    if let status = client.status {
      print(
        "GPS after fix: \(CLIReport.gpsSourceLabel(status.gpsSource)), "
          + "\(status.gpsSatellites) satellites")
    } else {
      note("No status notification yet; location writes have no firmware acknowledgement.")
    }
    return CLIExitCode.success.rawValue
  }

  private static func sendTrigger(
    _ options: CLITriggerOptions, client: FurbleBLEClient
  ) throws -> Int32 {
    let target = CLIReport.triggerTargetLabel(focus: options.focus)
    if options.focus {
      try client.pressFocus()
    } else {
      try client.pressShutter()
    }
    print("Pressed \(target).")
    wait(seconds: Double(options.holdMilliseconds) / 1000)
    if options.focus {
      try client.releaseFocus()
    } else {
      try client.releaseShutter()
    }
    print("Released \(target) after \(options.holdMilliseconds) ms.")
    return CLIExitCode.success.rawValue
  }

  /// Read-only status polling for battery observations. No settings, camera,
  /// location or trigger writes are issued after authentication. The monitor
  /// owns reconnect backoff so a powered-off device stops after a bounded
  /// window instead of scanning forever.
  private static func monitor(
    _ options: CLIMonitorOptions, client: FurbleBLEClient
  ) throws -> Int32 {
    let interrupt = InterruptFlag()
    signal(SIGINT, SIG_IGN)
    let signalSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    signalSource.setEventHandler { interrupt.set() }
    signalSource.resume()
    defer {
      signalSource.cancel()
      signal(SIGINT, SIG_DFL)
    }

    _ = try waitForStatus(client)
    if options.csv, options.includeHeader {
      stdoutLine(CLIReport.monitorCSVHeader)
    }

    let start = Date()
    var samples = 0
    var first: FurbleProtocol.Status?
    var last: FurbleProtocol.Status?
    var nextSample = start
    var lastStatusUptime: UInt32?
    var lastStatusAt = start
    var lost = false
    var warnedCurrent = false
    var retry = CLIMonitorBackoff()
    var nextAttemptAt: Date?
    var attemptStartedAt: Date?
    let attemptTimeout: TimeInterval = 10

    note(
      "Monitoring every \(formatSeconds(options.interval)) s"
        + (options.duration.map { " for \(formatSeconds($0)) s" } ?? " until Ctrl-C")
        + (options.untilEmpty ? " or battery <= 1%" : "")
        + " (retry window \(formatSeconds(options.retryWindow)) s).")

    func elapsed() -> TimeInterval { Date().timeIntervalSince(start) }

    func emitMarker(_ event: String) {
      let now = Date()
      if options.csv {
        stdoutLine(CLIReport.monitorMarkerRow(
          timestamp: now, elapsed: now.timeIntervalSince(start), event: event))
      } else {
        stdoutLine(
          "\(CLIReport.iso8601Timestamp(now)) elapsed="
            + "\(String(format: "%.1f", now.timeIntervalSince(start)))s event=\(event)")
      }
    }

    func scheduleRetry(_ now: Date) {
      let delay = retry.nextDelay()
      nextAttemptAt = now.addingTimeInterval(delay)
      note("Reconnect attempt \(retry.attempt) in \(formatSeconds(delay)) s.")
    }

    func startAttempt(_ now: Date) {
      emitMarker("reconnect-attempt \(retry.attempt)")
      nextAttemptAt = nil
      attemptStartedAt = now
      client.start()
    }

    func exhausted(_ now: Date) -> Bool {
      CLIMonitorPolicy.retryExhausted(
        lastActivity: lastStatusAt, now: now, retryWindow: options.retryWindow)
    }

    monitorLoop: while true {
      if interrupt.isSet { break }
      if CLIMonitorPolicy.reachedDuration(
        elapsed: elapsed(), duration: options.duration) { break }
      let now = Date()

      switch client.phase {
      case .ready:
        if lost {
          emitMarker("reconnected")
          lost = false
          retry.reset()
          nextAttemptAt = nil
          attemptStartedAt = nil
          lastStatusAt = now
          lastStatusUptime = nil
          nextSample = now
        }
        if let status = client.status {
          if status.uptimeSeconds != lastStatusUptime {
            lastStatusUptime = status.uptimeSeconds
            lastStatusAt = Date()
          }
          if Date() >= nextSample {
            client.readRSSI()
            let sampleTime = Date()
            let sampleElapsed = sampleTime.timeIntervalSince(start)
            if options.csv {
              stdoutLine(CLIReport.monitorCSVRow(
                timestamp: sampleTime,
                elapsed: sampleElapsed,
                status: status,
                rssi: client.rssi ?? client.targetDevice?.rssi,
                label: options.label))
            } else {
              stdoutLine(CLIReport.monitorHumanRow(
                timestamp: sampleTime,
                elapsed: sampleElapsed,
                status: status,
                rssi: client.rssi ?? client.targetDevice?.rssi,
                label: options.label))
            }
            if !warnedCurrent, status.batteryMilliamps == 0 {
              note("battery_ma is 0; this firmware may have no current backend. "
                + "Use battery_percent and battery_mv slope.")
              warnedCurrent = true
            }
            samples += 1
            if first == nil { first = status }
            last = status
            nextSample = sampleTime.addingTimeInterval(options.interval)
            if CLIMonitorPolicy.reachedEmpty(
              batteryPercent: status.batteryPercent, untilEmpty: options.untilEmpty) {
              emitMarker("battery-empty")
              break monitorLoop
            }
          }
        }
        if CLIMonitorPolicy.retryExhausted(
          lastActivity: lastStatusAt, now: Date(), retryWindow: options.retryWindow) {
          emitMarker("status-timeout")
          break monitorLoop
        }

      case .failed(let failure):
        switch failure {
        case .linkLost:
          if !lost {
            emitMarker("link-lost")
            lost = true
            scheduleRetry(now)
          } else if attemptStartedAt != nil {
            attemptStartedAt = nil
            scheduleRetry(now)
          }
          if let nextAttemptAt, now >= nextAttemptAt {
            startAttempt(now)
          }
        case .bluetoothUnavailable, .authenticationUnavailable, .authenticationFailed,
          .authenticationRequired:
          throw failureError(failure, passwordOrigin: .none)
        default:
          throw failureError(failure, passwordOrigin: .none)
        }
        if lost, exhausted(Date()) {
          emitMarker("retry-exhausted")
          break monitorLoop
        }

      default:
        if !lost {
          emitMarker("link-lost")
          lost = true
          scheduleRetry(now)
        }
        if let startedAt = attemptStartedAt,
          now.timeIntervalSince(startedAt) >= attemptTimeout {
          client.stop()
          attemptStartedAt = nil
          emitMarker("attempt-timeout")
          scheduleRetry(now)
        } else if attemptStartedAt == nil, let nextAttemptAt, now >= nextAttemptAt {
          startAttempt(now)
        }
        if exhausted(now) {
          emitMarker("retry-exhausted")
          break monitorLoop
        }
      }

      if CLIMonitorPolicy.reachedDuration(
        elapsed: elapsed(), duration: options.duration) { break }
      monitorWait(seconds: 0.2, interrupt: interrupt)
    }

    note(CLIReport.monitorSummary(
      samples: samples,
      first: first,
      last: last,
      elapsed: Date().timeIntervalSince(start)))
    return CLIExitCode.success.rawValue
  }

  private static func monitorWait(seconds: TimeInterval, interrupt: InterruptFlag) {
    let deadline = Date().addingTimeInterval(seconds)
    while Date() < deadline, !interrupt.isSet {
      RunLoop.main.run(until: min(deadline, Date().addingTimeInterval(0.1)))
    }
  }

  // MARK: - Credentials

  private struct PasswordSetup {
    let store: FurbleCredentialStore
    let origin: CLIPasswordOrigin
  }

  private static func passwordSetup(
    _ connection: CLIConnectionOptions, environment: [String: String]
  ) throws -> PasswordSetup {
    let readFile: (String) throws -> String = { path in
      do {
        return try String(contentsOfFile: path, encoding: .utf8)
      } catch {
        throw CLIUsageError.message("cannot read password file \(path)")
      }
    }
    let readStdin: () throws -> String = {
      let data = FileHandle.standardInput.readDataToEndOfFile()
      guard let text = String(data: data, encoding: .utf8) else {
        throw CLIUsageError.message("standard input is not valid UTF-8")
      }
      return text
    }
    if let resolved = try CLIPasswordResolver.resolve(
      connection.passwordSource,
      environment: environment,
      readFile: readFile,
      readStdin: readStdin),
      let password = resolved.password {
      return PasswordSetup(store: CLICredentialStore(password: password), origin: resolved.origin)
    }

    let keychain = KeychainCredentialStore(
      service: connection.keychainService,
      account: connection.keychainAccount)
    do {
      if let password = try keychain.readPassword(), !password.isEmpty {
        guard FurblePasswordPolicy.isValid(password) else {
          note("warning: the Keychain companion password exceeds the firmware limit")
          return PasswordSetup(store: keychain, origin: .none)
        }
        return PasswordSetup(store: keychain, origin: .keychain)
      }
    } catch {
      note("warning: could not read the companion password from the Keychain: \(error)")
    }
    return PasswordSetup(store: keychain, origin: .none)
  }

  // MARK: - Event pump and messages

  @discardableResult
  private static func pump(until deadline: Date, _ condition: () -> Bool) -> Bool {
    while !condition() && Date() < deadline {
      RunLoop.main.run(until: min(deadline, Date().addingTimeInterval(0.05)))
    }
    return condition()
  }

  private static func wait(seconds: TimeInterval) {
    guard seconds > 0 else { return }
    let deadline = Date().addingTimeInterval(seconds)
    while Date() < deadline {
      RunLoop.main.run(until: min(deadline, Date().addingTimeInterval(0.05)))
    }
  }

  private static func phaseLabel(_ phase: CompanionConnectionPhase) -> String {
    switch phase {
    case .idle: return "idle"
    case .scanning: return "scanning"
    case .connecting: return "connecting"
    case .discovering: return "discovering"
    case .awaitingAuthentication: return "awaiting authentication"
    case .ready: return "ready"
    case .reconnecting(let attempt): return "reconnecting, attempt \(attempt)"
    case .failed(let failure): return "failed: \(failure)"
    }
  }

  private static func bluetoothUnavailableMessage() -> String {
    switch CBManager.authorization {
    case .denied:
      return "Bluetooth permission is denied. Allow the terminal application in "
        + "System Settings > Privacy & Security > Bluetooth, then retry."
    case .restricted:
      return "Bluetooth is restricted on this Mac."
    case .notDetermined, .allowedAlways:
      return "Bluetooth is unavailable. Check that it is powered on and that the terminal "
        + "application has Bluetooth permission in System Settings > Privacy & Security."
    @unknown default:
      return "Bluetooth is unavailable. Check power and Privacy & Security > Bluetooth."
    }
  }

  private static func formatSeconds(_ seconds: TimeInterval) -> String {
    String(format: "%.1f", seconds)
  }

  /// Monitor output goes through an unbuffered FileHandle write so a CSV file
  /// redirected for an hour-long run is visible and durable while it is open.
  private static func stdoutLine(_ line: String) {
    try? FileHandle.standardOutput.write(contentsOf: Data((line + "\n").utf8))
  }

  private static func note(_ message: String) {
    FileHandle.standardError.write(Data((message + "\n").utf8))
  }

  private static func emit(_ message: String, code: CLIExitCode) -> Int32 {
    FileHandle.standardError.write(Data(("error: " + message + "\n").utf8))
    return code.rawValue
  }

  private struct RunError: Error {
    let message: String
    let code: CLIExitCode
  }
}

private final class InterruptFlag: @unchecked Sendable {
  private let lock = NSLock()
  private var value = false

  func set() {
    lock.lock()
    value = true
    lock.unlock()
  }

  var isSet: Bool {
    lock.lock()
    defer { lock.unlock() }
    return value
  }
}
