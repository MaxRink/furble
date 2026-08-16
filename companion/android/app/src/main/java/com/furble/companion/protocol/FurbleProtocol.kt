package com.furble.companion.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/** Wire contract copied from plans/50-companion-app-design.md. */
object FurbleProtocol {
    const val PROTOCOL_VERSION = 1
    const val LOCATION_PACKET_SIZE = 42
    const val STATUS_PACKET_SIZE = 20
    const val TRIGGER_PACKET_SIZE = 4

    val SERVICE_UUID: UUID = UUID.fromString("00000001-6675-7262-6c65-e0d1c2b3a495")
    val LOCATION_UUID: UUID = UUID.fromString("00000002-6675-7262-6c65-e0d1c2b3a495")
    val STATUS_UUID: UUID = UUID.fromString("00000003-6675-7262-6c65-e0d1c2b3a495")
    val SETTINGS_UUID: UUID = UUID.fromString("00000004-6675-7262-6c65-e0d1c2b3a495")
    val TRIGGER_UUID: UUID = UUID.fromString("00000005-6675-7262-6c65-e0d1c2b3a495")

    const val LOCATION_VALID: Int = 1 shl 0
    const val TIME_VALID: Int = 1 shl 1
    const val ALTITUDE_VALID: Int = 1 shl 2

    object SettingsOperation {
        const val LIST = 0
        const val GET = 1
        const val SET = 2
    }

    object SettingsStatus {
        const val OK = 0
        const val UNKNOWN_ID = 1
        const val BAD_LENGTH = 2
        const val READ_ONLY = 3
        const val REJECTED = 4
    }

    object SettingType {
        const val BOOL = 0
        const val UINT8 = 1
        const val UINT32 = 2
        const val STRING = 3
        const val BLOB = 4
    }

    object TriggerOperation {
        const val SHUTTER_RELEASE = 0
        const val SHUTTER_PRESS = 1
        const val FOCUS_PRESS = 2
        const val FOCUS_RELEASE = 3
        const val TIMED_SHUTTER = 4
    }

    data class LocationFix(
        val positionValid: Boolean,
        val timeValid: Boolean,
        val altitudeValid: Boolean,
        val satellites: Int,
        val accuracyMeters: Int?,
        val latitude: Double,
        val longitude: Double,
        val altitude: Double,
        val year: Int,
        val month: Int,
        val day: Int,
        val hour: Int,
        val minute: Int,
        val second: Int,
        val centisecond: Int,
        val ageMs: Long,
    )

    data class StatusSnapshot(
        val version: Int,
        val batteryPercent: Int,
        val batteryMv: Int,
        val batteryMa: Int,
        val powerFlags: Int,
        val cameraTotal: Int,
        val cameraConnected: Int,
        val controlState: Int,
        val gpsSource: Int,
        val gpsSatellites: Int,
        val intervalometerState: Int,
        val intervalometerRemaining: Int,
        val uptimeSeconds: Long,
    ) {
        val charging: Boolean
            get() = powerFlags and 0x01 != 0

        val externalPower: Boolean
            get() = powerFlags and 0x02 != 0
    }

    data class SettingsResponse(
        val status: Int,
        val id: Int,
        val type: Int,
        val value: ByteArray,
        val flags: Int,
    ) {
        val isTerminator: Boolean
            get() = id == 0xFF
    }

    data class SettingRecord(
        val id: Int,
        val type: Int,
        val value: ByteArray,
        val flags: Int = 0,
    ) {
        val name: String
            get() = "Setting $id"

        val editable: Boolean
            get() = type == SettingType.BOOL || type == SettingType.UINT8

        val needsRestart: Boolean
            get() = flags and 0x01 != 0

        fun displayValue(): String = when (type) {
            SettingType.BOOL -> if (value.firstOrNull()?.toInt()?.and(0xFF) == 0) "false" else "true"
            SettingType.UINT8 -> value.firstOrNull()?.toInt()?.and(0xFF)?.toString() ?: "invalid"
            SettingType.UINT32 -> value.toHexString()
            SettingType.STRING -> value.toString(Charsets.UTF_8)
            SettingType.BLOB -> value.toHexString()
            else -> value.toHexString()
        }
    }

