import Foundation
import Testing
@testable import FurbleCompanionCLIKit
import FurbleCompanionCore

struct CLIArgumentParserTests {
  @Test func emptyArgumentsShowHelp() throws {
    #expect(try CLIArgumentParser.parse([]).command == .help)
    #expect(try CLIArgumentParser.parse(["help"]).command == .help)
    #expect(try CLIArgumentParser.parse(["status", "--help"]).command == .help)
  }

  @Test func scanAcceptsNameAndCaseInsensitiveTimeout() throws {
    let invocation = try CLIArgumentParser.parse(
      ["scan", "--device", "Furble", "--scan-timeout", "2.5"])
    #expect(invocation.command == .scan)
    #expect(invocation.connection.device == .name("Furble"))
    #expect(invocation.connection.scanTimeout == 2.5)
  }

  @Test func deviceQueryRecognisesMacPeripheralUUIDs() {
    let uuid = UUID(uuidString: "11111111-2222-3333-4444-555555555555")!
    #expect(CLIDeviceQuery.parse(uuid.uuidString) == .identifier(uuid))
    #expect(CLIDeviceQuery.parse("Furble-1A2B") == .name("Furble-1A2B"))
    #expect(CLIDeviceQuery.name("Furble").selection == FurbleDeviceSelection.nameContains("Furble"))
  }

  @Test func statusParsesPasswordSourceAndKeychainNames() throws {
    let invocation = try CLIArgumentParser.parse([
      "status", "--password-env", "FURBLE_PW", "--keychain-service", "svc",
      "--keychain-account", "acct", "--timeout", "30"
    ])
    #expect(invocation.command == .status)
    #expect(invocation.connection.passwordSource == .environment("FURBLE_PW"))
    #expect(invocation.connection.keychainService == "svc")
    #expect(invocation.connection.keychainAccount == "acct")
    #expect(invocation.connection.connectionTimeout == 30)
  }

  @Test func locationBuildsEveryFieldIntoTheSharedRecord() throws {
    let invocation = try CLIArgumentParser.parse([
      "location", "--latitude", "52.5", "--longitude", "13.4", "--altitude", "34",
      "--accuracy", "5", "--age-ms", "1200", "--satellites", "7",
      "--time", "2026-08-16T14:15:16Z"
    ])
    guard case .location(let options) = invocation.command else {
      Issue.record("expected location command")
      return
    }
    #expect(options.latitude == 52.5)
    #expect(options.longitude == 13.4)
    #expect(options.altitude == 34)
    #expect(options.accuracyMeters == 5)
    #expect(options.ageMilliseconds == 1200)
    #expect(options.satellites == 7)
    let fix = try options.makeFix()
    #expect(fix.positionValid)
    #expect(fix.timeValid)
    #expect(fix.altitudeValid)
    #expect(fix.latitude == 52.5)
    #expect(fix.longitude == 13.4)
    #expect(fix.accuracyMeters == 5)
    #expect(fix.ageMilliseconds == 1200)
    #expect(fix.year == 2026)
    #expect(fix.month == 8)
    #expect(fix.day == 16)
    #expect(fix.hour == 14)
    #expect(fix.minute == 15)
    #expect(fix.second == 16)
  }

  @Test func locationWithoutAltitudeUsesUnknownAltitude() throws {
    let options = CLILocationOptions(latitude: -33.5, longitude: 151.2, timestamp: Date(timeIntervalSince1970: 0))
    let fix = try options.makeFix()
    #expect(!fix.altitudeValid)
    #expect(fix.altitude == 0)
    #expect(fix.accuracyMeters == nil)
    #expect(fix.year == 1970)
    #expect(fix.centisecond == 0)
  }

