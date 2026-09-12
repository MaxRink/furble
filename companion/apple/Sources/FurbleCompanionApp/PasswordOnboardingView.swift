#if canImport(SwiftUI)
import SwiftUI
import FurbleCompanionCore

/// Native onboarding shared by the iOS and macOS targets. SecureField never
/// renders the value, and the view keeps only transient entry text. The
/// persisted value is read only to determine whether a credential exists.
struct PasswordOnboardingView: View {
  let credentialStore: KeychainCredentialStore
  let onPasswordSaved: () -> Void
  let onPasswordDeleted: () -> Void

  @State private var password = ""
  @State private var confirmation = ""
  @State private var form = PasswordOnboardingState()
  @State private var isEditing = false

  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      if form.hasStoredPassword && !isEditing {
        Label("Password saved securely", systemImage: "checkmark.shield")
          .accessibilityLabel("Companion password is saved securely")
        HStack {
          Button("Change password") { beginEditing() }
            .accessibilityHint("Replaces the saved companion password")
          Button("Delete password", role: .destructive) { deletePassword() }
            .accessibilityHint("Removes the saved companion password from this device")
        }
      } else {
        SecureField("Password", text: $password)
          .textContentType(.password)
          .accessibilityLabel("Companion password")
          .accessibilityHint("Up to 63 UTF-8 bytes")
        SecureField("Confirm password", text: $confirmation)
          .textContentType(.password)
          .accessibilityLabel("Confirm companion password")
        Text("The password is stored only in the device Keychain and is never shown or logged.")
          .font(.footnote)
          .foregroundStyle(.secondary)
        Text("Maximum \(FurblePasswordPolicy.maximumUTF8Bytes) UTF-8 bytes")
          .font(.footnote)
          .foregroundStyle(.secondary)
        if let message {
          Text(message)
            .foregroundStyle(.red)
            .accessibilityLabel(message)
        }
        HStack {
          Button("Save password") { savePassword() }
            .disabled(password.isEmpty || confirmation.isEmpty)
            .accessibilityHint("Stores the password in the Keychain and reconnects")
          if form.hasStoredPassword {
            Button("Cancel") { cancelEditing() }
              .accessibilityHint("Discards the new password")
          }
        }
      }
    }
    .task { loadState() }
  }

  private var message: String? {
    switch form.validation {
    case .none: return nil
    case .empty: return "Enter a password."
    case .tooLong:
      return "Password must be at most \(FurblePasswordPolicy.maximumUTF8Bytes) UTF-8 bytes."
    case .mismatch: return "The passwords do not match."
    case .storageUnavailable: return "Secure password storage is unavailable."
    case .saveFailed: return "The password could not be saved securely."
    }
  }

  private func loadState() {
    do {
      form.setStoredPassword(try credentialStore.readPassword() != nil)
    } catch {
      form.storageUnavailable()
    }
  }

  private func beginEditing() {
    password.removeAll(keepingCapacity: false)
    confirmation.removeAll(keepingCapacity: false)
    form.setStoredPassword(true)
    isEditing = true
  }

  private func cancelEditing() {
    clearEntry()
    isEditing = false
    form.setStoredPassword(true)
  }

  private func savePassword() {
    guard form.validate(password: password, confirmation: confirmation) else { return }
    do {
      try credentialStore.savePassword(password)
      clearEntry()
      isEditing = false
      form.didSave()
      onPasswordSaved()
    } catch {
      clearEntry()
      form.saveFailed()
    }
  }

  private func deletePassword() {
    do {
      try credentialStore.deletePassword()
      clearEntry()
      isEditing = false
      form.didDelete()
      onPasswordDeleted()
    } catch {
      form.storageUnavailable()
    }
  }

  private func clearEntry() {
    password.removeAll(keepingCapacity: false)
    confirmation.removeAll(keepingCapacity: false)
  }
}
#endif
