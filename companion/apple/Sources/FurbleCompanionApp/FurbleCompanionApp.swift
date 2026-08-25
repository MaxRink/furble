#if canImport(SwiftUI) && canImport(CoreBluetooth)
import SwiftUI
import FurbleCompanionCore

@main
struct FurbleCompanionApp: App {
  @StateObject private var client: FurbleBLEClient
  private let credentialStore: KeychainCredentialStore

  init() {
    let accessGroup = Bundle.main.object(forInfoDictionaryKey: "FurbleKeychainAccessGroup") as? String
    let store = KeychainCredentialStore(accessGroup: accessGroup)
    credentialStore = store
    _client = StateObject(wrappedValue: FurbleBLEClient(credentialStore: store))
  }

  var body: some Scene {
    WindowGroup {
      ContentView(client: client, credentialStore: credentialStore)
        .onAppear { client.start() }
    }
  }
}

private struct ContentView: View {
  @ObservedObject var client: FurbleBLEClient
  let credentialStore: KeychainCredentialStore
  @StateObject private var location = FurbleLocationProvider()

  var body: some View {
    NavigationStack {
      List {
        Section("Companion password") {
          PasswordOnboardingView(
            credentialStore: credentialStore,
            onPasswordSaved: {
              client.stop()
              client.start()
            },
            onPasswordDeleted: {
              client.stop()
            })
        }
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
          HStack {
            Button("Press shutter") { try? client.trigger(.shutterPress) }
            Button("Release shutter") { try? client.trigger(.shutterRelease) }
          }
          HStack {
            Button("Press focus") { try? client.trigger(.focusPress) }
            Button("Release focus") { try? client.trigger(.focusRelease) }
          }
          Text("Release a held trigger before leaving the app. The firmware also releases held outputs when the link is lost.")
            .font(.footnote)
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
