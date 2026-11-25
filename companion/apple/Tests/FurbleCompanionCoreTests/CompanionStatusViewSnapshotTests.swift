#if os(macOS)
import AppKit
import Foundation
import ImageIO
import SwiftUI
import Testing
import UniformTypeIdentifiers
import FurbleCompanionCore

/// Pixel snapshot of the shipped Status section body. This is a macOS host test
/// on purpose: the committed baseline carries macOS font rasterisation and
/// window appearance, and the iOS test bundle compiles this file out instead of
/// comparing against a second renderer.
///
/// Regenerate the baseline after an intended visual change with
/// `FURBLE_APPLE_SNAPSHOT_UPDATE=1 swift test --filter CompanionStatusViewSnapshotTests`
/// and review the PNG before committing it.
struct CompanionStatusViewSnapshotTests {
  @Test @MainActor func statusViewMatchesCommittedBaseline() throws {
    let rendered = try StatusSnapshot.render()
    if let failure = try StatusSnapshot.verify(rendered) {
      Issue.record("\(failure)")
    }
  }
}

private struct SnapshotError: Error, CustomStringConvertible {
  let description: String
}

/// One 1x render of `CompanionStatusView` against one committed PNG. Everything
/// that could vary between machines is pinned: size, scale, locale, calendar,
/// time zone, layout direction, text size, colour scheme and AppKit appearance.
/// The view itself reads no clock and no live connection.
private enum StatusSnapshot {
  /// Wide enough for every status row, tall enough that no row wraps.
  static let size = CGSize(width: 360, height: 168)
  static let scale: CGFloat = 1
  /// Stands in for the inset a List row adds, so the render keeps the same
  /// breathing room as the app.
  static let padding: CGFloat = 16

  static let fileName = "CompanionStatusView.1x.png"
  static let updateVariable = "FURBLE_APPLE_SNAPSHOT_UPDATE"
  static let regenerateCommand =
    "FURBLE_APPLE_SNAPSHOT_UPDATE=1 swift test --filter CompanionStatusViewSnapshotTests"

  /// Colour noise allowance per channel out of 255. Text antialiasing between
  /// renderer versions stays inside this; a colour or weight change does not.
  static let maxChannelDelta = 8
  /// Share of pixels allowed to exceed `maxChannelDelta`, about 604 pixels of
  /// this canvas. Enough for glyph edge noise, far too small for reflowed text.
  static let maxDifferingPixelRatio = 0.01
  /// A real render of four text rows always covers more than this share of the
  /// canvas. A blank or offscreen render must never become a baseline.
  static let minimumInkRatio = 0.01
  /// Channel distance from the white background that counts as ink.
  static let inkThreshold = 24

