import Darwin
import Foundation
import Testing
@testable import FurbleCompanionCLIKit
import FurbleCompanionCore

/// Scripted USB CDC port. It answers like the firmware console, stays silent
/// like a stalled endpoint, or prints one ROM banner like a chip that reset
/// into the bootloader. An RTS pulse with DTR low boots it back to the console.
private final class FakeSerialTransport: FurbleSerialTransport {
  enum Mode: Equatable {
    case console
    case stalled
    case rom(String)
  }

  struct Lines: Equatable {
    let dtr: Bool
    let rts: Bool
  }

  var mode: Mode
  /// The console's status reply body.
  var reply = "version: test\nreset: panic\nuptime: 40\n"
  /// When set, the RTS release fails with this errno, like a USB node that
  /// the reset already took away.
  var failRelease: Int32?
  var isOpen = false
  var opens = 0
  var lines: [Lines] = []
  private var pending: [UInt8] = []
  private var bannerSent = false

  init(mode: Mode) {
    self.mode = mode
  }

  func open() throws {
    if !isOpen { opens += 1 }
    isOpen = true
  }

  func close() {
    isOpen = false
    pending.removeAll()
  }

  func write(_ bytes: [UInt8]) throws {
    switch mode {
    case .console:
      let command = String(decoding: bytes, as: UTF8.self)
        .trimmingCharacters(in: .whitespacesAndNewlines)
      let body = command.isEmpty ? "" : "\(command)\n\(reply)"
      pending += Array((body + "furble> ").utf8)
    case .stalled:
      break
    case .rom(let banner):
      if !bannerSent {
        pending += Array(banner.utf8)
        bannerSent = true
      }
    }
  }

  func read(timeout: TimeInterval) throws -> [UInt8] {
    guard !pending.isEmpty else {
      Thread.sleep(forTimeInterval: min(timeout, 0.01))
      return []
    }
    // Split the reply like the CDC endpoint does, so the prompt can straddle reads.
    let count = min(7, pending.count)
    defer { pending.removeFirst(count) }
    return Array(pending[0..<count])
  }

  func setModemLines(dtr: Bool, rts: Bool) throws {
    let reset = lines.last.map { $0.rts && !$0.dtr } ?? false
    if reset && !rts, let code = failRelease {
      mode = .console
      bannerSent = false
      isOpen = false
      throw FurbleSerialError.modemControl(code)
    }
    lines.append(Lines(dtr: dtr, rts: rts))
    if reset && !rts && !dtr {
      mode = .console
      bannerSent = false
    }
  }
}

private let downloadBanner = """
  ESP-ROM:esp32s3-20210327
  Build:Mar 27 2021
  rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))
  Saved PC:0x40048ac6
  waiting for download

  """

struct ROMBannerTests {
  @Test func downloadBannerNamesTheResetAndTheBootMode() throws {
    let banner = try #require(FurbleROMBanner.parse(downloadBanner))
    #expect(banner.reason == "USB_UART_CHIP_RESET")
    #expect(banner.downloadMode)
    #expect(banner.resetLine.hasPrefix("rst:0x15"))
  }

  @Test func flashBootBannerIsNotDownloadMode() throws {
    let banner = try #require(FurbleROMBanner.parse(
      "ESP-ROM:esp32s3-20210327\nrst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\n"))
    #expect(banner.reason == "RTC_SW_CPU_RST")
    #expect(!banner.downloadMode)
  }

  @Test func partialBannerStillReportsDownloadMode() throws {
    let banner = try #require(FurbleROMBanner.parse("0x40048ac6\nwaiting for download\n"))
    #expect(banner.reason == nil)
    #expect(banner.downloadMode)
  }

  @Test func consoleTextIsNotABanner() {
    #expect(FurbleROMBanner.parse("version: dev\nreset: panic\n") == nil)
  }

  @Test func crlfBannerParses() throws {
    let banner = try #require(FurbleROMBanner.parse(
      "ESP-ROM:esp32s3-20210327\r\nrst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"))
    #expect(banner.reason == "RTC_SW_CPU_RST")
    #expect(banner.resetLine == "rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)")
    #expect(!banner.downloadMode)
  }

  @Test func noiseBeforeTheResetLineIsDropped() throws {
    let banner = try #require(FurbleROMBanner.parse(
      "\u{0}\u{FF}garbagerst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))\n"))
    #expect(banner.resetLine.hasPrefix("rst:0x15"))
    #expect(banner.reason == "USB_UART_CHIP_RESET")
    #expect(banner.downloadMode)
  }

  @Test func truncatedReasonIsUnknown() throws {
    let banner = try #require(FurbleROMBanner.parse("rst:0x15 (USB_UART_CHI"))
    #expect(banner.reason == nil)
    #expect(!banner.downloadMode)
  }

  @Test func theLastOfTwoBannersWins() throws {
    let banner = try #require(FurbleROMBanner.parse(
      downloadBanner + "rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\n"))
    #expect(banner.resetLine.contains("SPI_FAST_FLASH_BOOT"))
    #expect(!banner.downloadMode)
  }

  @Test func classicDownloadBootIsDownloadMode() throws {
    let banner = try #require(FurbleROMBanner.parse(
      "rst:0x1 (POWERON_RESET),boot:0x3 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))\n"))
    #expect(banner.reason == "POWERON_RESET")
    #expect(banner.downloadMode)
  }
}

