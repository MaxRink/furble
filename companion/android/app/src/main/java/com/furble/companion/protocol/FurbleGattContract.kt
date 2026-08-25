package com.furble.companion.protocol

/**
 * Pure representation of the Android BluetoothGatt property bits required by
 * the firmware characteristics. Keeping this independent of Android classes
 * makes the discovery contract testable with fake characteristics.
 */
object FurbleGattContract {
    const val PROPERTY_WRITE_NO_RESPONSE = 0x04
    const val PROPERTY_WRITE = 0x08
    const val PROPERTY_READ = 0x02
    const val PROPERTY_NOTIFY = 0x10
    const val PROPERTY_INDICATE = 0x20

    const val LOCATION_PROPERTIES = PROPERTY_WRITE or PROPERTY_WRITE_NO_RESPONSE
    const val STATUS_PROPERTIES = PROPERTY_READ or PROPERTY_NOTIFY
    const val SETTINGS_PROPERTIES = PROPERTY_WRITE or PROPERTY_INDICATE
    const val TRIGGER_PROPERTIES = PROPERTY_WRITE
    const val CAPABILITY_PROPERTIES = PROPERTY_READ

    // Firmware security declarations: *_ENC requires encryption and *_AUTHEN
    // requires an authenticated (MITM) link. These are checked by Android's
    // bonded GATT stack rather than represented in BluetoothGatt properties.
    const val LOCATION_REQUIRES_ENCRYPTION = true
    const val STATUS_REQUIRES_ENCRYPTION = true
    const val SETTINGS_REQUIRES_AUTHENTICATION = true
    const val TRIGGER_REQUIRES_AUTHENTICATION = true
    const val CAPABILITY_REQUIRES_ENCRYPTION = true

    fun supports(actualProperties: Int, requiredProperties: Int): Boolean =
        actualProperties and requiredProperties == requiredProperties
}
