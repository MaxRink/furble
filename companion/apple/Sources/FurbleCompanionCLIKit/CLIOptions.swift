import Foundation
import FurbleCompanionCore

public enum CLIExitCode: Int32, Equatable, Sendable {
  case success = 0
  case failure = 1
  case usage = 2
  case bluetoothUnavailable = 3
  case deviceNotFound = 4
  case authenticationFailed = 5
  case timeout = 6
  case unsupported = 7
}

public enum CLIUsageError: Error, Equatable, Sendable, CustomStringConvertible {
  case message(String)

  public var description: String {
    switch self {
    case .message(let message): return message
    }
  }
}

/// The user can identify a device by the advertised name or by the
/// CoreBluetooth peripheral UUID. macOS does not expose the BLE MAC address.
public enum CLIDeviceQuery: Equatable, Sendable {
  case any
  case name(String)
  case identifier(UUID)

  public static func parse(_ raw: String) -> CLIDeviceQuery {
    let value = raw.trimmingCharacters(in: .whitespacesAndNewlines)
    if let identifier = UUID(uuidString: value) {
      return .identifier(identifier)
    }
    return .name(value)
  }

  public func matches(_ device: FurbleDiscoveredDevice) -> Bool {
    switch self {
    case .any:
      return true
    case .name(let needle):
      return FurbleDeviceSelection.nameContains(needle).matches(device)
    case .identifier(let identifier):
      return device.id == identifier
    }
  }

  public var selection: FurbleDeviceSelection {
    switch self {
    case .any: return .any
    case .name(let needle): return .nameContains(needle)
    case .identifier(let identifier): return .identifier(identifier)
    }
  }
}

public enum CLIPasswordSource: Equatable, Sendable {
  case automatic
  case argument(String)
  case environment(String)
  case file(String)
  case stdin
}

public struct CLIConnectionOptions: Equatable, Sendable {
  public var device: CLIDeviceQuery = .any
  public var scanTimeout: TimeInterval = 5
  public var connectionTimeout: TimeInterval = 20
  public var passwordSource: CLIPasswordSource = .automatic
  public var keychainService: String = "com.furble.companion"
  public var keychainAccount: String = "furble-password"

  public init() {}
}

public struct CLILocationOptions: Equatable, Sendable {
  public var latitude: Double
  public var longitude: Double
  public var altitude: Double?
  public var accuracyMeters: UInt8?
  public var ageMilliseconds: UInt32 = 0
  public var satellites: UInt8 = 0
  public var timestamp: Date?

  public init(
    latitude: Double,
    longitude: Double,
    altitude: Double? = nil,
    accuracyMeters: UInt8? = nil,
    ageMilliseconds: UInt32 = 0,
    satellites: UInt8 = 0,
    timestamp: Date? = nil
  ) {
    self.latitude = latitude
    self.longitude = longitude
    self.altitude = altitude
    self.accuracyMeters = accuracyMeters
    self.ageMilliseconds = ageMilliseconds
    self.satellites = satellites
    self.timestamp = timestamp
  }

  /// Builds the same UTC location record used by the Core Location bridge.
  /// `positionValid` and `timeValid` are set because the CLI requires an
  /// explicit position and an explicit or current timestamp.
  public func makeFix(now: Date = Date()) throws -> FurbleProtocol.LocationFix {
    guard latitude.isFinite, longitude.isFinite,
      (-90...90).contains(latitude), (-180...180).contains(longitude) else {
      throw CLIUsageError.message("latitude must be -90...90 and longitude must be -180...180")
    }
    if let altitude, !altitude.isFinite {
      throw CLIUsageError.message("altitude must be a finite number")
    }
    if let accuracyMeters, accuracyMeters == 255 {
      throw CLIUsageError.message("accuracy must be 0...254; 255 means unknown and must be omitted")
    }
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(secondsFromGMT: 0) ?? .current
    let date = timestamp ?? now
    let components = calendar.dateComponents(
      [.year, .month, .day, .hour, .minute, .second, .nanosecond], from: date)
    guard let year = components.year, let month = components.month, let day = components.day,
      let hour = components.hour, let minute = components.minute, let second = components.second,
      year >= 0, year <= Int(UInt16.max) else {
      throw CLIUsageError.message("fix time is out of range")
    }
    let centisecond = UInt8(clamping: (components.nanosecond ?? 0) / 10_000_000)
    return FurbleProtocol.LocationFix(
      positionValid: true,
      timeValid: true,
      altitudeValid: altitude != nil,
      satellites: satellites,
      accuracyMeters: accuracyMeters,
      latitude: latitude,
      longitude: longitude,
      altitude: altitude ?? 0,
      year: UInt16(clamping: year),
      month: UInt8(clamping: month),
      day: UInt8(clamping: day),
      hour: UInt8(clamping: hour),
      minute: UInt8(clamping: minute),
      second: UInt8(clamping: second),
      centisecond: centisecond,
      ageMilliseconds: ageMilliseconds)
  }
}

