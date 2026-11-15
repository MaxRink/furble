import Darwin
import Foundation

public enum FurbleSerialError: Error, Equatable, CustomStringConvertible, Sendable {
  case cannotOpen(String)
  case cannotConfigure(String)
  case writeFailed(String)
  case readFailed(String)
  /// A DTR/RTS update failed with this errno.
  case modemControl(Int32)
  case timeout
  case notFurble
  /// The chip printed an ESP32 ROM boot banner instead of a console prompt.
  case romBoot(FurbleROMBanner)

  public var description: String {
    switch self {
    case .cannotOpen(let detail): return "cannot open serial port: \(detail)"
    case .cannotConfigure(let detail): return "cannot configure serial port: \(detail)"
    case .writeFailed(let detail): return "serial write failed: \(detail)"
    case .readFailed(let detail): return "serial read failed: \(detail)"
    case .modemControl(let code): return "modem control failed: \(String(cString: strerror(code)))"
    case .timeout: return "timed out waiting for the furble console prompt"
    case .notFurble: return "the serial port does not answer as a furble console"
    case .romBoot(let banner): return "the chip is in the ROM bootloader: \(banner.resetLine)"
    }
  }
}

/// The reset line the ESP32 ROM prints on every chip reset, for example
/// `rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))`.
///
/// The ROM writes it to the USB Serial/JTAG port as well as UART0, so a host
/// that still has the port open can read why the chip reset even though the
/// app never printed a panic.
public struct FurbleROMBanner: Equatable, Sendable {
  public let resetLine: String
  /// The ROM reset cause name, such as `RTC_SW_CPU_RST`, when readable.
  public let reason: String?
  /// True when the ROM chose download boot and the app will not start alone.
  public let downloadMode: Bool

  /// Parse the last banner in `text`. Only that banner's reset line and the
  /// text after it decide the boot mode, so an older banner earlier in the
  /// buffer never marks a later flash boot as download mode.
  public static func parse(_ text: String) -> FurbleROMBanner? {
    guard let start = text.range(of: "rst:0x", options: .backwards) else {
      // A partial read can miss the reset line but still hold the download
      // prompt. The cause is then unknown, not absent.
      guard text.contains("waiting for download") else { return nil }
      return FurbleROMBanner(resetLine: "waiting for download", reason: nil, downloadMode: true)
    }
    let banner = text[start.lowerBound...]
    let resetLine = String(banner.prefix(while: { !$0.isNewline }))
      .trimmingCharacters(in: .whitespaces)
    var reason: String?
    if let paren = resetLine.firstIndex(of: "("),
      let close = resetLine[paren...].firstIndex(of: ")") {
      reason = String(resetLine[resetLine.index(after: paren)..<close])
    }
    // S3 prints `boot:0x0 (DOWNLOAD(USB/UART0))`, the classic ESP32
    // `boot:0x3 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))`.
    let boot = resetLine.range(of: "boot:").map { resetLine[$0.upperBound...] } ?? ""
    let downloadMode = boot.contains("DOWNLOAD") || banner.contains("waiting for download")
    return FurbleROMBanner(resetLine: resetLine, reason: reason, downloadMode: downloadMode)
  }
}

/// Byte transport under the console. Production uses the POSIX serial device;
/// tests replay scripted bytes and record the modem line changes.
public protocol FurbleSerialTransport: AnyObject {
  var isOpen: Bool { get }
  func open() throws
  func close()
  func write(_ bytes: [UInt8]) throws
  /// Wait at most `timeout` for input. An empty result means nothing arrived.
  func read(timeout: TimeInterval) throws -> [UInt8]
  /// Drive DTR and RTS together, as one modem-control update.
  func setModemLines(dtr: Bool, rts: Bool) throws
}

/// POSIX serial transport for the StickS3 USB CDC port.
public final class FurblePOSIXSerialTransport: FurbleSerialTransport {
  public let path: String
  private var descriptor: Int32 = -1
  private let readChunk = 4096

  public init(path: String) {
    self.path = path
  }

  deinit {
    close()
  }

  public var isOpen: Bool { descriptor >= 0 }

