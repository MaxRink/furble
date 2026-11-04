import Foundation
import FurbleCompanionCore

public struct CLIStatusReport: Equatable, Sendable {
  public let device: FurbleDiscoveredDevice
  public let status: FurbleProtocol.Status
  public let capability: FurbleProtocol.Capability
  public let authentication: CompanionAuthenticationMode?
  public let passwordOrigin: CLIPasswordOrigin
  public let cameras: [FurbleProtocol.CameraRecord]
  public let cameraListSupported: Bool
  public let cameraListError: UInt8?
  public let cameraOperationError: String?

  public init(
    device: FurbleDiscoveredDevice,
    status: FurbleProtocol.Status,
    capability: FurbleProtocol.Capability,
    authentication: CompanionAuthenticationMode?,
    passwordOrigin: CLIPasswordOrigin,
    cameras: [FurbleProtocol.CameraRecord],
    cameraListSupported: Bool,
    cameraListError: UInt8? = nil,
    cameraOperationError: String? = nil
  ) {
    self.device = device
    self.status = status
    self.capability = capability
    self.authentication = authentication
    self.passwordOrigin = passwordOrigin
    self.cameras = cameras
    self.cameraListSupported = cameraListSupported
    self.cameraListError = cameraListError
    self.cameraOperationError = cameraOperationError
  }
}

public enum CLIReport {
  /// Fixed-point output is locale-independent so CSV never gains a decimal
  /// comma that would split a field on a comma-decimal host.
  private static let decimalLocale = Locale(identifier: "en_US_POSIX")

  private static func decimalString(_ value: TimeInterval) -> String {
    String(format: "%.1f", locale: decimalLocale, value)
  }

  /// One row per status notification sample. Raw GPS source codes are kept
  /// numeric for plotting; the human form maps them to labels.
  public static let monitorCSVHeader =
    "host_time_iso,host_elapsed_s,uptime_s,battery_percent,battery_mv,battery_ma,"
    + "gps_source,gps_satellites,control_state,rssi,label"

  public static func monitorCSVRow(
    timestamp: Date,
    elapsed: TimeInterval,
    status: FurbleProtocol.Status,
    rssi: Int?,
    label: String
  ) -> String {
    [
      csvField(iso8601Timestamp(timestamp)),
      decimalString(elapsed),
      String(status.uptimeSeconds),
      String(status.batteryPercent),
      String(status.batteryMillivolts),
      String(status.batteryMilliamps),
      String(status.gpsSource),
      String(status.gpsSatellites),
      String(status.controlState),
      rssi.map(String.init) ?? "",
      csvField(label)
    ].joined(separator: ",")
  }

  /// A reconnect/event marker keeps the same column count but leaves the
  /// measurement fields empty. The plot script treats those rows as vertical
  /// event markers and never draws them as zero-valued samples.
  public static func monitorMarkerRow(
    timestamp: Date, elapsed: TimeInterval, event: String
  ) -> String {
    [
      csvField(iso8601Timestamp(timestamp)),
      decimalString(elapsed),
      "", "", "", "", "", "", "", "",
      csvField(event)
    ].joined(separator: ",")
  }

  public static func monitorHumanRow(
    timestamp: Date, elapsed: TimeInterval, status: FurbleProtocol.Status,
    rssi: Int?, label: String
  ) -> String {
    let battery = status.batteryPercent == 255 ? "unknown" : "\(status.batteryPercent)%"
    var row = "\(iso8601Timestamp(timestamp)) elapsed="
      + "\(decimalString(elapsed))s uptime=\(status.uptimeSeconds)s "
      + "battery=\(battery) \(status.batteryMillivolts)mV \(status.batteryMilliamps)mA "
      + "gps=\(gpsSourceLabel(status.gpsSource))/\(status.gpsSatellites) "
      + "control=\(status.controlState) "
      + "external=\(status.externalPower ? 1 : 0) charging=\(status.charging ? 1 : 0)"
    if let rssi { row += " rssi=\(rssi)dBm" }
    if !label.isEmpty { row += " label=\(label)" }
    return row
  }

  public static func monitorSummary(
    samples: Int,
    first: FurbleProtocol.Status?,
    last: FurbleProtocol.Status?,
    elapsed: TimeInterval
  ) -> String {
    var summary = "samples=\(samples) elapsed=\(decimalString(elapsed))s"
    if let first, let last {
      summary += " battery=\(batteryPercentLabel(first.batteryPercent))"
        + "->\(batteryPercentLabel(last.batteryPercent))"
      summary += " \(first.batteryMillivolts)mV->\(last.batteryMillivolts)mV"
    }
    return summary
  }

  /// 255 is the wire sentinel for "the gauge has no percentage"; never
  /// interpolate it as a real reading in a summary.
  private static func batteryPercentLabel(_ percent: UInt8) -> String {
    percent == 255 ? "unknown" : "\(percent)%"
  }

  public static func iso8601Timestamp(_ date: Date) -> String {
    let formatter = ISO8601DateFormatter()
    formatter.formatOptions = [.withInternetDateTime]
    formatter.timeZone = TimeZone(secondsFromGMT: 0)
    return formatter.string(from: date)
  }

  public static func scanHeader(count: Int) -> String {
    "Discovered \(count) furble companion device\(count == 1 ? "" : "s"):"
  }

