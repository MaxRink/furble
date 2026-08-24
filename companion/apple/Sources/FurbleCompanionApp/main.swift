#if canImport(SwiftUI) && canImport(CoreBluetooth)
import SwiftUI
import FurbleCompanionCore

@main
struct FurbleCompanionApp: App {
  @StateObject private var client: FurbleBLEClient

  init() {
    let accessGroup = Bundle.main.object(forInfoDictionaryKey: "FurbleKeychainAccessGroup") as? String
    _client = StateObject(wrappedValue: FurbleBLEClient(
      credentialStore: KeychainCredentialStore(accessGroup: accessGroup)))
  }

  var body: some Scene {
    WindowGroup {
      ContentView(client: client)
        .onAppear { client.start() }
    }
  }
}

private struct ContentView: View {
  @ObservedObject var client: FurbleBLEClient
  @StateObject private var location = FurbleLocationProvider()

  var body: some View {
    NavigationStack {
      List {
        Section("Connection") {
          Label(client.phase.label, systemImage: client.phase.symbol)
          if let error = client.error { Text(error.localizedDescription).foregroundStyle(.red) }
          Button("Reconnect") { client.start() }
        }
        Section("Status") {
          if let status = client.status {
            LabeledContent("Battery", value: status.batteryPercent == 255 ? "Unknown" : "\(status.batteryPercent)%")
            LabeledContent("Voltage", value: "\(status.batteryMillivolts) mV")
            LabeledContent("Cameras", value: "\(status.cameraConnected)/\(status.cameraTotal)")
            LabeledContent("GPS", value: status.gpsSource == 0 ? "No fix" : "Available")
          } else {
            Text("Waiting for an authenticated furble link")
          }
        }
        Section("Phone GPS") {
          Toggle("Send location fixes", isOn: Binding(
            get: { location.enabled },
            set: { enabled in
              if enabled { location.start { client.writeLocation($0) } } else { location.stop() }
            }))
          Text("Location is sent only while enabled and the BLE link is authenticated.")
            .font(.footnote)
        }
        Section("Trigger") {
          Button("Shutter") { try? client.trigger(.shutterRelease) }
          Button("Focus") { try? client.trigger(.focusRelease) }
        }
      }
      .navigationTitle("furble companion")
    }
  }
}

private extension CompanionConnectionPhase {
  var label: String {
    switch self {
    case .idle: return "Idle"
    case .scanning: return "Scanning"
    case .connecting: return "Connecting"
    case .discovering: return "Discovering services"
    case .awaitingAuthentication: return "Authenticating"
    case .ready: return "Ready"
    case .reconnecting(let attempt): return "Reconnecting, attempt \(attempt)"
    case .failed(let error): return "Failed: \(error)"
    }
  }

  var symbol: String {
    switch self {
    case .ready: return "checkmark.circle"
    case .failed: return "xmark.circle"
    default: return "dot.radiowaves.left.and.right"
    }
  }
}
#else
import Foundation

@main
struct FurbleCompanionApp {
  static func main() {
    print("FurbleCompanionApp requires an iOS or macOS Xcode target with SwiftUI and CoreBluetooth.")
  }
}
#endif