  @Test func locationRejectsOutOfRangeValues() throws {
    #expect(throws: CLIUsageError.self) {
      try CLILocationOptions(latitude: 91, longitude: 0).makeFix()
    }
    #expect(throws: CLIUsageError.self) {
      try CLILocationOptions(latitude: 0, longitude: -181).makeFix()
    }
    #expect(throws: CLIUsageError.self) {
      try CLILocationOptions(latitude: 0, longitude: 0, accuracyMeters: 255).makeFix()
    }
    #expect(throws: CLIUsageError.self) {
      try CLILocationOptions(latitude: .infinity, longitude: 0).makeFix()
    }
  }

  @Test func triggerParsesTargetAndHold() throws {
    let invocation = try CLIArgumentParser.parse(["trigger", "focus", "--hold-ms", "750"])
    #expect(invocation.command == .trigger(CLITriggerOptions(focus: true, holdMilliseconds: 750)))
    let shutter = try CLIArgumentParser.parse(["trigger", "shutter"])
    #expect(shutter.command == .trigger(CLITriggerOptions(focus: false)))
  }

  @Test func passwordCommandsParse() throws {
    #expect(try CLIArgumentParser.parse(["password", "set"]).command == .password(.set))
    #expect(try CLIArgumentParser.parse(["password", "delete"]).command == .password(.delete))
    #expect(try CLIArgumentParser.parse(["password", "status"]).command == .password(.status))
  }

  @Test func monitorParsesIntervalDurationCsvAndHeader() throws {
    let invocation = try CLIArgumentParser.parse([
      "monitor", "--interval", "2.5", "--duration", "1800", "--until-empty",
      "--retry-window", "300", "--label", "GPS off", "--csv", "--no-header"
    ])
    #expect(invocation.command == .monitor(CLIMonitorOptions(
      interval: 2.5, duration: 1800, untilEmpty: true, csv: true,
      includeHeader: false, retryWindow: 300, label: "GPS off")))
    let defaults = try CLIArgumentParser.parse(["monitor"])
    #expect(defaults.command == .monitor(CLIMonitorOptions()))
  }

  @Test func monitorOptionsAreRejectedOnOtherCommands() {
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--interval", "5"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--csv"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["monitor", "--duration", "0"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["monitor", "--interval", "0"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--until-empty"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--label", "x"])
    }
  }

  @Test func parserRejectsAmbiguousOrMisplacedOptions() {
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--password", "one", "--password-env", "TWO"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--latitude", "1", "--longitude", "2"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["location", "--latitude", "1"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["trigger"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["password", "rotate"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--not-an-option"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["status", "--device"])
    }
  }
}

struct CLIPasswordResolverTests {
  @Test func defaultEnvironmentVariableIsUsedForAutomaticSource() throws {
    let resolution = try CLIPasswordResolver.resolve(
      .automatic,
      environment: [CLIPasswordResolver.defaultEnvironmentVariable: "secret"],
      readFile: { _ in "" },
      readStdin: { "" })
    #expect(resolution?.password == "secret")
    #expect(resolution?.origin == .environment)
  }

  @Test func automaticSourceWithoutEnvironmentReturnsNilForKeychainFallback() throws {
    let resolution = try CLIPasswordResolver.resolve(
      .automatic, environment: [:], readFile: { _ in "" }, readStdin: { "" })
    #expect(resolution == nil)
  }

  @Test func namedEnvironmentMustExistAndBeNonEmpty() {
    #expect(throws: CLIUsageError.self) {
      try CLIPasswordResolver.resolve(
        .environment("MISSING"), environment: [:], readFile: { _ in "" }, readStdin: { "" })
    }
    #expect(throws: CLIUsageError.self) {
      try CLIPasswordResolver.resolve(
        .environment("EMPTY"), environment: ["EMPTY": ""], readFile: { _ in "" },
        readStdin: { "" })
    }
  }

  @Test func fileAndStdinUseTheFirstLineOnly() throws {
    let fromFile = try CLIPasswordResolver.resolve(
      .file("/tmp/password"),
      environment: [:],
      readFile: { path in
        #expect(path == "/tmp/password")
        return "  spaced secret  \r\nignored\n"
      },
      readStdin: { "" })
    #expect(fromFile?.password == "  spaced secret  ")
    #expect(fromFile?.origin == .file)

    let fromStdin = try CLIPasswordResolver.resolve(
      .stdin, environment: [:], readFile: { _ in "" }, readStdin: { "secret\nignored\n" })
    #expect(fromStdin?.password == "secret")
    #expect(fromStdin?.origin == .stdin)
  }

  @Test func missingOrEmptyInputIsRejected() {
    #expect(throws: CLIUsageError.self) {
      try CLIPasswordResolver.resolve(
        .file("/tmp/empty"), environment: [:], readFile: { _ in "\n" }, readStdin: { "" })
    }
    #expect(throws: CLIUsageError.self) {
      try CLIPasswordResolver.resolve(
        .stdin, environment: [:], readFile: { _ in "" }, readStdin: { "" })
    }
  }

  @Test func firmwareByteLimitIsAppliedWithoutEchoingTheValue() {
    let tooLong = String(repeating: "é", count: 32)
    do {
      _ = try CLIPasswordResolver.resolve(
        .argument(tooLong), environment: [:], readFile: { _ in "" }, readStdin: { "" })
      Issue.record("expected the password to be rejected")
    } catch let error as CLIUsageError {
      #expect(!error.description.contains(tooLong))
      #expect(error.description.contains("63"))
    } catch {
      Issue.record("unexpected error: \(error)")
    }
  }

  @Test func firstLineKeepsSpacesAndDropsCRLF() {
    #expect(CLIPasswordResolver.firstLine(" secret \r\nignored") == " secret ")
    #expect(CLIPasswordResolver.firstLine("\n") == nil)
    #expect(CLIPasswordResolver.firstLine("") == nil)
  }
}