  static var directory: URL {
    URL(fileURLWithPath: #filePath)
      .deletingLastPathComponent()
      .deletingLastPathComponent()
      .appendingPathComponent("Snapshots", isDirectory: true)
  }

  static var expectedURL: URL {
    directory.appendingPathComponent(fileName)
  }

  static var actualURL: URL {
    directory.appendingPathComponent("CompanionStatusView.1x.actual.png")
  }

  static var updateRequested: Bool {
    ProcessInfo.processInfo.environment[updateVariable] == "1"
  }

  /// Fixed firmware status. Every value is a literal, so no firmware, clock or
  /// device state can reach the render.
  static var status: FurbleProtocol.Status {
    FurbleProtocol.Status(
      version: 1,
      batteryPercent: 87,
      batteryMillivolts: 4012,
      batteryMilliamps: 0,
      powerFlags: 0,
      cameraTotal: 3,
      cameraConnected: 2,
      controlState: 0,
      gpsSource: 1,
      gpsSatellites: 9,
      intervalometerState: 0,
      intervalometerRemaining: 0,
      uptimeSeconds: 0)
  }

  @MainActor
  static func render() throws -> Data {
    let utc = try unwrap(TimeZone(secondsFromGMT: 0), "UTC is unavailable")
    let content = CompanionStatusView(status: status)
      .environment(\.locale, Locale(identifier: "en_US_POSIX"))
      .environment(\.calendar, Calendar(identifier: .gregorian))
      .environment(\.timeZone, utc)
      .environment(\.layoutDirection, .leftToRight)
      .environment(\.dynamicTypeSize, .large)
      .environment(\.colorScheme, .light)
      .padding(padding)
      .frame(width: size.width, height: size.height, alignment: .topLeading)
      .background(Color.white)

    let renderer = ImageRenderer(content: content)
    renderer.scale = scale
    renderer.proposedSize = ProposedViewSize(width: size.width, height: size.height)

    // Semantic colours such as .primary and .secondary resolve through the
    // current AppKit appearance. Pin the light one so a dark host cannot change
    // the baseline.
    let appearance = try unwrap(
      NSAppearance(named: .aqua), "NSAppearance(named: .aqua) is unavailable")
    var image: CGImage?
    appearance.performAsCurrentDrawingAppearance { image = renderer.cgImage }
    return try pngData(of: try unwrap(image, "ImageRenderer produced no image"))
  }

  /// Returns nil on success, or the message to fail with. Writes the actual
  /// image beside the baseline whenever the pixels differ.
  static func verify(_ rendered: Data) throws -> String? {
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

    let actual = try bitmap(of: rendered)
    if let blank = blankFailure(actual) {
      try rendered.write(to: actualURL)
      return blank
    }

    if updateRequested {
      if ProcessInfo.processInfo.environment["CI"]?.isEmpty == false {
        return """
          \(updateVariable) is set on CI. Refusing to rewrite \(expectedURL.path).
          Regenerate a committed baseline locally and review the PNG.
          """
      }
      try rendered.write(to: expectedURL)
      print("wrote snapshot baseline \(expectedURL.path)")
      return nil
    }

    guard FileManager.default.fileExists(atPath: expectedURL.path) else {
      try rendered.write(to: actualURL)
      return """
        Missing snapshot baseline \(expectedURL.path).
        The render was written to \(actualURL.path). Commit it after review with
        \(regenerateCommand)
        """
    }

    let expected = try bitmap(of: expectedURL)
    guard expected.width == actual.width, expected.height == actual.height else {
      try rendered.write(to: actualURL)
      return """
        CompanionStatusView size mismatch: baseline is \
        \(expected.width)x\(expected.height), render is \(actual.width)x\(actual.height).
        Actual render: \(actualURL.path)
        """
    }

    var differingPixels = 0
    var largestDelta = 0
    for index in stride(from: 0, to: actual.pixels.count, by: 4) {
      var differs = false
      for channel in 0..<4 {
        let delta = abs(Int(actual.pixels[index + channel]) - Int(expected.pixels[index + channel]))
        largestDelta = max(largestDelta, delta)
        if delta > maxChannelDelta { differs = true }
      }
      if differs { differingPixels += 1 }
    }

    let totalPixels = actual.width * actual.height
    let ratio = Double(differingPixels) / Double(totalPixels)
    if ratio <= maxDifferingPixelRatio {
      return nil
    }

    try rendered.write(to: actualURL)
    return """
      CompanionStatusView does not match its committed baseline.
      \(differingPixels) of \(totalPixels) pixels differ \
      (\(String(format: "%.2f", ratio * 100))%, budget \
      \(String(format: "%.2f", maxDifferingPixelRatio * 100))% at \
      \(maxChannelDelta)/255 per channel). Largest channel delta: \(largestDelta).
      Expected: \(expectedURL.path)
      Actual:   \(actualURL.path)
      If the change is intended, regenerate and review the PNG before committing:
      \(regenerateCommand)
      """
  }

  /// Guards against the failure that would make this test worthless: a renderer
  /// that returns an empty or offscreen image, whose blank pixels then get
  /// committed as the baseline it is compared against.
  static func blankFailure(_ bitmap: Bitmap) -> String? {
    var ink = 0
    for index in stride(from: 0, to: bitmap.pixels.count, by: 4) {
      var covered = false
      for channel in 0..<3 where bitmap.pixels[index + channel] < UInt8(255 - inkThreshold) {
        covered = true
      }
      if covered { ink += 1 }
    }
    let totalPixels = bitmap.width * bitmap.height
    let ratio = Double(ink) / Double(totalPixels)
    guard ratio >= minimumInkRatio else {
      return """
        CompanionStatusView rendered \(ink) of \(totalPixels) non-background pixels \
        (\(String(format: "%.2f", ratio * 100))%), under the \
        \(String(format: "%.2f", minimumInkRatio * 100))% floor.
        The render is blank or clipped. ImageRenderer produced \(bitmap.width)x\(bitmap.height).
        Actual render: \(actualURL.path)
        """
    }
    return nil
  }

  static func pngData(of image: CGImage) throws -> Data {
    let data = NSMutableData()
    guard
      let destination = CGImageDestinationCreateWithData(
        data as CFMutableData, UTType.png.identifier as CFString, 1, nil)
    else {
      throw SnapshotError(description: "cannot create a PNG destination")
    }
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else {
      throw SnapshotError(description: "cannot encode the PNG")
    }
    return data as Data
  }

  struct Bitmap {
    let width: Int
    let height: Int
    /// Straight 8-bit sRGB RGBA, four bytes per pixel.
    let pixels: [UInt8]
  }

  static func bitmap(of url: URL) throws -> Bitmap {
    let source = try unwrap(
      CGImageSourceCreateWithURL(url as CFURL, nil), "cannot open \(url.path)")
    let image = try unwrap(
      CGImageSourceCreateImageAtIndex(source, 0, nil), "cannot decode \(url.path)")
    return try bitmap(of: image)
  }

  static func bitmap(of data: Data) throws -> Bitmap {
    let source = try unwrap(
      CGImageSourceCreateWithData(data as CFData, nil), "cannot open the rendered PNG")
    let image = try unwrap(
      CGImageSourceCreateImageAtIndex(source, 0, nil), "cannot decode the rendered PNG")
    return try bitmap(of: image)
  }

  /// Decodes both sides through the same sRGB context, so the comparison is
  /// between pixels and not between PNG encoder details.
  static func bitmap(of image: CGImage) throws -> Bitmap {
    let width = image.width
    let height = image.height
    let bytesPerRow = width * 4
    let space = try unwrap(CGColorSpace(name: CGColorSpace.sRGB), "sRGB is unavailable")
    guard
      let context = CGContext(
        data: nil,
        width: width,
        height: height,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: space,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else {
      throw SnapshotError(description: "cannot create an sRGB bitmap context")
    }
    context.interpolationQuality = .none
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    guard let data = context.data else {
      throw SnapshotError(description: "the bitmap context has no storage")
    }
    let buffer = UnsafeBufferPointer(
      start: data.assumingMemoryBound(to: UInt8.self), count: bytesPerRow * height)
    return Bitmap(width: width, height: height, pixels: Array(buffer))
  }

  /// Throws with a readable message instead of trapping, so a partially set up
  /// host fails the test rather than crashing the whole run.
  static func unwrap<T>(_ value: T?, _ message: String) throws -> T {
    guard let value else { throw SnapshotError(description: message) }
    return value
  }
}
#endif