    fun encodeLocation(fix: LocationFix): ByteArray {
        val flags = (if (fix.positionValid) LOCATION_VALID else 0) or
            (if (fix.timeValid) TIME_VALID else 0) or
            (if (fix.altitudeValid) ALTITUDE_VALID else 0)
        val accuracy = fix.accuracyMeters?.coerceIn(0, 254) ?: 255
        val buffer = ByteBuffer.allocate(LOCATION_PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(PROTOCOL_VERSION.toByte())
        buffer.put(flags.toByte())
        buffer.put(fix.satellites.coerceIn(0, 255).toByte())
        buffer.put(accuracy.toByte())
        buffer.putDouble(fix.latitude)
        buffer.putDouble(fix.longitude)
        buffer.putDouble(fix.altitude)
        buffer.putShort(fix.year.coerceIn(0, 0xFFFF).toShort())
        buffer.put(fix.month.coerceIn(0, 255).toByte())
        buffer.put(fix.day.coerceIn(0, 255).toByte())
        buffer.put(fix.hour.coerceIn(0, 255).toByte())
        buffer.put(fix.minute.coerceIn(0, 255).toByte())
        buffer.put(fix.second.coerceIn(0, 255).toByte())
        buffer.put(fix.centisecond.coerceIn(0, 99).toByte())
        buffer.put(0)
        buffer.putInt(fix.ageMs.coerceIn(0, UINT32_MAX).toInt())
        // The packed field list sums to 41 bytes although the design declares
        // companion_fix_t as 42 bytes. Preserve the declared size with one
        // trailing zero compatibility byte without moving any named field.
        buffer.put(0)
        check(buffer.position() == LOCATION_PACKET_SIZE) {
            "Location packet layout changed: ${buffer.position()} bytes"
        }
        return buffer.array()
    }

    fun decodeLocation(bytes: ByteArray): LocationFix? {
        if (bytes.size < LOCATION_PACKET_SIZE) return null
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val version = buffer.get().u8()
        if (version < PROTOCOL_VERSION) return null
        val flags = buffer.get().u8()
        val satellites = buffer.get().u8()
        val accuracy = buffer.get().u8().let { if (it == 255) null else it }
        val latitude = buffer.getDouble()
        val longitude = buffer.getDouble()
        val altitude = buffer.getDouble()
        val year = buffer.short.toInt() and 0xFFFF
        val month = buffer.get().u8()
        val day = buffer.get().u8()
        val hour = buffer.get().u8()
        val minute = buffer.get().u8()
        val second = buffer.get().u8()
        val centisecond = buffer.get().u8()
        buffer.get()
        val ageMs = buffer.int.toLong() and UINT32_MAX
        buffer.get()
        return LocationFix(
            positionValid = flags and LOCATION_VALID != 0,
            timeValid = flags and TIME_VALID != 0,
            altitudeValid = flags and ALTITUDE_VALID != 0,
            satellites = satellites,
            accuracyMeters = accuracy,
            latitude = latitude,
            longitude = longitude,
            altitude = altitude,
            year = year,
            month = month,
            day = day,
            hour = hour,
            minute = minute,
            second = second,
            centisecond = centisecond,
            ageMs = ageMs,
        )
    }

    fun decodeStatus(bytes: ByteArray): StatusSnapshot? {
        if (bytes.size < STATUS_PACKET_SIZE) return null
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val snapshot = StatusSnapshot(
            version = buffer.get().u8(),
            batteryPercent = buffer.get().u8(),
            batteryMv = buffer.short.toInt() and 0xFFFF,
            batteryMa = buffer.short.toInt(),
            powerFlags = buffer.get().u8(),
            cameraTotal = buffer.get().u8(),
            cameraConnected = buffer.get().u8(),
            controlState = buffer.get().u8(),
            gpsSource = buffer.get().u8(),
            gpsSatellites = buffer.get().u8(),
            intervalometerState = buffer.get().u8(),
            intervalometerRemaining = buffer.short.toInt() and 0xFFFF,
            uptimeSeconds = buffer.int.toLong() and UINT32_MAX,
        }
        // The named packed status fields sum to 19 bytes while the design
        // declares companion_status_t as 20 bytes.
        buffer.get()
        return snapshot
    }

    fun encodeTrigger(operation: Int, holdMs: Int = 0): ByteArray {
        require(operation in TriggerOperation.SHUTTER_RELEASE..TriggerOperation.TIMED_SHUTTER) {
            "Unknown trigger operation: $operation"
        }
        require(holdMs in 0..0xFFFF) { "hold_ms must fit in uint16" }
        return ByteBuffer.allocate(TRIGGER_PACKET_SIZE)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(PROTOCOL_VERSION.toByte())
            .put(operation.toByte())
            .putShort(holdMs.toShort())
            .array()
    }

    fun encodeSettingsListRequest(): ByteArray = encodeSettingsRequest(SettingsOperation.LIST, 0, byteArrayOf())

    fun encodeSettingsGet(id: Int): ByteArray = encodeSettingsRequest(SettingsOperation.GET, id, byteArrayOf())

    fun encodeSettingsSet(id: Int, value: ByteArray): ByteArray =
        encodeSettingsRequest(SettingsOperation.SET, id, value)

    fun parseSettingsResponse(bytes: ByteArray): SettingsResponse? {
        if (bytes.size < 4) return null
        val status = bytes[0].u8()
        val id = bytes[1].u8()
        val type = bytes[2].u8()
        val length = bytes[3].u8()
        if (length > bytes.size - 4) return null
        val value = bytes.copyOfRange(4, 4 + length)
        // The design's response table has no flags field, but section 3.5
        // says list records gain one. Treat one trailing byte as that optional
        // list-record flags byte and ignore any future trailing fields.
        val flags = if (id != 0xFF && bytes.size > 4 + length) bytes[4 + length].u8() else 0
        return SettingsResponse(status, id, type, value, flags)
    }

    private fun encodeSettingsRequest(operation: Int, id: Int, value: ByteArray): ByteArray {
        require(operation in SettingsOperation.LIST..SettingsOperation.SET)
        require(id in 0..0xFF)
        require(value.size <= 0xFF)
        return ByteBuffer.allocate(3 + value.size)
            .put(operation.toByte())
            .put(id.toByte())
            .put(value.size.toByte())
            .put(value)
            .array()
    }

    private fun Byte.u8(): Int = toInt() and 0xFF

    private const val UINT32_MAX = 0xFFFF_FFFFL
}

private fun ByteArray.toHexString(): String =
    joinToString(separator = "") { "%02x".format(it.toInt() and 0xFF) }