struct CLIMonitorPolicyTests {
  @Test func durationStopsOnlyWhenConfiguredAndReached() {
    #expect(!CLIMonitorPolicy.reachedDuration(elapsed: 3600, duration: nil))
    #expect(!CLIMonitorPolicy.reachedDuration(elapsed: 3599, duration: 3600))
    #expect(CLIMonitorPolicy.reachedDuration(elapsed: 3600, duration: 3600))
  }

  @Test func untilEmptyStopsAtOnePercentAndIgnoresUnknown() {
    #expect(!CLIMonitorPolicy.reachedEmpty(batteryPercent: 1, untilEmpty: false))
    #expect(!CLIMonitorPolicy.reachedEmpty(batteryPercent: 2, untilEmpty: true))
    #expect(CLIMonitorPolicy.reachedEmpty(batteryPercent: 1, untilEmpty: true))
    #expect(CLIMonitorPolicy.reachedEmpty(batteryPercent: 0, untilEmpty: true))
    #expect(!CLIMonitorPolicy.reachedEmpty(batteryPercent: 255, untilEmpty: true))
  }

  @Test func retryWindowIsBounded() {
    let last = Date(timeIntervalSince1970: 100)
    #expect(!CLIMonitorPolicy.retryExhausted(
      lastActivity: last, now: Date(timeIntervalSince1970: 279), retryWindow: 180))
    #expect(CLIMonitorPolicy.retryExhausted(
      lastActivity: last, now: Date(timeIntervalSince1970: 280), retryWindow: 180))
  }
}

struct CLIMonitorBackoffTests {
  @Test func backoffDoublesToTheCapAndResets() {
    var backoff = CLIMonitorBackoff(maximumDelay: 8)
    #expect(backoff.nextDelay() == 1)
    #expect(backoff.nextDelay() == 2)
    #expect(backoff.nextDelay() == 4)
    #expect(backoff.nextDelay() == 8)
    #expect(backoff.nextDelay() == 8)
    #expect(backoff.attempt == 5)
    backoff.reset()
    #expect(backoff.attempt == 0)
    #expect(backoff.delay == 1)
    #expect(backoff.nextDelay() == 1)
  }
}

struct CLIReportTests {
  private let device = FurbleDiscoveredDevice(
    id: UUID(uuidString: "11111111-2222-3333-4444-555555555555")!,
    name: "Furble-1A2B",
    rssi: -52)