  public func open() throws {
    guard descriptor < 0 else { return }
    descriptor = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
    guard descriptor >= 0 else {
      throw FurbleSerialError.cannotOpen(String(cString: strerror(errno)))
    }

    var options = termios()
    guard tcgetattr(descriptor, &options) == 0 else {
      let detail = String(cString: strerror(errno))
      close()
      throw FurbleSerialError.cannotConfigure(detail)
    }
    cfmakeraw(&options)
    options.c_cflag |= tcflag_t(CLOCAL | CREAD)
    options.c_cflag &= ~tcflag_t(CSIZE | PARENB | CSTOPB)
    options.c_cflag |= tcflag_t(CS8)
    options.c_cc.16 = 0  // VMIN
    options.c_cc.17 = 0  // VTIME
    guard cfsetispeed(&options, speed_t(B115200)) == 0,
      cfsetospeed(&options, speed_t(B115200)) == 0,
      tcsetattr(descriptor, TCSANOW, &options) == 0 else {
      let detail = String(cString: strerror(errno))
      close()
      throw FurbleSerialError.cannotConfigure(detail)
    }
  }

  public func close() {
    guard descriptor >= 0 else { return }
    Darwin.close(descriptor)
    descriptor = -1
  }

  public func write(_ bytes: [UInt8]) throws {
    let written = bytes.withUnsafeBytes { buffer -> Int in
      guard let base = buffer.baseAddress else { return -1 }
      return Darwin.write(descriptor, base, buffer.count)
    }
    guard written == bytes.count else {
      throw FurbleSerialError.writeFailed(String(cString: strerror(errno)))
    }
  }

  public func read(timeout: TimeInterval) throws -> [UInt8] {
    var poller = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
    let ready = Darwin.poll(&poller, 1, Int32(max(0, timeout * 1000)))
    if ready < 0 {
      throw FurbleSerialError.readFailed(String(cString: strerror(errno)))
    }
    if ready == 0 { return [] }
    var scratch = [UInt8](repeating: 0, count: readChunk)
    let count = scratch.withUnsafeMutableBytes { raw -> Int in
      guard let base = raw.baseAddress else { return -1 }
      return Darwin.read(descriptor, base, raw.count)
    }
    if count < 0 {
      if errno == EAGAIN || errno == EWOULDBLOCK { return [] }
      throw FurbleSerialError.readFailed(String(cString: strerror(errno)))
    }
    // A re-enumerated USB device leaves the old node hung up. XNU reports a
    // hung-up tty as POLLIN|POLLHUP and read() then returns 0, so a ready
    // poll with no bytes is the hang-up. Without this every read returns at
    // once until the command times out, which looks like a stall.
    if count == 0 {
      throw FurbleSerialError.readFailed("port hung up")
    }
    return Array(scratch[0..<count])
  }

  public func setModemLines(dtr: Bool, rts: Bool) throws {
    #if os(macOS)
      var bits: Int32 = 0
      guard ioctl(descriptor, TIOCMGET, &bits) == 0 else {
        throw FurbleSerialError.modemControl(errno)
      }
      bits = dtr ? (bits | TIOCM_DTR) : (bits & ~TIOCM_DTR)
      bits = rts ? (bits | TIOCM_RTS) : (bits & ~TIOCM_RTS)
      guard ioctl(descriptor, TIOCMSET, &bits) == 0 else {
        throw FurbleSerialError.modemControl(errno)
      }
    #else
      // Only the macOS CLI drives a USB CDC port.
      throw FurbleSerialError.cannotConfigure("modem lines are only driven on macOS")
    #endif
  }
}

/// Line-oriented client for the furble developer console.
///
/// The firmware console prints `furble> ` when a command completes. Keeping
/// this in Core lets the CLI and the native app use the same USB path without
/// duplicating termios setup.
public final class FurbleSerialConsole {
  public let path: String
  private let transport: FurbleSerialTransport

  public convenience init(path: String) {
    self.init(path: path, transport: FurblePOSIXSerialTransport(path: path))
  }

  public init(path: String, transport: FurbleSerialTransport) {
    self.path = path
    self.transport = transport
  }

  deinit {
    close()
  }

  public static func candidatePorts() -> [String] {
    guard let names = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else {
      return []
    }
    return names
      .filter {
        $0.hasPrefix("cu.usbmodem") || $0.hasPrefix("cu.usbserial")
          || $0.hasPrefix("cu.SLAB_USBtoUART")
      }
      .sorted()
      .map { "/dev/\($0)" }
  }

