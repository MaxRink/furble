package com.furble.companion.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/** Wire contract copied from plans/50-companion-app-design.md. */
object FurbleProtocol {
    const val PROTOCOL_VERSION = 1
    const val CAPABILITY_VERSION = 1
    const val SETTINGS_CAPABILITY_WIRE_VERSION = 2

    // Wire sizes are fixed by the firmware structs in include/FurbleCompanion.h:
    // the location packet is 42 bytes and the status packet is 20 bytes.
    const val LOCATION_PACKET_SIZE = 42
    const val STATUS_PACKET_SIZE = 20
    const val TRIGGER_PACKET_SIZE = 4
    const val CAPABILITY_PACKET_SIZE = 6

    // The frozen firmware UUID base from include/FurbleCompanion.h. Only the
    // first 32-bit field changes per characteristic.
    val SERVICE_UUID: UUID = UUID.fromString("b57f4f5e-087b-4740-b71d-8262cf26ebbc")
    val LOCATION_UUID: UUID = UUID.fromString("b57f4f5f-087b-4740-b71d-8262cf26ebbc")
    val STATUS_UUID: UUID = UUID.fromString("b57f4f60-087b-4740-b71d-8262cf26ebbc")
    val SETTINGS_UUID: UUID = UUID.fromString("b57f4f61-087b-4740-b71d-8262cf26ebbc")
    val TRIGGER_UUID: UUID = UUID.fromString("b57f4f62-087b-4740-b71d-8262cf26ebbc")
    val CAPABILITY_UUID: UUID = UUID.fromString("b57f4f64-087b-4740-b71d-8262cf26ebbc")

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

    object CapabilityFeature {
        const val SETTINGS_V2 = 1 shl 0
    }

    object SettingFlag {
        const val NEEDS_RESTART = 1 shl 0
        const val DANGEROUS = 1 shl 1
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

    data class CapabilitySnapshot(
        val version: Int,
        val wireVersion: Int,
        val features: Long,
    ) {
        val supportsSettings: Boolean
            get() = version >= CAPABILITY_VERSION &&
                wireVersion >= SETTINGS_CAPABILITY_WIRE_VERSION &&
                features and CapabilityFeature.SETTINGS_V2.toLong() != 0L
    }

    data class SettingsResponse(
        val status: Int,
        val id: Int,
        val type: Int,
        val value: ByteArray,
        val flags: Int,
        val isListRecord: Boolean = false,
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
        val metadata: SettingMetadata?
            get() = FurbleSettingMetadata.byWireId[id]

        val name: String
            get() = metadata?.name ?: "Unknown setting $id"

        val editable: Boolean
            get() {
                val info = metadata ?: return false
                return info.wireType == type && info.editor != SettingEditorKind.READ_ONLY &&
                    hasValidValue()
            }

        val needsRestart: Boolean
            get() = flags and SettingFlag.NEEDS_RESTART != 0

        val appliesImmediately: Boolean
            get() = !needsRestart

        val isDangerous: Boolean
            get() = flags and SettingFlag.DANGEROUS != 0

        val dangerousConsequence: String?
            get() = when (id) {
                12 -> "Turning Companion off disconnects this app and stops furble advertising."
                4 -> "Lowering TX power can drop the Bluetooth link."
                20 -> "This changes connection timing while the device sleeps."
                17 -> "Changing CPU frequency can affect BLE timing under load."
                else -> null
            }

        fun displayValue(): String {
            val info = metadata
            if (info == null || info.wireType != type || !hasValidValue()) {
                return value.toHexString()
            }
            return when (type) {
                SettingType.BOOL -> if (value[0].u8() == 0) "Disabled" else "Enabled"
                SettingType.UINT8 -> {
                    val raw = value[0].u8()
                    info.options.firstOrNull { it.value == raw }?.label ?: raw.toString()
                }
                SettingType.UINT32 -> {
                    val raw = value.toUInt32()
                    info.options.firstOrNull { it.value.toLong() == raw }?.label ?: raw.toString()
                }
                SettingType.STRING -> value.toString(Charsets.UTF_8)
                SettingType.BLOB -> decodeInterval(value)?.displayValue() ?: value.toHexString()
                else -> value.toHexString()
            }
        }

        private fun hasValidValue(): Boolean = when (type) {
            SettingType.BOOL -> value.size == 1 && value[0].u8() <= 1
            SettingType.UINT8 -> value.size == 1
            SettingType.UINT32 -> value.size == 4
            SettingType.STRING -> true
            SettingType.BLOB -> id == 7 && decodeInterval(value) != null
            else -> false
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
        // The named fields occupy 19 bytes. Accept that prefix as well as the
        // declared 20-byte record so a firmware build that omits the
        // unspecified trailing byte still degrades to a useful status view.
        if (bytes.size < STATUS_PACKET_SIZE - 1) return null
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
        )
        // The named packed status fields sum to 19 bytes while the design
        // declares companion_status_t as 20 bytes.
        if (buffer.hasRemaining()) buffer.get()
        return snapshot
    }

