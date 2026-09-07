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
        if client.state.supportsCameras {
          CamerasSection(client: client)
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
          HoldTriggerButton(
            title: "Hold shutter",
            onPress: { try? client.pressShutter() },
            onRelease: { try? client.releaseShutter() })
          HoldTriggerButton(
            title: "Hold focus",
            onPress: { try? client.pressFocus() },
            onRelease: { try? client.releaseFocus() })
          Text("Each hold sends one press and one matching release. Leaving the app releases both outputs. The firmware also releases held outputs when the link is lost.")
            .font(.footnote)
        }
      }
      .navigationTitle("furble companion")
      .onDisappear { try? client.releaseAllTriggers() }
    }
  }
}

private struct CamerasSection: View {
  @ObservedObject var client: FurbleBLEClient
  @State private var actionError: String?
  @State private var hasRequested = false

  private var operationBusy: Bool {
    client.cameras.contains { $0.state == 1 || $0.state == 3 || $0.state == 5 }
  }

  var body: some View {
    Section("Cameras") {
      if client.phase != .ready {
        Text("Available after an authenticated connection.")
          .font(.footnote)
      } else {
        HStack {
          Button("Refresh cameras") { refresh() }
          Spacer()
          Button("Connect selected") { perform { try client.connectCamera() } }
            .disabled(client.cameras.isEmpty || operationBusy)
          Button("Disconnect") { perform { try client.disconnectCameras() } }
            .disabled(client.cameras.isEmpty)
        }
        if let actionError {
          Text(actionError)
            .font(.footnote)
            .foregroundStyle(.red)
        }
        if client.cameras.isEmpty {
          Text(hasRequested ? "No saved cameras reported." : "Loading saved cameras...")
            .font(.footnote)
        } else {
          ForEach(client.cameras, id: \.cameraID) { camera in
            CameraRow(
              camera: camera,
              onConnect: { perform { try client.connectCamera(camera.cameraID) } },
              onSelectionChanged: { selected in
                perform { try client.setCamera(camera.cameraID, selected: selected) }
              })
          }
        }
      }
    }
    .onAppear { refreshIfReady() }
    .onChange(of: client.phase) { phase in
      if phase == .ready { refreshIfReady() }
    }
  }

  private func refreshIfReady() {
    guard client.phase == .ready, client.state.supportsCameras else { return }
    refresh()
  }

  private func refresh() {
    perform {
      try client.requestCameras()
      hasRequested = true
    }
  }

  private func perform(_ action: () throws -> Void) {
    do {
      try action()
      actionError = nil
    } catch {
      actionError = cameraErrorMessage(error)
    }
  }
}

private struct CameraRow: View {
  let camera: FurbleProtocol.CameraRecord
  let onConnect: () -> Void
  let onSelectionChanged: (Bool) -> Void

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      HStack {
        Image(systemName: "camera")
          .accessibilityHidden(true)
        VStack(alignment: .leading) {
          Text(camera.name.isEmpty ? "Camera \(camera.cameraID)" : camera.name)
            .font(.headline)
          Text(camera.typeLabel)
            .font(.caption)
            .foregroundStyle(.secondary)
        }
        Spacer()
        Button("Connect", action: onConnect)
          .disabled(camera.isConnected || camera.state == 1 || camera.state == 3 || camera.state == 5)
      }
      HStack {
        Text(camera.connectionStateLabel)
        if camera.isConnected, camera.rssi != -128 {
          Text("\(camera.rssi) dBm")
        }
        Spacer()
        Toggle("Selected", isOn: Binding(
          get: { camera.isSelected },
          set: onSelectionChanged))
        .labelsHidden()
        .accessibilityLabel("Select \(camera.name)")
      }
      if let status = camera.operationStatusLabel {
        Text("Last request: \(status)")
          .font(.footnote)
          .foregroundStyle(.red)
      }
    }
    .padding(.vertical, 4)
  }
}

private func cameraErrorMessage(_ error: Error) -> String {
  guard let error = error as? FurbleProtocol.Error else {
    return "Camera request failed: \(error.localizedDescription)"
  }
  switch error {
  case .authenticationRequired:
    return "Authenticate before using camera controls."
  case .authenticationUnavailable:
    return "Camera controls are unavailable on this firmware."
  case .payloadTooLarge:
    return "The camera request is too large for this BLE link."
  case .malformed:
    return "The camera request could not be encoded."
  case .unsupportedVersion(let version):
    return "Unsupported camera protocol version \(version)."
  case .invalidValue(let message):
    return "Invalid camera request: \(message)."
  case .authenticationFailed:
    return "Authentication failed; camera controls are unavailable."
  }
}

/// A platform-neutral SwiftUI gesture wrapper. DragGesture with zero minimum
/// distance reports the initial touch and the terminal release on both iOS and
/// macOS, unlike a tap which cannot represent a held focus or shutter output.
private struct HoldTriggerButton: View {
  let title: String
  let onPress: () -> Void
  let onRelease: () -> Void
  @State private var isPressed = false

  var body: some View {
    Text(title)
      .frame(maxWidth: .infinity)
      .padding(.vertical, 8)
      .contentShape(Rectangle())
      .background(isPressed ? Color.accentColor.opacity(0.25) : Color.secondary.opacity(0.12))
      .clipShape(RoundedRectangle(cornerRadius: 8))
      .accessibilityAddTraits(.isButton)
      .gesture(
        DragGesture(minimumDistance: 0)
          .onChanged { _ in
            guard !isPressed else { return }
            isPressed = true
            onPress()
          }
          .onEnded { _ in
            releaseIfNeeded()
          })
      .onDisappear { releaseIfNeeded() }
  }

  private func releaseIfNeeded() {
    guard isPressed else { return }
    isPressed = false
    onRelease()
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
