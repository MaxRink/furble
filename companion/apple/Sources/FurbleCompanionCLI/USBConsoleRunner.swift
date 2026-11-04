import Foundation
import FurbleCompanionCLIKit
import FurbleCompanionCore

/// USB CDC console transport for the macOS CLI.
///
/// USB uses the developer console rather than the BLE companion protocol. It is
/// the fallback when Bluetooth pairing is unavailable, and it exposes the same
/// battery/GPS fields for power runs. Camera trigger and phone-GPS injection
/// remain BLE features because the console has no equivalent command.
enum USBConsoleRunner {
  static func run(
    _ command: CLICommand, connection: CLIConnectionOptions
  ) -> Int32 {
    guard let path = connection.serialPort else {
      return emit("no USB CDC port selected; pass --port", code: .usage)
    }
    switch command {
    case .help:
      print(CLIArgumentParser.usage)
      return CLIExitCode.success.rawValue
    case .scan:
      return scan(connection: connection)
    case .password(.status):
      return passwordStatus(path: path)
    case .password:
      return emit(
        "password set/delete is only supported over the BLE transport; "
          + "use the device console or the native app",
        code: .unsupported)
    case .status:
      return status(path: path)
    case .cameras:
      return cameras(path: path)
    case .monitor(let options):
      return monitor(options, path: path)
    case .location, .trigger:
      return emit(
        "this command requires the BLE companion transport; USB supports scan, status, cameras, "
          + "monitor and password status",
        code: .unsupported)
    }
  }

  private static func scan(connection: CLIConnectionOptions) -> Int32 {
    let ports = connection.serialPort.map { [$0] } ?? FurbleSerialConsole.candidatePorts()
    guard !ports.isEmpty else {
      return emit("no USB CDC port found", code: .deviceNotFound)
    }
    print("USB CDC ports:")
    for port in ports {
      let console = FurbleSerialConsole(path: port)
      do {
        let version = try console.probe()
        let line = version.split(whereSeparator: { $0.isNewline }).first { $0.hasPrefix("version:") }
        print("  \(port)  \(line ?? "furble console")")
      } catch {
        print("  \(port)  not a furble console (\(error))")
      }
      console.close()
    }
    return CLIExitCode.success.rawValue
  }

  private static func status(path: String) -> Int32 {
    let console = FurbleSerialConsole(path: path)
    do {
      try console.open()
      defer { console.close() }
      let status = try parseStatus(console.command("status"))
      let gps = try parseGPS(console.command("gps"))
      let heap = try parseKeyValues(console.command("perf heap"))
      print("USB device: \(path)")
      print("Version: \(status["version"] ?? "unknown")")
      print("State: \(status["state"] ?? "unknown")")
      print("Targets: \(status["targets"] ?? "0")")
      print("Uptime: \(status["uptime"] ?? "0") s")
      print(
        "Battery: \(status["battery"] ?? "unknown")%, \(status["voltage"] ?? "unknown") mV, "
          + "\(status["current"] ?? "unknown") mA")
      print("Heap: \(status["heap"] ?? "unknown") B, min \(status["heap_min"] ?? "unknown") B")
      if let free = heap["heap.internal.free"] {
        print("Internal heap: \(free) B free")
      }
      let source = gps["source"] ?? "none"
      let fix = gps["fix"] ?? "false"
      let satellites = gps["satellites"] ?? "0"
      print("GPS: source \(source), fix \(fix), \(satellites) satellites")
      if let age = gps["age"] {
        print("GPS location age: \(age) ms")
      }
      return CLIExitCode.success.rawValue
    } catch {
      return emit("USB status failed: \(error)", code: .failure)
    }
  }

  private static func cameras(path: String) -> Int32 {
    let console = FurbleSerialConsole(path: path)
    do {
      try console.open()
      defer { console.close() }
      print("Saved cameras:")
      print(withoutEcho(try console.command("cameras list"), command: "cameras list"))
      print("Active cameras:")
      print(withoutEcho(try console.command("cameras status"), command: "cameras status"))
      return CLIExitCode.success.rawValue
    } catch {
      return emit("USB camera query failed: \(error)", code: .failure)
    }
  }