  public static func deviceLine(_ device: FurbleDiscoveredDevice) -> String {
    "  \(device.displayName)  id=\(device.id.uuidString)  rssi=\(device.rssi) dBm"
  }

  public static func status(_ report: CLIStatusReport) -> String {
    var lines: [String] = []
    lines.append("Device: \(report.device.displayName)")
    lines.append("Peripheral: \(report.device.id.uuidString)")
    lines.append("Authentication: \(authenticationLabel(report.authentication))")
    lines.append("Password source: \(passwordOriginLabel(report.passwordOrigin))")
    lines.append("Protocol version: \(report.status.version)")
    lines.append(
      "Capability: version \(report.capability.version), settings wire \(report.capability.wireVersion)")
    lines.append("Features: \(featureLabel(report.capability))")

    let battery = report.status.batteryPercent == 255
      ? "unknown" : "\(report.status.batteryPercent)%"
    var power = ["\(report.status.batteryMillivolts) mV", "\(report.status.batteryMilliamps) mA"]
    if report.status.charging { power.append("charging") }
    if report.status.externalPower { power.append("external power") }
    lines.append("Battery: \(battery), \(power.joined(separator: ", "))")
    lines.append(
      "Cameras: \(report.status.cameraConnected)/\(report.status.cameraTotal) connected")
    lines.append(
      "GPS: \(gpsSourceLabel(report.status.gpsSource)), \(report.status.gpsSatellites) satellites")
    lines.append(
      "Intervalometer: state \(report.status.intervalometerState), "
        + "\(report.status.intervalometerRemaining) remaining")
    lines.append("Control state: \(report.status.controlState)")
    lines.append("Uptime: \(report.status.uptimeSeconds) s")
    lines.append(cameraSection(report))
    return lines.joined(separator: "\n")
  }

  public static func authenticationLabel(_ mode: CompanionAuthenticationMode?) -> String {
    switch mode {
    case .password: return "authenticated with the companion password"
    case .notRequired:
      return "authenticated; firmware reports no companion password is set"
    case nil: return "not authenticated"
    }
  }

  public static func passwordOriginLabel(_ origin: CLIPasswordOrigin) -> String {
    switch origin {
    case .argument: return "command-line argument (visible in the process list)"
    case .environment: return "environment variable"
    case .file: return "password file"
    case .stdin: return "standard input"
    case .keychain: return "login Keychain"
    case .none: return "not supplied (device password may be unset)"
    }
  }

  public static func gpsSourceLabel(_ source: UInt8) -> String {
    switch source {
    case 0: return "no fix"
    case 1: return "wired receiver"
    case 2: return "phone companion"
    default: return "unknown source \(source)"
    }
  }

  public static func cameraErrorLabel(_ status: UInt8) -> String {
    switch status {
    case 0: return "ok"
    case 1: return "unknown camera"
    case 2: return "request rejected"
    case 3: return "camera operation busy"
    default: return "camera error \(status)"
    }
  }

  public static func triggerTargetLabel(focus: Bool) -> String {
    focus ? "focus" : "shutter"
  }

  private static func csvField(_ value: String) -> String {
    guard value.contains(where: { $0 == "," || $0 == "\"" || $0 == "\n" || $0 == "\r" }) else {
      return value
    }
    return "\"" + value.replacingOccurrences(of: "\"", with: "\"\"") + "\""
  }

  private static func featureLabel(_ capability: FurbleProtocol.Capability) -> String {
    var features: [String] = []
    if capability.supportsSettings { features.append("settings-v2") }
    if capability.supportsCameras { features.append("cameras") }
    return features.isEmpty ? "none advertised" : features.joined(separator: ", ")
  }

  private static func cameraSection(_ report: CLIStatusReport) -> String {
    guard report.cameraListSupported else {
      return "Camera catalog: unsupported; firmware does not advertise capability bit 1"
    }
    if let error = report.cameraListError {
      return "Camera catalog: failed: \(cameraErrorLabel(error)) (\(error))"
    }
    var lines = ["Camera catalog:"]
    if let operationError = report.cameraOperationError {
      lines.append("  Last camera request: \(operationError)")
    }
    if report.cameras.isEmpty {
      lines.append("  (no saved cameras reported)")
    } else {
      for camera in report.cameras {
        lines.append("  " + cameraLine(camera))
      }
    }
    return lines.joined(separator: "\n")
  }

  private static func cameraLine(_ camera: FurbleProtocol.CameraRecord) -> String {
    var flags: [String] = []
    if camera.isSaved { flags.append("saved") }
    if camera.isSelected { flags.append("selected") }
    if camera.isConnected { flags.append("connected") }
    if flags.isEmpty { flags.append("transient") }
    let name = camera.name.isEmpty ? "Camera \(camera.cameraID)" : camera.name
    var line =
      "id=\(camera.cameraID) name=\"\(name)\" type=\(camera.typeLabel) "
      + "flags=\(flags.joined(separator: ",")) state=\(camera.connectionStateLabel)"
    if camera.isConnected, camera.rssi != -128 {
      line += " rssi=\(camera.rssi) dBm"
    }
    if let status = camera.operationStatusLabel {
      line += " last=\(status)"
    }
    return line
  }
}
