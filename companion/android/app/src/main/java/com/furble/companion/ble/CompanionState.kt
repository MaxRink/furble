package com.furble.companion.ble

import android.content.IntentSender
import com.furble.companion.protocol.FurbleProtocol

enum class ConnectionState {
    NO_ASSOCIATION,
    ASSOCIATED,
    OUT_OF_RANGE,
    BONDING,
    CONNECTING,
    DISCOVERING,
    READY,
    PERMISSION_DENIED,
    ERROR,
}

enum class AuthState {
    UNKNOWN,
    AUTHENTICATING,
    AUTHENTICATED,
    NOT_REQUIRED,
    REJECTED,
    DROPPED,
}

data class AssociationState(
    val associated: Boolean = false,
    val address: String? = null,
    val deviceName: String? = null,
    val present: Boolean = false,
    val bonded: Boolean = false,
)

data class CompanionUiState(
    val association: AssociationState = AssociationState(),
    val connection: ConnectionState = ConnectionState.NO_ASSOCIATION,
    val auth: AuthState = AuthState.UNKNOWN,
    val authSupported: Boolean = false,
    val storedPassword: Boolean = false,
    val status: FurbleProtocol.StatusSnapshot? = null,
    val capability: FurbleProtocol.CapabilitySnapshot? = null,
    val settingsSupported: Boolean = false,
    val settings: List<FurbleProtocol.SettingRecord> = emptyList(),
    val settingsLoading: Boolean = false,
    val pendingSettingConfirmation: PendingSettingConfirmation? = null,
    val locationEnabled: Boolean = false,
    val locationIntervalSeconds: Int = 10,
    val locationFixesSent: Long = 0,
    val shutterHeld: Boolean = false,
    val focusHeld: Boolean = false,
    val pairingInProgress: Boolean = false,
    val chooserIntentSender: IntentSender? = null,
    val error: String? = null,
)

data class PendingSettingConfirmation(
    val record: FurbleProtocol.SettingRecord,
    val value: ByteArray,
)

fun CompanionUiState.protectedReady(): Boolean = !authSupported ||
    auth == AuthState.AUTHENTICATED || auth == AuthState.NOT_REQUIRED
