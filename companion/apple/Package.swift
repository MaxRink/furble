// swift-tools-version: 6.0
import PackageDescription

let package = Package(
  name: "FurbleCompanion",
  platforms: [
    .iOS(.v16),
    .macOS(.v13)
  ],
  products: [
    .library(name: "FurbleCompanionCore", targets: ["FurbleCompanionCore"]),
    .executable(name: "FurbleCompanionApp", targets: ["FurbleCompanionApp"])
  ],
  targets: [
    .target(name: "FurbleCompanionCore"),
    .executableTarget(
      name: "FurbleCompanionApp",
      dependencies: ["FurbleCompanionCore"]
    ),
    .testTarget(
      name: "FurbleCompanionCoreTests",
      dependencies: ["FurbleCompanionCore"]
    )
  ]
)
