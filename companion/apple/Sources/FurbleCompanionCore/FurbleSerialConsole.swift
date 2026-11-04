import Darwin
import Foundation

public enum FurbleSerialError: Error, Equatable, CustomStringConvertible, Sendable {
  case cannotOpen(String)
  case cannotConfigure(String)
  case writeFailed(String)
  case readFailed(String)
  case timeout
  case notFurble

  public var description: String {
    switch self {
    case .cannotOpen(let detail): return "cannot open serial port: \(detail)"
    case .cannotConfigure(let detail): return "cannot configure serial port: \(detail)"
    case .writeFailed(let detail): return "serial write failed: \(detail)"
    case .readFailed(let detail): return "serial read failed: \(detail)"
    case .timeout: return "timed out waiting for the furble console prompt"
    case .notFurble: return "the serial port does not answer as a furble console"
    }
  }
}

/// POSIX serial console transport for the StickS3 USB CDC port.
///
/// The firmware console is line-oriented and prints `furble> ` when a command
/// completes. Keeping this transport in Core lets the CLI and the native app
/// use the same USB path without duplicating termios setup.
public final class FurbleSerialConsole {
  public let path: String
  private var descriptor: Int32 = -1
  private let readChunk = 4096

  public init(path: String) {
    self.path = path
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
    guard descriptor < 0 else { return }
    descriptor = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
    guard descriptor >= 0 else {
      throw FurbleSerialError.cannotOpen(String(cString: strerror(errno)))
    }

    var options = termios()
    guard tcgetattr(descriptor, &options) == 0 else {
      let detail = String(cString: strerror(errno))
      Darwin.close(descriptor)
      descriptor = -1
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
      Darwin.close(descriptor)
      descriptor = -1
      throw FurbleSerialError.cannotConfigure(detail)
    }
  }

  public func close() {
    guard descriptor >= 0 else { return }
    Darwin.close(descriptor)
    descriptor = -1
  }

  /// Send one console command and return the cleaned response before `furble>`.
  public func command(_ text: String, timeout: TimeInterval = 2) throws -> String {
    guard descriptor >= 0 else {
      throw FurbleSerialError.readFailed("port is not open")
    }
    // A bare newline consumes any prompt left by the previous command, so the
    // next prompt belongs to this command even when the CDC endpoint split it
    // across reads.
    _ = try exchange("\n", timeout: 1)
    let response = try exchange(text + "\n", timeout: timeout)
    if ProcessInfo.processInfo.environment["FURBLE_COMPANION_DEBUG"] != nil {
      FileHandle.standardError.write(Data("[usb] \(text) -> \(response)\n".utf8))
    }
    return response
  }

  private func exchange(_ payload: String, timeout: TimeInterval) throws -> String {
    let bytes = Array(payload.utf8)
    let written = bytes.withUnsafeBytes { buffer -> Int in
      guard let base = buffer.baseAddress else { return -1 }
      return Darwin.write(descriptor, base, buffer.count)
    }
    guard written == bytes.count else {
      throw FurbleSerialError.writeFailed(String(cString: strerror(errno)))
    }

    let deadline = Date().addingTimeInterval(timeout)
    var buffer = Data()
    var scratch = [UInt8](repeating: 0, count: readChunk)
    while Date() < deadline {
      var poller = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
      let ready = Darwin.poll(&poller, 1, 100)
      if ready < 0 {
        throw FurbleSerialError.readFailed(String(cString: strerror(errno)))
      }
      if ready == 0 { continue }
      let count = scratch.withUnsafeMutableBytes { raw -> Int in
        guard let base = raw.baseAddress else { return -1 }
        return Darwin.read(descriptor, base, raw.count)
      }
      if count < 0 {
        if errno == EAGAIN || errno == EWOULDBLOCK { continue }
        throw FurbleSerialError.readFailed(String(cString: strerror(errno)))
      }
      if count == 0 { continue }
      buffer.append(contentsOf: scratch[0..<count])
      if let response = Self.extractResponse(buffer) {
        return response
      }
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

  private func drain() {
    var scratch = [UInt8](repeating: 0, count: readChunk)
    var quietSince = Date()
    // The USB CDC endpoint delivers the prompt in small chunks. Keep reading
    // until the line has been quiet for 30 ms so a leftover `furble>` cannot be
    // mistaken for the next command's response.
    while Date().timeIntervalSince(quietSince) < 0.03 {
      var poller = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
      let ready = Darwin.poll(&poller, 1, 10)
      if ready <= 0 { continue }
      let count = scratch.withUnsafeMutableBytes { raw -> Int in
        guard let base = raw.baseAddress else { return -1 }
        return Darwin.read(descriptor, base, raw.count)
      }
      if count > 0 {
        quietSince = Date()
      }
    }
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