  private static func passwordStatus(path: String) -> Int32 {
    let console = FurbleSerialConsole(path: path)
    do {
      try console.open()
      defer { console.close() }
      let values = try parseKeyValues(console.command("companion password status"))
      print(values["companion.password"] ?? "unavailable")
      return CLIExitCode.success.rawValue
    } catch {
      return emit("USB password status failed: \(error)", code: .failure)
    }
  }

  private static func monitor(_ options: CLIMonitorOptions, path: String) -> Int32 {
    let console = FurbleSerialConsole(path: path)
    let interrupt = USBInterruptFlag()
    signal(SIGINT, SIG_IGN)
    let signalSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    signalSource.setEventHandler { interrupt.set() }
    signalSource.resume()
    defer {
      signalSource.cancel()
      signal(SIGINT, SIG_DFL)
      console.close()
    }

    do {
      try console.open()
    } catch {
      return emit("USB monitor failed: \(error)", code: .failure)
    }

    let start = Date()
    var samples = 0
    var first: Sample?
    var last: Sample?
    var nextSample = start
    var lastStatusAt = start
    var lost = false
    var retry = CLIMonitorBackoff()

    if options.csv, options.includeHeader {
      print(CLIReport.monitorCSVHeader)
    }
    note(
      "Monitoring \(path) every \(formatSeconds(options.interval)) s"
        + (options.duration.map { " for \(formatSeconds($0)) s" } ?? " until Ctrl-C")
        + (options.untilEmpty ? " or battery <= 1%" : "") + ".")

    func elapsed() -> TimeInterval { Date().timeIntervalSince(start) }
    func emitMarker(_ event: String) {
      let now = Date()
      if options.csv {
        print(CLIReport.monitorMarkerRow(
          timestamp: now, elapsed: now.timeIntervalSince(start), event: event))
      } else {
        print("\(CLIReport.iso8601Timestamp(now)) elapsed="
          + "\(formatSeconds(now.timeIntervalSince(start)))s event=\(event)")
      }
    }

    while true {
      if interrupt.isSet { break }
      if CLIMonitorPolicy.reachedDuration(elapsed: elapsed(), duration: options.duration) {
        break
      }
      let now = Date()
      if now >= nextSample {
        do {
          try console.open()
          let status = try parseStatus(console.command("status"))
          let gps = try parseGPS(console.command("gps"))
          if lost {
            emitMarker("reconnected")
            lost = false
            retry.reset()
          }
          lastStatusAt = Date()
          let sampleTime = Date()
          let sample = Sample(
            timestamp: sampleTime,
            elapsed: sampleTime.timeIntervalSince(start),
            uptime: UInt32(status["uptime"] ?? "0") ?? 0,
            batteryPercent: UInt8(status["battery"] ?? "255") ?? 255,
            batteryMillivolts: UInt16(status["voltage"] ?? "65535") ?? 65535,
            batteryMilliamps: Int16(status["current"] ?? "0") ?? 0,
            gpsSource: gpsSourceCode(gps["source"]),
            gpsSatellites: UInt8(gps["satellites"] ?? "0") ?? 0,
            controlState: controlStateCode(status["state"]),
            rssi: nil)
          if options.csv {
            print(CLIReport.monitorCSVRow(
              timestamp: sample.timestamp,
              elapsed: sample.elapsed,
              status: sample.status,
              rssi: nil,
              label: options.label))
          } else {
            print(CLIReport.monitorHumanRow(
              timestamp: sample.timestamp,
              elapsed: sample.elapsed,
              status: sample.status,
              rssi: nil,
              label: options.label))
          }
          samples += 1
          if first == nil { first = sample }
          last = sample
          nextSample = sampleTime.addingTimeInterval(options.interval)
          if CLIMonitorPolicy.reachedEmpty(
            batteryPercent: sample.status.batteryPercent, untilEmpty: options.untilEmpty) {
            emitMarker("battery-empty")
            break
          }
        } catch {
          console.close()
          if !lost {
            emitMarker("link-lost")
            lost = true
          }
          let delay = retry.nextDelay()
          let until = Date().addingTimeInterval(delay)
          note("USB reconnect in \(formatSeconds(delay)) s: \(error)")
          while Date() < until {
            RunLoop.main.run(until: min(until, Date().addingTimeInterval(0.1)))
          }
          if CLIMonitorPolicy.retryExhausted(
            lastActivity: lastStatusAt, now: Date(), retryWindow: options.retryWindow) {
            emitMarker("retry-exhausted")
            break
          }
        }
      }
      RunLoop.main.run(until: min(nextSample, Date().addingTimeInterval(0.1)))
    }

    note(CLIReport.monitorSummary(
      samples: samples, first: first?.status, last: last?.status, elapsed: elapsed()))
    return CLIExitCode.success.rawValue
  }