  @Test func statusReportPrintsAuthenticationGpsAndCameras() throws {
    let status = try FurbleProtocol.decodeStatus(Data([
      1, 85, 0x18, 0x10, 0x88, 0xff, 3, 3, 1, 0,
      2, 0, 0, 0xff, 0xff, 0x78, 0, 0, 0, 0
    ]))
    let capability = try FurbleProtocol.decodeCapability(Data([1, 2, 3, 0, 0, 0]))
    let camera = try FurbleProtocol.decodeCameraRecord(
      Data([0, 3, 1, 9, 100, 0xf0, 2, 3, 0x46, 0x75, 0x6a]))
    let report = CLIStatusReport(
      device: device,
      status: status,
      capability: capability,
      authentication: .notRequired,
      passwordOrigin: .none,
      cameras: [camera],
      cameraListSupported: true)
    let text = CLIReport.status(report)
    #expect(text.contains("Device: Furble-1A2B"))
    #expect(text.contains("Authentication: authenticated; firmware reports no companion password is set"))
    #expect(text.contains("Password source: not supplied"))
    #expect(text.contains("Battery: 85%, 4120 mV, -120 mA, charging, external power"))
    #expect(text.contains("GPS: phone companion, 0 satellites"))
    #expect(text.contains("Features: settings-v2, cameras"))
    #expect(text.contains("id=3 name=\"Fuj\" type=Fujifilm flags=saved,connected state=connected"))
    #expect(!text.contains("secret"))
  }

  @Test func passwordAuthenticationAndUnsupportedCamerasAreReported() throws {
    let status = try FurbleProtocol.decodeStatus(Data([1] + [UInt8](repeating: 0, count: 19)))
    let capability = try FurbleProtocol.decodeCapability(Data([1, 2, 1, 0, 0, 0]))
    let report = CLIStatusReport(
      device: device,
      status: status,
      capability: capability,
      authentication: .password,
      passwordOrigin: .keychain,
      cameras: [],
      cameraListSupported: false)
    let text = CLIReport.status(report)
    #expect(text.contains("Authentication: authenticated with the companion password"))
    #expect(text.contains("Password source: login Keychain"))
    #expect(text.contains("Camera catalog: unsupported"))
    #expect(text.contains("Battery: 0%, 0 mV, 0 mA"))
  }

