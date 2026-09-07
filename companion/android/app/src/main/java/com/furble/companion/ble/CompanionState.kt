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
    val cameraCharacteristicAvailable: Boolean = false,
    val camerasSupported: Boolean = false,
    val cameras: List<FurbleProtocol.CameraRecord> = emptyList(),
    val camerasLoading: Boolean = false,
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

internal enum class CameraRecordDisposition { IGNORED, PENDING, UPDATED, SNAPSHOT, REJECTED }

internal class CameraCatalog {
    private val pending = mutableMapOf<Int, FurbleProtocol.CameraRecord>()
    private var listing = false
    private var current = emptyList<FurbleProtocol.CameraRecord>()

    val records: List<FurbleProtocol.CameraRecord>
        get() = current

    fun beginList(): Boolean {
        if (listing) return false
        listing = true
        pending.clear()
        return true
    }

    fun accept(record: FurbleProtocol.CameraRecord): CameraRecordDisposition {
        if (record.status != FurbleProtocol.CameraStatus.OK) {
            listing = false
            pending.clear()
            return CameraRecordDisposition.REJECTED
        }
        if (record.isTerminator) {
            if (!listing) return CameraRecordDisposition.IGNORED
            current = pending.values.sortedBy { it.cameraId }
            listing = false
            pending.clear()
            return CameraRecordDisposition.SNAPSHOT
        }
        if (listing) {
            pending[record.cameraId] = record
            return CameraRecordDisposition.PENDING
        }
        if (record.cameraType == 0 && record.flags == 0 && record.progress == 0 &&
            record.rssi == FurbleProtocol.CAMERA_RSSI_UNKNOWN &&
                record.state == FurbleProtocol.CameraState.IDLE && record.name.isEmpty()
        ) return CameraRecordDisposition.IGNORED
        val existing = current.indexOfFirst { it.cameraId == record.cameraId }
        current = if (existing < 0) {
            (current + record).sortedBy { it.cameraId }
        } else {
            current.toMutableList().also { it[existing] = record }
        }
        return CameraRecordDisposition.UPDATED
    }

    fun fail() {
        listing = false
        pending.clear()
    }

    fun clear() {
        fail()
        current = emptyList()
    }
}
