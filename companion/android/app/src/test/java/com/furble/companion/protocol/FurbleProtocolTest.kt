package com.furble.companion.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FurbleProtocolTest {
    @Test
    fun locationRecordUsesTheFrozen42ByteLittleEndianLayout() {
        val fix = FurbleProtocol.LocationFix(
            positionValid = true,
            timeValid = true,
            altitudeValid = true,
            satellites = 7,
            accuracyMeters = 12,
            latitude = 12.25,
            longitude = -45.5,
            altitude = 123.75,
            year = 2026,
            month = 8,
            day = 16,
            hour = 14,
            minute = 15,
            second = 16,
            centisecond = 17,
            ageMs = 0x01020304,
        )

        val bytes = FurbleProtocol.encodeLocation(fix)

        assertEquals(42, bytes.size)
        assertEquals(1, bytes[0].toInt())
        assertEquals(0x07, bytes[1].toInt())
        assertEquals(7, bytes[2].toInt())
        assertEquals(12, bytes[3].toInt())
        val decoded = FurbleProtocol.decodeLocation(bytes)
        assertNotNull(decoded)
        assertEquals(fix, decoded)

        val ageOffset = 37
        assertArrayEquals(
            byteArrayOf(0x04, 0x03, 0x02, 0x01),
            bytes.copyOfRange(ageOffset, ageOffset + 4),
        )
        assertEquals(0xEA, bytes[28].toInt() and 0xFF)
        assertEquals(0x07, bytes[29].toInt() and 0xFF)
        assertEquals(8, bytes[30].toInt() and 0xFF)
    }

    @Test
    fun statusRecordReadsUnsignedAndSignedLittleEndianFields() {
        val bytes = ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN)
            .put(1)
            .put(85)
            .putShort(4120)
            .putShort((-120).toShort())
            .put(3)
            .put(2)
            .put(1)
            .put(4)
            .put(2)
            .put(9)
            .put(5)
            .putShort(0xFFFF.toShort())
            .putInt(0x01020304)
            .array()

        val status = FurbleProtocol.decodeStatus(bytes)

        assertNotNull(status)
        assertEquals(85, status?.batteryPercent)
        assertEquals(4120, status?.batteryMv)
        assertEquals(-120, status?.batteryMa)
        assertEquals(0xFFFF, status?.intervalometerRemaining)
        assertEquals(0x01020304L, status?.uptimeSeconds)
        assertTrue(status?.charging == true)
        assertTrue(status?.externalPower == true)
    }

    @Test
    fun settingsRequestsAndResponsesUseTheDocumentedTlv() {
        assertArrayEquals(byteArrayOf(0, 0, 0), FurbleProtocol.encodeSettingsListRequest())
        assertArrayEquals(
            byteArrayOf(2, 7, 1, 0xFF.toByte()),
            FurbleProtocol.encodeSettingsSet(7, byteArrayOf(0xFF.toByte())),
        )

        val response = FurbleProtocol.parseSettingsResponse(byteArrayOf(0, 7, 1, 1, 0xFE.toByte(), 1))

        assertNotNull(response)
        assertEquals(0, response?.status)
        assertEquals(7, response?.id)
        assertEquals(FurbleProtocol.SettingType.UINT8, response?.type)
        assertArrayEquals(byteArrayOf(0xFE.toByte()), response?.value)
        assertEquals(1, response?.flags)
    }

    @Test
    fun triggerHoldTimeIsLittleEndianAndUsesProtocolVersion() {
        assertArrayEquals(
            byteArrayOf(1, FurbleProtocol.TriggerOperation.TIMED_SHUTTER.toByte(), 0x2C, 0x01),
            FurbleProtocol.encodeTrigger(FurbleProtocol.TriggerOperation.TIMED_SHUTTER, 300),
        )
    }
}