public struct CLITriggerOptions: Equatable, Sendable {
  public var focus: Bool
  public var holdMilliseconds: UInt16

  public init(focus: Bool, holdMilliseconds: UInt16 = 500) {
    self.focus = focus
    self.holdMilliseconds = holdMilliseconds
  }
}

public struct CLIMonitorOptions: Equatable, Sendable {
  public var interval: TimeInterval
  /// nil runs until SIGINT. A duration lets a battery run finish on its own.
  public var duration: TimeInterval?
  public var untilEmpty: Bool
  public var csv: Bool
  public var includeHeader: Bool
  public var retryWindow: TimeInterval
  public var label: String

  public init(
    interval: TimeInterval = 5,
    duration: TimeInterval? = nil,
    untilEmpty: Bool = false,
    csv: Bool = false,
    includeHeader: Bool = true,
    retryWindow: TimeInterval = 180,
    label: String = ""
  ) {
    self.interval = interval
    self.duration = duration
    self.untilEmpty = untilEmpty
    self.csv = csv
    self.includeHeader = includeHeader
    self.retryWindow = retryWindow
    self.label = label
  }
}

public enum CLIPasswordCommand: Equatable, Sendable {
  case set
  case delete
  case status
}

public enum CLICommand: Equatable, Sendable {
  case help
  case scan
  case status
  case cameras
  case location(CLILocationOptions)
  case trigger(CLITriggerOptions)
  case monitor(CLIMonitorOptions)
  case password(CLIPasswordCommand)
}

public struct CLIInvocation: Equatable, Sendable {
  public var command: CLICommand
  public var connection: CLIConnectionOptions

  public init(command: CLICommand, connection: CLIConnectionOptions = CLIConnectionOptions()) {
    self.command = command
    self.connection = connection
  }
}

public enum CLIArgumentParser {
  public static let usage = """
    USAGE
      furble-companion-cli <command> [options]

    COMMANDS
      scan        Discover furble companion devices (name, UUID, RSSI)
      status      Connect, authenticate, print status, capabilities and cameras
      cameras     Connect, authenticate, refresh and print the camera catalog
      location    Connect, authenticate and send a phone-GPS fallback fix
      trigger     Connect, authenticate and press then release shutter or focus
      monitor     Poll status notifications for battery/power observations
      password    set | delete | status for the local Keychain item
      help        Show this help

    CONNECTION OPTIONS
      --device NAME|UUID     Device name substring or macOS peripheral UUID
      --scan-timeout SEC     Scan window in seconds (default 5)
      --timeout SEC          Connect and authentication timeout (default 20)
      --password VALUE       Discouraged: visible in the process list
      --password-env VAR     Read the password from environment variable VAR
      --password-file PATH   Read the password from the first line of PATH
      --password-stdin       Read the password from standard input
      --keychain-service S   Keychain service (default com.furble.companion)
      --keychain-account A   Keychain account (default furble-password)

      With no explicit source, FURBLE_COMPANION_PASSWORD is used when set,
      otherwise the Keychain item. The password is never printed. A device
      whose companion password is unset authenticates with the firmware's
      explicit not-required result; no local password is needed for that.

    LOCATION OPTIONS
      --latitude DEG        Required position, -90...90
      --longitude DEG       Required position, -180...180
      --altitude M          Optional altitude; also marks altitude valid
      --accuracy M          Horizontal accuracy 0...254 (omit for unknown)
      --age-ms N            Fix age in milliseconds (default 0)
      --satellites N        Satellite count (default 0)
      --time ISO8601        Fix timestamp (default now, UTC)

    TRIGGER OPTIONS
      --hold-ms N           Milliseconds between press and release (default 500)

    MONITOR OPTIONS
      --interval SEC        Status sample interval (default 5)
      --duration SEC        Stop after SEC seconds (default until Ctrl-C)
      --until-empty         Stop after battery_percent falls to 1 or below
      --retry-window SEC    Stop after this long without a reachable device (default 180)
      --label TEXT          Phase label stored in every CSV row
      --csv                 Write only CSV rows to stdout; progress goes to stderr
      --no-header           Omit the CSV header (useful when appending runs)

    PASSWORD COMMANDS
      password set          Prompt without echo and store in the login Keychain
      password status       Print set or unset; never print the value
      password delete       Remove the local Keychain item

    EXIT CODES
      0 success, 1 failure, 2 usage, 3 Bluetooth unavailable,
      4 device not found, 5 authentication failed, 6 timeout,
      7 unsupported by this firmware
    """

