package com.furble.companion.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertThrows
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
        assertTrue(response?.isListRecord == true)

        val getResponse = FurbleProtocol.parseSettingsResponse(
            byteArrayOf(0, 6, FurbleProtocol.SettingType.UINT32.toByte(), 4, 3, 0, 0, 0),
        )
        assertNotNull(getResponse)
        assertArrayEquals(byteArrayOf(3, 0, 0, 0), getResponse?.value)
        assertFalse(getResponse?.isListRecord == true)

        val listRecord = FurbleProtocol.parseSettingsResponse(
            byteArrayOf(0, 4, FurbleProtocol.SettingType.UINT8.toByte(), 1, 0x01, 0x82.toByte()),
        )
        assertNotNull(listRecord)
        assertArrayEquals(byteArrayOf(0x01), listRecord?.value)
        assertEquals(0x82, listRecord?.flags)
        assertTrue(listRecord?.isListRecord == true)
        val dangerousRecord = FurbleProtocol.SettingRecord(
            id = listRecord!!.id,
            type = listRecord!!.type,
            value = listRecord!!.value,
            flags = listRecord!!.flags,
        )
        assertTrue(dangerousRecord.isDangerous)
    }

    @Test
    fun trailingFlagsEnabledBoolDecodesAsEnabledWithoutDangerousFlag() {
        // Canonical firmware list record for an enabled bool: status, id, type,
        // length, value, flags. GPS (wire id 5) is a plain bool with value 0x01
        // and no flags. The retired flags-before-length parse used to read the
        // length byte as flags and the value byte as length, decoding this as
        // Disabled with a spurious restart-required flag.
        val record = FurbleProtocol.parseSettingsResponse(
            byteArrayOf(0, 5, FurbleProtocol.SettingType.BOOL.toByte(), 1, 0x01, 0x00),
        )

        assertNotNull(record)
        assertEquals(5, record?.id)
        assertEquals(FurbleProtocol.SettingType.BOOL, record?.type)
        assertArrayEquals(byteArrayOf(0x01), record?.value)
        assertEquals(0, record?.flags)
        assertTrue(record?.isListRecord == true)

        val setting = FurbleProtocol.SettingRecord(
            id = record!!.id,
            type = record.type,
            value = record.value,
            flags = record.flags,
        )
        assertEquals("Enabled", setting.displayValue())
        assertFalse(setting.needsRestart)
        assertFalse(setting.isDangerous)
        assertTrue(setting.appliesImmediately)
    }

    @Test
    fun capabilityReadEnablesOnlySettingsV2() {
        val bytes = ByteBuffer.allocate(FurbleProtocol.CAPABILITY_PACKET_SIZE)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(1)
            .put(FurbleProtocol.SETTINGS_CAPABILITY_WIRE_VERSION.toByte())
            .putInt(1)
            .array()

        val capability = FurbleProtocol.parseCapability(bytes)

        assertNotNull(capability)
        assertTrue(capability?.supportsSettings == true)
        assertFalse(
            FurbleProtocol.CapabilitySnapshot(1, 1, 1).supportsSettings,
        )
        assertFalse(FurbleProtocol.CapabilitySnapshot(1, 1, 1).supportsCameras)
        assertTrue(FurbleProtocol.CapabilitySnapshot(1, 1, 3).supportsCameras)
    }

    @Test
    fun cameraRequestsAndRecordsUseTheFrozenLayout() {
        assertEquals(
            "b57f4f63-087b-4740-b71d-8262cf26ebbc",
            FurbleProtocol.CAMERAS_UUID.toString(),
        )
        assertArrayEquals(byteArrayOf(0, 0xFF.toByte()), FurbleProtocol.encodeCameraListRequest())
        assertArrayEquals(
            byteArrayOf(FurbleProtocol.CameraOperation.SELECT.toByte(), 7),
            FurbleProtocol.encodeCameraRequest(FurbleProtocol.CameraOperation.SELECT, 7),
        )
        val record = FurbleProtocol.parseCameraRecord(
            byteArrayOf(0, 7, 10, 0x0F, 75, 0x80.toByte(), FurbleProtocol.CameraState.CONNECTED.toByte(), 4) +
                "A\u00e9".toByteArray(),
        )
        assertNotNull(record)
        assertEquals(7, record?.cameraId)
        assertEquals(10, record?.cameraType)
        assertEquals(75, record?.progress)
        assertEquals(-128, record?.rssi)
        assertEquals("A\u00e9", record?.name)
        assertTrue(record?.isSaved == true)
        assertTrue(record?.isSelected == true)
        assertTrue(record?.isTarget == true)
        assertTrue(record?.isConnected == true)
        assertTrue(FurbleProtocol.parseCameraRecord(byteArrayOf(0, 0xFF.toByte(), 0, 0, 0, 0x80.toByte(), 0, 0))?.isTerminator == true)
    }

    @Test
    fun cameraRecordParserRejectsTruncationOversizeAndInvalidUtf8() {
        assertEquals(null, FurbleProtocol.parseCameraRecord(ByteArray(7)))
        assertEquals(null, FurbleProtocol.parseCameraRecord(byteArrayOf(0, 1, 1, 1, 1, 0, 0, 65)))
        val tooLong = ByteArray(8 + FurbleProtocol.CAMERA_NAME_MAX + 1)
        tooLong[1] = 1
        tooLong[7] = (FurbleProtocol.CAMERA_NAME_MAX + 1).toByte()
        assertEquals(null, FurbleProtocol.parseCameraRecord(tooLong))
        assertEquals(
            null,
            FurbleProtocol.parseCameraRecord(byteArrayOf(0, 1, 1, 1, 1, 0, 0, 1, 0xFF.toByte())),
        )
    }

    @Test
    fun intervalBlobUsesFourPackedLittleEndianParts() {
        val interval = FurbleProtocol.IntervalSetting(
            count = FurbleProtocol.IntervalPart(10, 0),
            delay = FurbleProtocol.IntervalPart(15, 3),
            shutter = FurbleProtocol.IntervalPart(30, 2),
            wait = FurbleProtocol.IntervalPart(0, 1),
        )

        val bytes = FurbleProtocol.encodeInterval(interval)

        assertEquals(12, bytes.size)
        assertArrayEquals(byteArrayOf(10, 0, 0, 15, 0, 3), bytes.copyOfRange(0, 6))
        assertEquals(interval, FurbleProtocol.decodeInterval(bytes))
    }

    @Test
    fun metadataCoversEveryCurrentWireIdAndUnknownRowsStayReadOnly() {
        assertEquals(45, FurbleSettingMetadata.byWireId.size)
        assertEquals((1..41).toSet() + setOf(44, 46, 72, 73), FurbleSettingMetadata.byWireId.keys)
        assertEquals("Brightness", FurbleSettingMetadata.byWireId[1]?.name)
        assertEquals(FurbleProtocol.SettingType.BLOB, FurbleSettingMetadata.byWireId[7]?.wireType)
        assertEquals(listOf("Dark", "Default", "Mono Furble"), FurbleSettingMetadata.byWireId[3]?.stringOptions)
        assertFalse(
            FurbleProtocol.SettingRecord(42, FurbleProtocol.SettingType.UINT8, byteArrayOf(4)).editable,
        )
        assertEquals(FurbleProtocol.SettingType.BOOL, FurbleSettingMetadata.byWireId[30]?.wireType)
        assertEquals(FurbleProtocol.SettingType.STRING, FurbleSettingMetadata.byWireId[27]?.wireType)
        assertEquals(FurbleProtocol.SettingType.UINT8, FurbleSettingMetadata.byWireId[41]?.wireType)
        val textSize = FurbleSettingMetadata.byWireId[40]
        assertEquals(FurbleProtocol.SettingType.UINT8, textSize?.wireType)
        assertEquals(listOf(0, 1, 2), textSize?.options?.map { it.value })
        assertFalse(textSize?.dangerous == true)
        assertTrue(FurbleProtocol.isSettingValueValid(40, FurbleProtocol.SettingType.UINT8, byteArrayOf(2)))
        assertFalse(FurbleProtocol.isSettingValueValid(40, FurbleProtocol.SettingType.UINT8, byteArrayOf(3)))
        assertEquals(FurbleProtocol.SettingType.BOOL, FurbleSettingMetadata.byWireId[46]?.wireType)
        assertEquals(FurbleProtocol.SettingType.UINT8, FurbleSettingMetadata.byWireId[72]?.wireType)
        assertEquals(listOf(0, 1, 2, 3), FurbleSettingMetadata.byWireId[72]?.options?.map { it.value })
        assertTrue(FurbleProtocol.isSettingValueValid(72, FurbleProtocol.SettingType.UINT8, byteArrayOf(3)))
        assertFalse(FurbleProtocol.isSettingValueValid(72, FurbleProtocol.SettingType.UINT8, byteArrayOf(4)))
        assertTrue(FurbleProtocol.isSettingValueValid(73, FurbleProtocol.SettingType.BOOL, byteArrayOf(1)))
    }

    @Test
    fun triggerHoldTimeIsLittleEndianAndUsesProtocolVersion() {
        assertArrayEquals(
            byteArrayOf(1, FurbleProtocol.TriggerOperation.TIMED_SHUTTER.toByte(), 0x2C, 0x01),
            FurbleProtocol.encodeTrigger(FurbleProtocol.TriggerOperation.TIMED_SHUTTER, 300),
        )
        assertArrayEquals(byteArrayOf(1, 0), FurbleProtocol.encodeTrigger(0))
        assertArrayEquals(byteArrayOf(1, 1), FurbleProtocol.encodeTrigger(1))
        assertArrayEquals(byteArrayOf(1, 2), FurbleProtocol.encodeTrigger(2))
        assertArrayEquals(byteArrayOf(1, 3), FurbleProtocol.encodeTrigger(3))
    }

    @Test
    fun firmwareGoldenRecordsRoundTripAcrossAllCompanionPaths() {
        val location = FurbleProtocol.LocationFix(
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
        assertArrayEquals(
            hex("0107070c00000000008028400000000000c046c00000000000f05e40ea0708100e0f1011000403020100"),
            FurbleProtocol.encodeLocation(location),
        )
        assertEquals(location, FurbleProtocol.decodeLocation(hex(
            "0107070c00000000008028400000000000c046c00000000000f05e40ea0708100e0f1011000403020100",
        )))

        val status = FurbleProtocol.decodeStatus(hex("0155181088ff03020104020905ffff0403020100"))
        assertNotNull(status)
        assertEquals(85, status?.batteryPercent)
        assertEquals(4120, status?.batteryMv)
        assertEquals(-120, status?.batteryMa)
        assertEquals(0x01020304L, status?.uptimeSeconds)

        assertArrayEquals(hex("000000"), FurbleProtocol.encodeSettingsListRequest())
        assertArrayEquals(hex("010100"), FurbleProtocol.encodeSettingsGet(1))
        assertArrayEquals(hex("02010121"), FurbleProtocol.encodeSettingsSet(1, byteArrayOf(0x21)))
        assertEquals(
            FurbleProtocol.SettingType.UINT8,
            FurbleProtocol.parseSettingsResponse(hex("000101012101"))?.type,
        )
        assertTrue(FurbleProtocol.parseSettingsResponse(hex("000101012101"))?.isListRecord == true)
        assertFalse(FurbleProtocol.parseSettingsResponse(hex("0001010121"))?.isListRecord == true)
        assertTrue(FurbleProtocol.parseSettingsResponse(hex("00ff040000"))?.isTerminator == true)

        assertArrayEquals(hex("01042c01"), FurbleProtocol.encodeTrigger(4, 300))
        assertArrayEquals(hex("0100"), FurbleProtocol.encodeTrigger(0))
        val invalidFlags = ByteArray(FurbleProtocol.LOCATION_PACKET_SIZE)
        invalidFlags[0] = 1
        invalidFlags[1] = 0x80.toByte()
        assertEquals(null, FurbleProtocol.decodeLocation(invalidFlags))
    }

    @Test
    fun settingsParserRejectsTrailingBytesThatFirmwareDoesNotEmit() {
        assertEquals(null, FurbleProtocol.parseSettingsResponse(hex("0001010121aabb")))
        assertEquals(null, FurbleProtocol.parseSettingsResponse(hex("00ff04000001")))
    }

    @Test
    fun authUsesFramedFirmwareHandshakeAndWire47() {
        assertEquals("b57f4f6f-087b-4740-b71d-8262cf26ebbc", FurbleProtocol.AUTH_UUID.toString())
        assertEquals(47, FurbleProtocol.COMPANION_PASSWORD_WIRE_ID)
        assertArrayEquals(byteArrayOf(1, 0), FurbleProtocol.encodeAuthBegin())
        val nonce = ByteArray(FurbleProtocol.AUTH_NONCE_SIZE) { it.toByte() }
        assertArrayEquals(nonce, FurbleProtocol.decodeAuthChallenge(byteArrayOf(1, 0) + nonce))
        val proof = FurbleProtocol.encodeAuthResponse("correct horse battery staple".toByteArray(), nonce)
        assertEquals(FurbleProtocol.AUTH_PROOF_PACKET_SIZE, proof.size)
        assertEquals(1, proof[0].toInt())
        assertEquals(1, proof[1].toInt())
        assertArrayEquals(
            byteArrayOf(
                1, 1, 0xc5.toByte(), 0xdf.toByte(), 0xbf.toByte(), 0x65,
                0x5b, 0xbc.toByte(), 0xd0.toByte(), 0x90.toByte(), 0xec.toByte(),
                0xb1.toByte(), 0xa5.toByte(), 0xbf.toByte(), 0x71, 0x68,
                0xb8.toByte(), 0x43,
            ),
            proof,
        )
        assertEquals(FurbleProtocol.AUTH_RESULT_NOT_REQUIRED, FurbleProtocol.decodeAuthResult(byteArrayOf(1, 2, 4)))
    }

    @Test
    fun authRejectsUnframedOrMalformedPackets() {
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.decodeAuthChallenge(ByteArray(16))
        }
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.decodeAuthResult(byteArrayOf(1))
        }
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.decodeAuthResult(byteArrayOf(1, 1, 1))
        }
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.decodeAuthResult(byteArrayOf(1, 2, 1, 0))
        }
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.encodeAuthResponse(byteArrayOf(), ByteArray(16))
        }
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.encodeAuthResponse("x".repeat(64).toByteArray(), ByteArray(16))
        }
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.encodeAuthResponse("é".repeat(32).toByteArray(), ByteArray(16))
        }
        assertThrows(IllegalArgumentException::class.java) {
            FurbleProtocol.encodeAuthResponse("pw".toByteArray(), ByteArray(15))
        }
    }

    @Test
    fun hmacMatchesFirmwareSha256GoldenVector() {
        assertArrayEquals(
            byteArrayOf(
                0xf7.toByte(), 0xbc.toByte(), 0x83.toByte(), 0xf4.toByte(),
                0x30, 0x53, 0x84.toByte(), 0x24, 0xb1.toByte(), 0x32,
                0x98.toByte(), 0xe6.toByte(), 0xaa.toByte(), 0x6f, 0xb1.toByte(), 0x43,
                0xef.toByte(), 0x4d, 0x59, 0xa1.toByte(), 0x49, 0x46,
                0x17, 0x59, 0x97.toByte(), 0x47, 0x9d.toByte(), 0xbc.toByte(),
                0x2d, 0x1a, 0x3c, 0xd8.toByte(),
            ),
            FurbleProtocol.hmacSha256(
                "key".toByteArray(),
                "The quick brown fox jumps over the lazy dog".toByteArray(),
            ),
        )
    }

    @Test
    fun utf8PasswordInputStopsAtCompleteCodePointBoundary() {
        val value = "é".repeat(31) + "a" + "🙂"
        val truncated = FurbleProtocol.truncateUtf8(value)
        assertEquals(63, truncated.toByteArray(Charsets.UTF_8).size)
        assertEquals("é".repeat(31) + "a", truncated)
    }

    private fun hex(value: String): ByteArray = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