    fun parseCapability(bytes: ByteArray): CapabilitySnapshot? {
        if (bytes.size < CAPABILITY_PACKET_SIZE) return null
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        return CapabilitySnapshot(
            version = buffer.get().u8(),
            wireVersion = buffer.get().u8(),
            features = buffer.int.toLong() and UINT32_MAX,
        )
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

    fun isSettingValueValid(id: Int, type: Int, value: ByteArray): Boolean {
        val info = FurbleSettingMetadata.byWireId[id] ?: return false
        if (info.wireType != type) return false
        return when (type) {
            SettingType.BOOL -> value.size == 1 && value[0].u8() <= 1
            SettingType.UINT8 -> {
                if (value.size != 1) {
                    false
                } else {
                    val raw = value[0].u8()
                    when (info.editor) {
                        SettingEditorKind.ENUM -> info.options.any { it.value == raw }
                        SettingEditorKind.RANGE -> info.range?.let { raw in it.min..it.max } == true
                        else -> true
                    }
                }
            }
            SettingType.UINT32 -> {
                val raw = decodeUint32(value) ?: return false
                info.options.any { it.value.toLong() == raw }
            }
            SettingType.STRING -> value.size <= 255 &&
                value.toString(Charsets.UTF_8) in info.stringOptions
            SettingType.BLOB -> id == 7 && decodeInterval(value) != null
            else -> false
        }
    }

    fun parseSettingsResponse(bytes: ByteArray): SettingsResponse? {
        if (bytes.size < 4) return null
        val status = bytes[0].u8()
        val id = bytes[1].u8()
        val type = bytes[2].u8()

        // Canonical firmware wire form: status, id, type, length, value, flags.
        // The flags byte trails the value on list records; a plain get or set
        // acknowledgement omits it. This is the primary parse, matching the
        // firmware fixtures in tests/protocol.
        val length = bytes[3].u8()
        if (length <= bytes.size - 4) {
            val value = bytes.copyOfRange(4, 4 + length)
            val hasTrailingFlags = id != 0xFF && bytes.size > 4 + length
            val flags = if (hasTrailingFlags) bytes[4 + length].u8() else 0
            return SettingsResponse(status, id, type, value, flags, hasTrailingFlags || id == 0xFF)
        }

        // Strict fallback for the retired flags-before-length prototype form
        // (status, id, type, flags, length, value). Only reached when the value
        // length cannot be read canonically, so the two forms never collide.
        if (id != 0xFF && bytes.size >= 6) {
            val flags = bytes[3].u8()
            val fallbackLength = bytes[4].u8()
            if (5 + fallbackLength == bytes.size && canonicalValueLength(id, type, fallbackLength)) {
                return SettingsResponse(
                    status = status,
                    id = id,
                    type = type,
                    value = bytes.copyOfRange(5, bytes.size),
                    flags = flags,
                    isListRecord = true,
                )
            }
        }

        return null
    }

    private fun canonicalValueLength(id: Int, type: Int, length: Int): Boolean {
        val info = FurbleSettingMetadata.byWireId[id]
        if (info == null || info.wireType != type) return true
        return when (type) {
            SettingType.BOOL, SettingType.UINT8 -> length == 1
            SettingType.UINT32 -> length == 4
            SettingType.STRING -> true
            SettingType.BLOB -> length == INTERVAL_PACKET_SIZE
            else -> false
        }
    }

    fun encodeUint32(value: Long): ByteArray {
        require(value in 0..UINT32_MAX) { "uint32 must fit in four bytes" }
        return ByteBuffer.allocate(4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putInt(value.toInt())
            .array()
    }

    fun decodeUint32(value: ByteArray): Long? = if (value.size == 4) value.toUInt32() else null

    data class IntervalPart(
        val value: Int,
        val unit: Int,
    ) {
        fun displayValue(): String = when {
            unit == INTERVAL_UNIT_INF -> "Infinite"
            unit == INTERVAL_UNIT_NIL -> value.toString()
            else -> "$value ${INTERVAL_UNIT_LABELS.getOrElse(unit) { "unit" }}"
        }
    }

    data class IntervalSetting(
        val count: IntervalPart,
        val delay: IntervalPart,
        val shutter: IntervalPart,
        val wait: IntervalPart,
    ) {
        fun displayValue(): String = listOf(
            "count ${count.displayValue()}",
            "delay ${delay.displayValue()}",
            "shutter ${shutter.displayValue()}",
            "wait ${wait.displayValue()}",
        ).joinToString(", ")
    }

    fun encodeInterval(interval: IntervalSetting): ByteArray {
        val parts = listOf(interval.count, interval.delay, interval.shutter, interval.wait)
        require(parts.all { it.value in 0..0xFFFF && it.unit in INTERVAL_UNIT_NIL..INTERVAL_UNIT_MIN }) {
            "interval values do not fit the firmware blob"
        }
        val buffer = ByteBuffer.allocate(INTERVAL_PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        parts.forEach { part ->
            buffer.putShort(part.value.toShort())
            buffer.put(part.unit.toByte())
        }
        return buffer.array()
    }

    fun decodeInterval(bytes: ByteArray): IntervalSetting? {
        if (bytes.size != INTERVAL_PACKET_SIZE) return null
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val parts = (0 until 4).map {
            IntervalPart(buffer.short.toInt() and 0xFFFF, buffer.get().u8())
        }
        if (parts.any { it.unit !in INTERVAL_UNIT_NIL..INTERVAL_UNIT_MIN }) return null
        return IntervalSetting(parts[0], parts[1], parts[2], parts[3])
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

    private const val UINT32_MAX = 0xFFFF_FFFFL
}

private fun Byte.u8(): Int = toInt() and 0xFF

private fun ByteArray.toUInt32(): Long =
    ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFF_FFFFL

private const val INTERVAL_PACKET_SIZE = 12
private const val INTERVAL_UNIT_NIL = 0
private const val INTERVAL_UNIT_INF = 1
private const val INTERVAL_UNIT_MS = 2
private const val INTERVAL_UNIT_SEC = 3
private const val INTERVAL_UNIT_MIN = 4
private val INTERVAL_UNIT_LABELS = listOf("", "", "ms", "sec", "min")

private fun ByteArray.toHexString(): String =
    joinToString(separator = "") { "%02x".format(it.toInt() and 0xFF) }
