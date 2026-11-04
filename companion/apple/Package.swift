// swift-tools-version: 6.0
import Foundation
import PackageDescription

// A Command Line Tools-only host ships Testing.framework but SwiftPM can omit
// the matching TestingMacros plugin when it builds a test target. Load the
// toolchain's own plugin explicitly in that case. A full Xcode install, which
// CI uses, keeps SwiftPM's normal plugin discovery.
func activeDeveloperDirectory() -> String {
  if let value = ProcessInfo.processInfo.environment["DEVELOPER_DIR"], !value.isEmpty {
    return value
  }
  let process = Process()
  process.executableURL = URL(fileURLWithPath: "/usr/bin/xcode-select")
  process.arguments = ["-p"]
  let pipe = Pipe()
  process.standardOutput = pipe
  do {
    try process.run()
    process.waitUntilExit()
  } catch {
    return ""
  }
  let data = pipe.fileHandleForReading.readDataToEndOfFile()
  return String(decoding: data, as: UTF8.self)
    .trimmingCharacters(in: .whitespacesAndNewlines)
}

let developerDirectory = activeDeveloperDirectory()
let testingPlugin = developerDirectory
  + "/usr/lib/swift/host/plugins/testing/libTestingMacros.dylib"
let testSwiftSettings: [SwiftSetting] =
  developerDirectory.contains("CommandLineTools")
    && FileManager.default.fileExists(atPath: testingPlugin)
  ? [.unsafeFlags(["-load-plugin-library", testingPlugin])]
  : []

let package = Package(
  name: "FurbleCompanion",
  platforms: [
    .iOS(.v16),
    .macOS(.v13)
  ],
  products: [
    .library(name: "FurbleCompanionCore", targets: ["FurbleCompanionCore"]),
    .library(name: "FurbleCompanionCLIKit", targets: ["FurbleCompanionCLIKit"]),
    .executable(name: "furble-companion-cli", targets: ["FurbleCompanionCLI"])
  ],
  targets: [
    .target(name: "FurbleCompanionCore"),
    .target(
      name: "FurbleCompanionCLIKit",
      dependencies: ["FurbleCompanionCore"]
    ),
    .executableTarget(
      name: "FurbleCompanionCLI",
      dependencies: ["FurbleCompanionCore", "FurbleCompanionCLIKit"]
    ),
    .testTarget(
      name: "FurbleCompanionCoreTests",
      dependencies: ["FurbleCompanionCore"],
      swiftSettings: testSwiftSettings
    ),
    .testTarget(
      name: "FurbleCompanionCLIKitTests",
      dependencies: ["FurbleCompanionCLIKit", "FurbleCompanionCore"],
      swiftSettings: testSwiftSettings
    )
  ],
  swiftLanguageModes: [.v5]
)