  public static func parse(_ arguments: [String]) throws -> CLIInvocation {
    guard let first = arguments.first else {
      return CLIInvocation(command: .help)
    }
    if arguments.contains("--help") || arguments.contains("-h") || first == "help" {
      return CLIInvocation(command: .help)
    }

    var connection = CLIConnectionOptions()
    var location: CLILocationOptions?
    var command: CLICommand
    var index = 1
    var passwordSourceCount = 0

    func takeValue(_ option: String, allowDash: Bool = false) throws -> String {
      guard index + 1 < arguments.count else {
        throw CLIUsageError.message("\(option) requires a value")
      }
      let value = arguments[index + 1]
      if !allowDash && value.hasPrefix("-") {
        throw CLIUsageError.message("\(option) requires a value")
      }
      index += 1
      return value
    }

    switch first {
    case "scan": command = .scan
    case "status": command = .status
    case "cameras": command = .cameras
    case "location":
      let options = CLILocationOptions(latitude: .nan, longitude: .nan)
      location = options
      command = .location(options)
    case "trigger":
      guard arguments.count > 1 else {
        throw CLIUsageError.message("trigger requires shutter or focus")
      }
      switch arguments[1] {
      case "shutter": command = .trigger(CLITriggerOptions(focus: false))
      case "focus": command = .trigger(CLITriggerOptions(focus: true))
      default:
        throw CLIUsageError.message("trigger requires shutter or focus, not '\(arguments[1])'")
      }
      index = 2
    case "monitor":
      command = .monitor(CLIMonitorOptions())
    case "password":
      guard arguments.count > 1 else {
        throw CLIUsageError.message("password requires set, delete or status")
      }
      switch arguments[1] {
      case "set": command = .password(.set)
      case "delete": command = .password(.delete)
      case "status": command = .password(.status)
      default:
        throw CLIUsageError.message("password requires set, delete or status, not '\(arguments[1])'")
      }
      index = 2
    default:
      throw CLIUsageError.message("unknown command '\(first)'")
    }

    while index < arguments.count {
      let raw = arguments[index]
      let option: String
      let inlineValue: String?
      if raw.hasPrefix("--"), let equals = raw.firstIndex(of: "=") {
        option = String(raw[..<equals])
        inlineValue = String(raw[raw.index(after: equals)...])
      } else {
        option = raw
        inlineValue = nil
      }

      func value(allowDash: Bool = false) throws -> String {
        if let inlineValue { return inlineValue }
        return try takeValue(option, allowDash: allowDash)
      }

      switch option {
      case "--device":
        let device = try value()
        guard !device.isEmpty else { throw CLIUsageError.message("--device must not be empty") }
        connection.device = CLIDeviceQuery.parse(device)
      case "--scan-timeout":
        connection.scanTimeout = try positiveSeconds(try value(), option: option)
      case "--timeout":
        connection.connectionTimeout = try positiveSeconds(try value(), option: option)
      case "--keychain-service":
        connection.keychainService = try nonEmpty(try value(), option: option)
      case "--keychain-account":
        connection.keychainAccount = try nonEmpty(try value(), option: option)
      case "--password":
        passwordSourceCount += 1
        connection.passwordSource = .argument(try value(allowDash: true))
      case "--password-env":
        passwordSourceCount += 1
        connection.passwordSource = .environment(try nonEmpty(try value(), option: option))
      case "--password-file":
        passwordSourceCount += 1
        connection.passwordSource = .file(try nonEmpty(try value(), option: option))
      case "--password-stdin":
        passwordSourceCount += 1
        connection.passwordSource = .stdin
      case "--latitude":
        var options = try locationOptions(command, location)
        options.latitude = try finiteDouble(try value(allowDash: true), option: option)
        location = options
      case "--longitude":
        var options = try locationOptions(command, location)
        options.longitude = try finiteDouble(try value(allowDash: true), option: option)
        location = options
      case "--altitude":
        var options = try locationOptions(command, location)
        options.altitude = try finiteDouble(try value(allowDash: true), option: option)
        location = options
      case "--accuracy":
        var options = try locationOptions(command, location)
        options.accuracyMeters = try uint8(try value(), option: option, maximum: 254)
        location = options
      case "--age-ms":
        var options = try locationOptions(command, location)
        options.ageMilliseconds = try uint32(try value(), option: option)
        location = options
      case "--satellites":
        var options = try locationOptions(command, location)
        options.satellites = try uint8(try value(), option: option, maximum: 255)
        location = options
      case "--time":
        var options = try locationOptions(command, location)
        options.timestamp = try iso8601(try value(), option: option)
        location = options
      case "--interval":
        guard case .monitor(var options) = command else {
          throw CLIUsageError.message("--interval is only valid for monitor")
        }
        options.interval = try positiveSeconds(
          try value(), option: option, maximum: 3600)
        command = .monitor(options)
      case "--duration":
        guard case .monitor(var options) = command else {
          throw CLIUsageError.message("--duration is only valid for monitor")
        }
        options.duration = try positiveSeconds(
          try value(), option: option, maximum: 86_400)
        command = .monitor(options)
      case "--until-empty":
        guard case .monitor(var options) = command else {
          throw CLIUsageError.message("--until-empty is only valid for monitor")
        }
        options.untilEmpty = true
        command = .monitor(options)
      case "--retry-window":
        guard case .monitor(var options) = command else {
          throw CLIUsageError.message("--retry-window is only valid for monitor")
        }
        options.retryWindow = try positiveSeconds(
          try value(), option: option, maximum: 3600)
        command = .monitor(options)
      case "--label":
        guard case .monitor(var options) = command else {
          throw CLIUsageError.message("--label is only valid for monitor")
        }
        let label = try nonEmpty(try value(allowDash: true), option: option)
        guard !label.contains("\n"), !label.contains("\r") else {
          throw CLIUsageError.message("--label must be a single line")
        }
        options.label = label
        command = .monitor(options)
      case "--csv":
        guard case .monitor(var options) = command else {
          throw CLIUsageError.message("--csv is only valid for monitor")
        }
        options.csv = true
        command = .monitor(options)
      case "--no-header":
        guard case .monitor(var options) = command else {
          throw CLIUsageError.message("--no-header is only valid for monitor")
        }
        options.includeHeader = false
        command = .monitor(options)
      case "--hold-ms":
        guard case .trigger(var options) = command else {
          throw CLIUsageError.message("--hold-ms is only valid for trigger")
        }
        options.holdMilliseconds = try uint16(try value(), option: option, maximum: 60_000)
        command = .trigger(options)
      case "--help", "-h":
        return CLIInvocation(command: .help)
      default:
        throw CLIUsageError.message("unknown option '\(option)'")
      }
      index += 1
    }

    if passwordSourceCount > 1 {
      throw CLIUsageError.message("use only one of --password, --password-env, --password-file or --password-stdin")
    }

    switch command {
    case .location:
      guard let location else {
        throw CLIUsageError.message("location requires --latitude and --longitude")
      }
      guard location.latitude.isFinite, location.longitude.isFinite else {
        throw CLIUsageError.message("location requires --latitude and --longitude")
      }
      guard (-90...90).contains(location.latitude),
        (-180...180).contains(location.longitude) else {
        throw CLIUsageError.message("latitude must be -90...90 and longitude must be -180...180")
      }
      command = .location(location)
    case .trigger, .monitor, .password:
      break
    default:
      break
    }

    return CLIInvocation(command: command, connection: connection)
  }