  private struct Sample {
    let timestamp: Date
    let elapsed: TimeInterval
    let uptime: UInt32
    let batteryPercent: UInt8
    let batteryMillivolts: UInt16
    let batteryMilliamps: Int16
    let gpsSource: UInt8
    let gpsSatellites: UInt8
    let controlState: UInt8
    let rssi: Int?

    var status: FurbleProtocol.Status {
      FurbleProtocol.Status(
        version: 1,
        batteryPercent: batteryPercent,
        batteryMillivolts: batteryMillivolts,
        batteryMilliamps: batteryMilliamps,
        // The console status has no power flags; do not invent charging state.
        powerFlags: 0,
        cameraTotal: 0,
        cameraConnected: 0,
        controlState: controlState,
        gpsSource: gpsSource,
        gpsSatellites: gpsSatellites,
        intervalometerState: 0,
        intervalometerRemaining: 0,
        uptimeSeconds: uptime)
    }
  }

  private static func withoutEcho(_ text: String, command: String) -> String {
    var lines = text.split(whereSeparator: { $0.isNewline })
    if let first = lines.first,
      first.trimmingCharacters(in: .whitespacesAndNewlines) == command {
      lines.removeFirst()
    }
    return lines.joined(separator: "\n")
  }

  private static func parseStatus(_ text: String) throws -> [String: String] {
    let values = try parseKeyValues(text)
    guard values["version"] != nil else {
      throw FurbleSerialError.notFurble
    }
    return values
  }

  private static func parseGPS(_ text: String) throws -> [String: String] {
    try parseKeyValues(text)
  }

  private static func parseKeyValues(_ text: String) throws -> [String: String] {
    var values: [String: String] = [:]
    for line in text.split(whereSeparator: { $0.isNewline }) {
      guard let separator = line.firstIndex(of: ":") else { continue }
      let key = line[..<separator].trimmingCharacters(in: .whitespaces)
      let value = line[line.index(after: separator)...].trimmingCharacters(in: .whitespaces)
      if !key.isEmpty { values[key] = value }
    }
    guard !values.isEmpty else { throw FurbleSerialError.notFurble }
    if ProcessInfo.processInfo.environment["FURBLE_COMPANION_DEBUG"] != nil {
      FileHandle.standardError.write(Data("[usb] keys: \(values.keys.sorted())\n".utf8))
    }
    return values
  }

  private static func gpsSourceCode(_ value: String?) -> UInt8 {
    switch value {
    case "uart": return 1
    case "companion": return 2
    default: return 0
    }
  }

  private static func controlStateCode(_ value: String?) -> UInt8 {
    // Matches Control::state_t on the wire: idle=0, connect=1, connecting=2,
    // connect_failed=3, active=4, disconnecting=5.
    switch value {
    case "idle": return 0
    case "connect": return 1
    case "connecting": return 2
    case "connect_failed": return 3
    case "active", "connected": return 4
    case "disconnecting": return 5
    default: return 255
    }
  }

  private static func formatSeconds(_ value: TimeInterval) -> String {
    String(format: "%.1f", value)
  }

  private static func emit(_ message: String, code: CLIExitCode) -> Int32 {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    return code.rawValue
  }

  private static func note(_ message: String) {
    FileHandle.standardError.write(Data((message + "\n").utf8))
  }
}

private final class USBInterruptFlag: @unchecked Sendable {
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