/// The real POSIX read path on a pseudo-terminal whose master has closed.
/// XNU reports it as POLLIN|POLLHUP and read() returns 0, the state a
/// re-enumerated USB node leaves behind.
struct POSIXHangUpTests {
  @Test func hungUpTTYThrowsInsteadOfReadingNothing() throws {
    var master: Int32 = -1
    var slave: Int32 = -1
    var name = [CChar](repeating: 0, count: 128)
    try #require(openpty(&master, &slave, &name, nil, nil) == 0)
    let transport = FurblePOSIXSerialTransport(path: String(cString: name))
    try transport.open()
    defer { transport.close() }
    Darwin.close(slave)
    Darwin.close(master)
    #expect(throws: FurbleSerialError.readFailed("port hung up")) {
      try transport.read(timeout: 0.5)
    }
  }
}

struct USBRecoveryTests {
  @Test func consoleAnswersThroughTheFakeTransport() throws {
    let console = FurbleSerialConsole(
      path: "/dev/fake", transport: FakeSerialTransport(mode: .console))
    try console.open()
    let response = try console.command("status", timeout: 0.5)
    #expect(response.contains("version: test"))
  }

  @Test func silentEndpointIsAStall() throws {
    let console = FurbleSerialConsole(
      path: "/dev/fake", transport: FakeSerialTransport(mode: .stalled))
    try console.open()
    #expect(throws: FurbleSerialError.timeout) {
      try console.command("status", timeout: 0.1)
    }
  }

  @Test func romBannerReplacesTheTimeoutWithTheResetReason() throws {
    let console = FurbleSerialConsole(
      path: "/dev/fake", transport: FakeSerialTransport(mode: .rom(downloadBanner)))
    try console.open()
    do {
      _ = try console.command("status", timeout: 0.2)
      Issue.record("a ROM banner must not read as a console reply")
    } catch let error {
      let failure = CLIUSBRecovery.classify(error)
      guard case .rom(let banner) = failure else {
        Issue.record("expected a ROM failure, got \(failure)")
        return
      }
      #expect(banner.reason == "USB_UART_CHIP_RESET")
    }
  }

  @Test func hardResetIsEsptoolsRTSPulseWithDTRLow() throws {
    let transport = FakeSerialTransport(mode: .stalled)
    let console = FurbleSerialConsole(path: "/dev/fake", transport: transport)
    #expect(try console.hardReset(dwell: 0) == .pulsed)
    #expect(transport.lines == [.init(dtr: false, rts: true), .init(dtr: false, rts: false)])
  }

  @Test func aPortLostOnReleaseStillCountsAsAReset() throws {
    for code in [ENXIO, EIO, ENODEV] {
      let transport = FakeSerialTransport(mode: .stalled)
      transport.failRelease = code
      let console = FurbleSerialConsole(path: "/dev/fake", transport: transport)
      #expect(try console.hardReset(dwell: 0) == .pulsedPortLost)
    }
    let transport = FakeSerialTransport(mode: .stalled)
    transport.failRelease = EPERM
    let console = FurbleSerialConsole(path: "/dev/fake", transport: transport)
    #expect(throws: FurbleSerialError.modemControl(EPERM)) {
      try console.hardReset(dwell: 0)
    }
  }

  @Test func hardResetIsAnExplicitUSBMonitorOption() throws {
    let usb = try CLIArgumentParser.parse([
      "monitor", "--transport", "usb", "--port", "/dev/cu.usbmodem-test", "--usb-hard-reset"
    ])
    #expect(usb.command == .monitor(CLIMonitorOptions(allowHardReset: true)))
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse(["monitor", "--usb-hard-reset"])
    }
    #expect(throws: CLIUsageError.self) {
      try CLIArgumentParser.parse([
        "status", "--transport", "usb", "--port", "/dev/cu.usbmodem-test", "--usb-hard-reset"
      ])
    }
  }

  @Test func lostPortOnlyReopens() {
    var recovery = CLIUSBRecovery(allowHardReset: true)
    #expect(CLIUSBRecovery.classify(FurbleSerialError.cannotOpen("gone")) == .portLost)
    #expect(CLIUSBRecovery.classify(FurbleSerialError.readFailed("port hung up")) == .portLost)
    for _ in 0..<5 {
      #expect(recovery.next(after: .portLost) == .reopen)
    }
  }

  @Test func repeatedStallsEscalateOnlyWhenAllowed() {
    var allowed = CLIUSBRecovery(allowHardReset: true, stallLimit: 3)
    #expect(allowed.next(after: .stalled) == .reopen)
    #expect(allowed.next(after: .stalled) == .reopen)
    #expect(allowed.next(after: .stalled) == .hardReset)
    #expect(allowed.next(after: .stalled) == .reopen)

    var refused = CLIUSBRecovery(allowHardReset: false, stallLimit: 3)
    #expect(refused.next(after: .stalled) == .reopen)
    #expect(refused.next(after: .stalled) == .reopen)
    #expect(refused.next(after: .stalled) == .needsHardReset)

    var recovered = CLIUSBRecovery(allowHardReset: true, stallLimit: 3)
    _ = recovered.next(after: .stalled)
    _ = recovered.next(after: .stalled)
    recovered.recovered()
    #expect(recovered.next(after: .stalled) == .reopen)
  }

  @Test func onlyDownloadModeNeedsAReset() throws {
    let download = try #require(FurbleROMBanner.parse(downloadBanner))
    let flash = try #require(FurbleROMBanner.parse(
      "rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)"))
    var allowed = CLIUSBRecovery(allowHardReset: true)
    #expect(allowed.next(after: .rom(flash)) == .reopen)
    #expect(allowed.next(after: .rom(download)) == .hardReset)
    var refused = CLIUSBRecovery(allowHardReset: false)
    #expect(refused.next(after: .rom(download)) == .needsHardReset)
  }

  @Test func uptimeRegressionIsAReboot() {
    #expect(!CLIUSBRecovery.rebooted(previousUptime: nil, uptime: 0))
    #expect(!CLIUSBRecovery.rebooted(previousUptime: 40, uptime: 45))
    #expect(!CLIUSBRecovery.rebooted(previousUptime: 40, uptime: nil))
    #expect(CLIUSBRecovery.rebooted(previousUptime: 2_100, uptime: 3))
  }

}