  private static func locationOptions(
    _ command: CLICommand, _ current: CLILocationOptions?
  ) throws -> CLILocationOptions {
    guard case .location = command else {
      throw CLIUsageError.message("location options require the location command")
    }
    guard let current else {
      throw CLIUsageError.message("location options require the location command")
    }
    return current
  }

  private static func nonEmpty(_ value: String, option: String) throws -> String {
    guard !value.isEmpty else { throw CLIUsageError.message("\(option) must not be empty") }
    return value
  }

  private static func positiveSeconds(
    _ value: String, option: String, maximum: TimeInterval = 300
  ) throws -> TimeInterval {
    guard let seconds = TimeInterval(value), seconds > 0, seconds <= maximum else {
      throw CLIUsageError.message("\(option) must be a number of seconds in 1...\(Int(maximum))")
    }
    return seconds
  }

  private static func finiteDouble(_ value: String, option: String) throws -> Double {
    guard let number = Double(value), number.isFinite else {
      throw CLIUsageError.message("\(option) must be a finite number")
    }
    return number
  }

  private static func uint8(_ value: String, option: String, maximum: UInt8) throws -> UInt8 {
    guard let number = UInt8(value), number <= maximum else {
      throw CLIUsageError.message("\(option) must be an integer in 0...\(maximum)")
    }
    return number
  }

  private static func uint16(_ value: String, option: String, maximum: UInt16) throws -> UInt16 {
    guard let number = UInt16(value), number <= maximum else {
      throw CLIUsageError.message("\(option) must be an integer in 0...\(maximum)")
    }
    return number
  }

  private static func uint32(_ value: String, option: String) throws -> UInt32 {
    guard let number = UInt32(value) else {
      throw CLIUsageError.message("\(option) must be an unsigned integer")
    }
    return number
  }

  private static func iso8601(_ value: String, option: String) throws -> Date {
    let fractional = ISO8601DateFormatter()
    fractional.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    if let date = fractional.date(from: value) { return date }
    let plain = ISO8601DateFormatter()
    plain.formatOptions = [.withInternetDateTime]
    if let date = plain.date(from: value) { return date }
    throw CLIUsageError.message("\(option) must be an ISO-8601 timestamp")
  }
}