  public func open() throws {
    try transport.open()
  }

  public func close() {
    transport.close()
  }

  public enum HardResetResult: Equatable, Sendable {
    case pulsed
    /// The reset dropped the USB node before the release; the pulse happened.
    case pulsedPortLost
  }

  /// Reset the chip into the flash app with esptool's `--after hard_reset`
  /// pulse. For the S3 USB Serial/JTAG port esptool takes its non-OTG
  /// HardReset branch: RTS high for 100 ms, then low. DTR stays low, which
  /// selects flash boot instead of download mode. esptool also clears
  /// RTC_CNTL_FORCE_DOWNLOAD_BOOT over SLIP first; a bare pulse cannot.
  ///
  /// USB_UART_CHIP_RESET can take the USB node away during the dwell, so the
  /// release then fails with ENXIO, EIO or ENODEV. The reset has already
  /// happened, so that counts as done.
  @discardableResult
  public func hardReset(dwell: TimeInterval = 0.1) throws -> HardResetResult {
    try open()
    try transport.setModemLines(dtr: false, rts: true)
    Thread.sleep(forTimeInterval: dwell)
    do {
      try transport.setModemLines(dtr: false, rts: false)
    } catch FurbleSerialError.modemControl(let code) where [ENXIO, EIO, ENODEV].contains(code) {
      return .pulsedPortLost
    }
    return .pulsed
  }

  /// Send one console command and return the cleaned response before `furble>`.
  public func command(_ text: String, timeout: TimeInterval = 2) throws -> String {
    guard transport.isOpen else {
      throw FurbleSerialError.readFailed("port is not open")
    }
    // A bare newline consumes any prompt left by the previous command, so the
    // next prompt belongs to this command even when the CDC endpoint split it
    // across reads. Never wait longer for it than for the command itself.
    _ = try exchange("\n", timeout: min(1, timeout))
    let response = try exchange(text + "\n", timeout: timeout)
    if ProcessInfo.processInfo.environment["FURBLE_COMPANION_DEBUG"] != nil {
      FileHandle.standardError.write(Data("[usb] \(text) -> \(response)\n".utf8))
    }
    return response
  }

  private func exchange(_ payload: String, timeout: TimeInterval) throws -> String {
    try transport.write(Array(payload.utf8))

    let deadline = Date().addingTimeInterval(timeout)
    var buffer = Data()
    while Date() < deadline {
      let bytes = try transport.read(timeout: min(0.1, deadline.timeIntervalSinceNow))
      if bytes.isEmpty { continue }
      buffer.append(contentsOf: bytes)
      if let response = Self.extractResponse(buffer) {
        return response
      }
    }
    // No prompt. If the chip reset while the port was open, the ROM banner
    // names the cause; report it rather than a bare timeout.
    if let banner = FurbleROMBanner.parse(String(decoding: buffer, as: UTF8.self)) {
      throw FurbleSerialError.romBoot(banner)
    }
    throw FurbleSerialError.timeout
  }

  /// Probe a port by asking for the firmware version.
  @discardableResult
  public func probe(timeout: TimeInterval = 1) throws -> String {
    try open()
    defer { close() }
    let response = try command("version", timeout: timeout)
    guard response.contains("version:") else {
      throw FurbleSerialError.notFurble
    }
    return response
  }

  private static func extractResponse(_ data: Data) -> String? {
    guard let raw = String(data: data, encoding: .utf8) else { return nil }
    let cleaned = stripANSI(raw)
    guard let prompt = cleaned.range(of: "furble>", options: .backwards) else {
      return nil
    }
    return String(cleaned[..<prompt.lowerBound])
      .trimmingCharacters(in: .whitespacesAndNewlines)
  }

  private static func stripANSI(_ text: String) -> String {
    guard let regex = try? NSRegularExpression(pattern: "\u{001B}\\[[0-9;]*[A-Za-z]") else {
      return text
    }
    let range = NSRange(text.startIndex..<text.endIndex, in: text)
    return regex.stringByReplacingMatches(in: text, range: range, withTemplate: "")
  }
}