/// The production monitor session, driven through the fake transport.
struct USBMonitorSessionTests {
  private func session(
    _ transport: FakeSerialTransport, allowHardReset: Bool
  ) -> (CLIUSBMonitorSession, () -> [String]) {
    final class Log { var events: [String] = [] }
    let log = Log()
    let console = FurbleSerialConsole(path: "/dev/fake", transport: transport)
    let session = CLIUSBMonitorSession(
      console: console, allowHardReset: allowHardReset,
      marker: { log.events.append($0) }, note: { _ in })
    session.commandTimeout = 0.05
    session.resetDwell = 0
    return (session, { log.events })
  }

  private func sampled(_ outcome: CLIUSBMonitorSession.Outcome) -> Bool {
    if case .sample = outcome { return true }
    return false
  }

  @Test func romDownloadModeIsReportedResetAndRecovered() {
    let transport = FakeSerialTransport(mode: .console)
    let (session, events) = session(transport, allowHardReset: true)
    #expect(sampled(session.step()))
    transport.mode = .rom(downloadBanner)
    #expect(!sampled(session.step()))
    // The port is closed after the pulse so the next sample reopens it.
    #expect(!transport.isOpen)
    #expect(sampled(session.step()))
    #expect(events() == ["link-lost", "rom-reset:USB_UART_CHIP_RESET", "hard-reset", "reconnected"])
    #expect(transport.opens == 3)
  }

  @Test func aResetThatDropsThePortStillEmitsHardReset() {
    let transport = FakeSerialTransport(mode: .rom(downloadBanner))
    transport.failRelease = ENXIO
    let (session, events) = session(transport, allowHardReset: true)
    #expect(!sampled(session.step()))
    #expect(!transport.isOpen)
    #expect(sampled(session.step()))
    #expect(events() == ["link-lost", "rom-reset:USB_UART_CHIP_RESET", "hard-reset", "reconnected"])
  }

  @Test func refusedResetIsAdvisedOncePerOutage() {
    let transport = FakeSerialTransport(mode: .stalled)
    let (session, events) = session(transport, allowHardReset: false)
    for _ in 0..<5 { _ = session.step() }
    #expect(transport.lines.isEmpty)
    #expect(events() == ["link-lost", "hard-reset-needed"])
    transport.mode = .console
    #expect(sampled(session.step()))
    transport.mode = .stalled
    for _ in 0..<3 { _ = session.step() }
    #expect(events() == [
      "link-lost", "hard-reset-needed", "reconnected", "link-lost", "hard-reset-needed"
    ])
  }

  @Test func uptimeRegressionMarksADeviceResetAndMissingUptimeDoesNot() {
    let transport = FakeSerialTransport(mode: .console)
    let (session, events) = session(transport, allowHardReset: false)
    #expect(sampled(session.step()))
    transport.reply = "version: test\nreset: panic\n"
    #expect(sampled(session.step()))
    #expect(events().isEmpty)
    transport.reply = "version: test\nreset: panic\nuptime: 3\n"
    #expect(sampled(session.step()))
    #expect(events() == ["device-reset:panic"])
  }
}
