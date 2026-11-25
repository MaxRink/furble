#if canImport(SwiftUI)
import Foundation
import SwiftUI

/// The Status section body of the companion app. It takes the frozen firmware
/// status record as a plain value, so the same view renders inside the app's
/// List and inside a snapshot test with no BLE client, Keychain, location
/// provider, date or clock involved.
///
/// Keep this view presentation-only. Anything that reads a clock, a
/// locale-dependent setting or a live connection belongs in the app, not in
/// the view that `CompanionStatusViewSnapshotTests` compares pixel by pixel.
public struct CompanionStatusView: View {
  public let status: FurbleProtocol.Status?

  public init(status: FurbleProtocol.Status?) {
    self.status = status
  }

  public var body: some View {
    if let status {
      LabeledContent(
        "Battery",
        value: status.batteryPercent == 255 ? "Unknown" : "\(status.batteryPercent)%")
      LabeledContent(
        "Voltage",
        value: status.batteryMillivolts == UInt16.max
          ? "Unknown" : "\(status.batteryMillivolts) mV")
      LabeledContent("Cameras", value: "\(status.cameraConnected)/\(status.cameraTotal)")
      LabeledContent("GPS", value: gpsStatusLabel(status))
    } else {
      Text("Waiting for an authenticated furble link")
    }
  }

  private func gpsStatusLabel(_ status: FurbleProtocol.Status) -> String {
    switch status.gpsSource {
    case 1:
      return "Receiver, \(status.gpsSatellites) sats"
    case 2:
      return "Phone"
    default:
      return "No fix"
    }
  }
}
#endif
