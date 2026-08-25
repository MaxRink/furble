package com.furble.companion.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FurbleGattContractTest {
    private data class FirmwareDeclaration(
        val properties: Int,
        val encrypted: Boolean,
        val authenticated: Boolean,
    )

    private data class FakeCharacteristic(val properties: Int)

    // Independent raw values from createGatt() in src/FurbleCompanion.cpp.
    private val firmware = mapOf(
        "location" to FirmwareDeclaration(0x0C, encrypted = true, authenticated = false),
        "status" to FirmwareDeclaration(0x12, encrypted = true, authenticated = false),
        "settings" to FirmwareDeclaration(0x28, encrypted = false, authenticated = true),
        "trigger" to FirmwareDeclaration(0x08, encrypted = false, authenticated = true),
        "capability" to FirmwareDeclaration(0x02, encrypted = true, authenticated = false),
    )

    @Test
    fun fakeFirmwareCharacteristicsMatchIndependentFirmwareDeclarations() {
        val app = mapOf(
            "location" to FurbleGattContract.LOCATION_PROPERTIES,
            "status" to FurbleGattContract.STATUS_PROPERTIES,
            "settings" to FurbleGattContract.SETTINGS_PROPERTIES,
            "trigger" to FurbleGattContract.TRIGGER_PROPERTIES,
            "capability" to FurbleGattContract.CAPABILITY_PROPERTIES,
        )
        app.forEach { (name, required) ->
            val fake = FakeCharacteristic(firmware.getValue(name).properties)
            assertTrue(FurbleGattContract.supports(fake.properties, required))
        }
        assertEquals(firmware.getValue("location").encrypted, FurbleGattContract.LOCATION_REQUIRES_ENCRYPTION)
        assertEquals(firmware.getValue("status").encrypted, FurbleGattContract.STATUS_REQUIRES_ENCRYPTION)
        assertEquals(firmware.getValue("settings").authenticated, FurbleGattContract.SETTINGS_REQUIRES_AUTHENTICATION)
        assertEquals(firmware.getValue("trigger").authenticated, FurbleGattContract.TRIGGER_REQUIRES_AUTHENTICATION)
        assertEquals(firmware.getValue("capability").encrypted, FurbleGattContract.CAPABILITY_REQUIRES_ENCRYPTION)
    }

    @Test
    fun fakeFirmwareCharacteristicsMissingARequiredPropertyAreRejected() {
        assertFalse(FurbleGattContract.supports(0x08, 0x0C))
        assertFalse(FurbleGattContract.supports(0x10, 0x12))
        assertFalse(FurbleGattContract.supports(0x08, 0x28))
    }
}