  @Test func cameraCatalogTimeoutSurvivesTheClearedPendingFlag() {
    #expect(CLICameraCatalogPolicy.timedOut(
      pending: true, phase: .ready))
    #expect(CLICameraCatalogPolicy.timedOut(
      pending: false, phase: .failed(.cameraTransactionTimedOut)))
    #expect(!CLICameraCatalogPolicy.timedOut(
      pending: false, phase: .failed(.linkLost)))
    #expect(!CLICameraCatalogPolicy.timedOut(
      pending: false, phase: .ready))
  }

  @Test func monitorCSVHeaderAndRowKeepPlotColumnsStable() throws {
    let status = try FurbleProtocol.decodeStatus(Data([
      1, 85, 0x18, 0x10, 0x88, 0xff, 3, 3, 1, 4,
      2, 3, 5, 0xff, 0xff, 0x78, 0, 0, 0, 0
    ]))
    #expect(CLIReport.monitorCSVHeader
      == "host_time_iso,host_elapsed_s,uptime_s,battery_percent,battery_mv,battery_ma,"
        + "gps_source,gps_satellites,control_state,rssi,label")
    let row = CLIReport.monitorCSVRow(
      timestamp: Date(timeIntervalSince1970: 1_700_000_000),
      elapsed: 12.5, status: status, rssi: -61, label: "GPS off")
    #expect(row
      == "2023-11-14T22:13:20Z,12.5,120,85,4120,-120,2,3,4,-61,GPS off")
    let human = CLIReport.monitorHumanRow(
      timestamp: Date(timeIntervalSince1970: 1_700_000_000),
      elapsed: 12.5, status: status, rssi: -61, label: "GPS off")
    #expect(human.contains("elapsed=12.5s"))
    #expect(human.contains("battery=85% 4120mV -120mA"))
    #expect(human.contains("gps=phone companion/3"))
    #expect(human.contains("external=1 charging=1"))
    #expect(human.contains("rssi=-61dBm label=GPS off"))
  }

  @Test func monitorMarkerRowsKeepTheSameColumnCount() {
    #expect(CLIReport.monitorMarkerRow(
      timestamp: Date(timeIntervalSince1970: 0), elapsed: 60, event: "link-lost")
      == "1970-01-01T00:00:00Z,60.0,,,,,,,,,link-lost")
    #expect(CLIReport.monitorMarkerRow(
      timestamp: Date(timeIntervalSince1970: 0), elapsed: 1, event: "run, phase \"A\"")
      == "1970-01-01T00:00:00Z,1.0,,,,,,,,,\"run, phase \"\"A\"\"\"")
  }

  @Test func monitorCSVDoesNotInventUnknownBatteryValues() throws {
    let status = try FurbleProtocol.decodeStatus(Data([
      1, 255, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 1, 0, 0, 0, 0
    ]))
    let row = CLIReport.monitorCSVRow(
      timestamp: Date(timeIntervalSince1970: 0),
      elapsed: 0, status: status, rssi: nil, label: "")
    #expect(row == "1970-01-01T00:00:00Z,0.0,1,255,0,0,0,0,0,,")
    #expect(CLIReport.monitorHumanRow(
      timestamp: Date(timeIntervalSince1970: 0),
      elapsed: 0, status: status, rssi: nil, label: "")
      .contains("battery=unknown"))
  }

  @Test func monitorSummaryReportsFirstAndLastBattery() throws {
    let first = try FurbleProtocol.decodeStatus(Data([
      1, 85, 0x18, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0
    ]))
    let last = try FurbleProtocol.decodeStatus(Data([
      1, 84, 0x04, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 0, 0, 0, 0
    ]))
    #expect(CLIReport.monitorSummary(
      samples: 2, first: first, last: last, elapsed: 60)
      == "samples=2 elapsed=60.0s battery=85%->84% 4120mV->4100mV")
  }

  @Test func monitorSummaryReportsUnknownBatteryPercent() throws {
    let status = try FurbleProtocol.decodeStatus(Data([
      1, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0
    ]))
    #expect(CLIReport.monitorSummary(
      samples: 1, first: status, last: status, elapsed: 60)
      == "samples=1 elapsed=60.0s battery=unknown->unknown 0mV->0mV")
  }

  @Test func monitorVoltageSentinelIsUnknownOutsideCSV() throws {
    let status = try FurbleProtocol.decodeStatus(Data([
      1, 85, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0
    ]))
    let human = CLIReport.monitorHumanRow(
      timestamp: Date(timeIntervalSince1970: 0),
      elapsed: 0, status: status, rssi: nil, label: "")
    #expect(human.contains("battery=85% unknown 0mA"))
    #expect(!human.contains("65535"))
    #expect(CLIReport.monitorSummary(
      samples: 1, first: status, last: status, elapsed: 60)
      == "samples=1 elapsed=60.0s battery=85%->85% unknown->unknown")
    #expect(CLIReport.monitorCSVRow(
      timestamp: Date(timeIntervalSince1970: 0),
      elapsed: 0, status: status, rssi: nil, label: "")
      .contains(",85,65535,"))

    let capability = try FurbleProtocol.decodeCapability(Data([1, 2, 3, 0, 0, 0]))
    let report = CLIStatusReport(
      device: device,
      status: status,
      capability: capability,
      authentication: .notRequired,
      passwordOrigin: .none,
      cameras: [],
      cameraListSupported: false)
    #expect(CLIReport.status(report).contains("Battery: 85%, unknown, 0 mA"))
  }

  @Test func cameraListFailuresAndScanLinesAreHumanReadable() throws {
    let status = try FurbleProtocol.decodeStatus(Data([1] + [UInt8](repeating: 0, count: 19)))
    let capability = try FurbleProtocol.decodeCapability(Data([1, 2, 3, 0, 0, 0]))
    let report = CLIStatusReport(
      device: device,
      status: status,
      capability: capability,
      authentication: .password,
      passwordOrigin: .environment,
      cameras: [],
      cameraListSupported: true,
      cameraListError: 2,
      cameraOperationError: nil)
    #expect(CLIReport.status(report).contains("Camera catalog: failed: request rejected (2)"))
    #expect(CLIReport.deviceLine(device).contains("rssi=-52 dBm"))
    #expect(CLIReport.scanHeader(count: 1) == "Discovered 1 furble companion device:")
    #expect(CLIReport.scanHeader(count: 2) == "Discovered 2 furble companion devices:")
  }
}
