package com.furble.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.content.Context
import android.os.Handler
import android.os.Looper
import com.furble.companion.protocol.FurbleProtocol
import java.util.ArrayDeque
import java.util.UUID

/**
 * One event-driven GATT session. Every ATT operation waits for its callback
 * before the next response-bearing operation starts.
 */
@SuppressLint("MissingPermission")
class GattConnection(
    context: Context,
    private val device: BluetoothDevice,
    private val listener: Listener,
) {
    interface Listener {
        fun onDiscoveringServices()
        fun onReady()
        fun onStatus(snapshot: FurbleProtocol.StatusSnapshot)
        fun onCapabilities(capability: FurbleProtocol.CapabilitySnapshot?)
        fun onSettings(response: FurbleProtocol.SettingsResponse)
        fun onAuthAvailability(supported: Boolean)
        fun onAuthResult(result: Int)
        fun onDisconnected()
        fun onError(message: String)
    }

    private val appContext = context.applicationContext
    private val handler = Handler(Looper.getMainLooper())
    private val operations = ArrayDeque<Operation>()
    private val callback = Callback()

    private var gatt: BluetoothGatt? = null
    private var service: BluetoothGattService? = null
    private var locationCharacteristic: BluetoothGattCharacteristic? = null
    private var statusCharacteristic: BluetoothGattCharacteristic? = null
    private var settingsCharacteristic: BluetoothGattCharacteristic? = null
    private var triggerCharacteristic: BluetoothGattCharacteristic? = null
    private var authCharacteristic: BluetoothGattCharacteristic? = null
    private var capabilityCharacteristic: BluetoothGattCharacteristic? = null
    private var currentOperation: Operation? = null
    private var isReady = false
    private var mtu = 23
    private var authPassword: ByteArray? = null
    private var authChallengePending = false
    private var authGeneration = 0L
    private var authAttemptGeneration: Long? = null

    fun connect() {
        handler.post {
            if (gatt != null) return@post
            try {
                gatt = device.connectGatt(
                    appContext,
                    false,
                    callback,
                    BluetoothDevice.TRANSPORT_LE,
                )
                if (gatt == null) {
                    listener.onError("Android could not start the BLE connection")
                    notifyDisconnected()
                }
            } catch (securityException: SecurityException) {
                listener.onError("Bluetooth permission is required to connect")
                notifyDisconnected()
            }
        }
    }

    fun close() {
        handler.post { closeInternal(notify = true) }
    }

    fun cancelAuthentication() {
        handler.post {
            authGeneration++
            authAttemptGeneration = null
            operations.removeAll { it is Operation.WriteCharacteristic && it.characteristic.uuid == FurbleProtocol.AUTH_UUID }
            clearAuthSecrets()
        }
    }

    fun writeLocation(bytes: ByteArray) {
        handler.post {
            if (!isReady || bytes.size != FurbleProtocol.LOCATION_PACKET_SIZE) return@post
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.LOCATION_UUID,
                value = bytes,
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE,
                waitForCallback = false,
            )
        }
    }

    fun requestSettingsList() {
        handler.post {
            if (!isReady) return@post
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.SETTINGS_UUID,
                value = FurbleProtocol.encodeSettingsListRequest(),
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                waitForCallback = true,
            )
        }
    }

    fun setSetting(id: Int, value: ByteArray) {
        handler.post {
            if (!isReady) return@post
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.SETTINGS_UUID,
                value = FurbleProtocol.encodeSettingsSet(id, value),
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                waitForCallback = true,
            )
        }
    }

    fun sendTrigger(operation: Int, holdMs: Int = 0) {
        handler.post {
            if (!isReady) return@post
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.TRIGGER_UUID,
                value = FurbleProtocol.encodeTrigger(operation, holdMs),
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                waitForCallback = true,
            )
        }
    }

    /** Starts the firmware challenge. The password is held only until its HMAC is sent. */
    fun authenticate(passwordUtf8: ByteArray) {
        handler.post {
            if (!isReady) {
                listener.onError("furble is not ready for authentication")
                return@post
            }
            if (passwordUtf8.size !in 1..FurbleProtocol.COMPANION_PASSWORD_MAX) {
                listener.onError("Companion password must be 1..${FurbleProtocol.COMPANION_PASSWORD_MAX} UTF-8 bytes")
                return@post
            }
            if (authCharacteristic == null) {
                listener.onError("The furble does not expose its AUTH characteristic")
                return@post
            }
            authPassword?.fill(0)
            authPassword = passwordUtf8.copyOf()
            authChallengePending = true
            authAttemptGeneration = ++authGeneration
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.AUTH_UUID,
                value = FurbleProtocol.encodeAuthBegin(),
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                waitForCallback = true,
            )
        }
    }

    private fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
        if (status != BluetoothGatt.GATT_SUCCESS) {
            fail("Service discovery failed with status $status")
            return
        }
        service = gatt.getService(FurbleProtocol.SERVICE_UUID)
        locationCharacteristic = service?.getCharacteristic(FurbleProtocol.LOCATION_UUID)
        statusCharacteristic = service?.getCharacteristic(FurbleProtocol.STATUS_UUID)
        settingsCharacteristic = service?.getCharacteristic(FurbleProtocol.SETTINGS_UUID)
        triggerCharacteristic = service?.getCharacteristic(FurbleProtocol.TRIGGER_UUID)
        authCharacteristic = service?.getCharacteristic(FurbleProtocol.AUTH_UUID)
        capabilityCharacteristic = service?.getCharacteristic(FurbleProtocol.CAPABILITY_UUID)
        if (service == null || locationCharacteristic == null || statusCharacteristic == null ||
            settingsCharacteristic == null || triggerCharacteristic == null
        ) {
            fail("The furble companion service is missing a required characteristic")
            return
        }
        listener.onAuthAvailability(authCharacteristic != null)
        if (!gatt.requestMtu(256)) {
            fail("The phone could not request the required BLE MTU")
        }
    }

    private fun onMtuChanged(negotiatedMtu: Int, status: Int) {
        if (status != BluetoothGatt.GATT_SUCCESS || negotiatedMtu < 45) {
            fail("furble requires a negotiated BLE MTU of at least 45 bytes")
            return
        }
        mtu = negotiatedMtu
        configureNotifications()
    }

    private fun configureNotifications() {
        val currentGatt = gatt ?: return
        val status = statusCharacteristic ?: return
        val settings = settingsCharacteristic ?: return
        val capability = capabilityCharacteristic
        val statusDescriptor = status.getDescriptor(CLIENT_CHARACTERISTIC_CONFIGURATION_UUID)
        val settingsDescriptor = settings.getDescriptor(CLIENT_CHARACTERISTIC_CONFIGURATION_UUID)
        val auth = authCharacteristic
        val authDescriptor = auth?.getDescriptor(CLIENT_CHARACTERISTIC_CONFIGURATION_UUID)
        if (statusDescriptor == null || settingsDescriptor == null) {
            fail("furble notification descriptors are missing")
            return
        }
        if (!currentGatt.setCharacteristicNotification(status, true)) {
            fail("Android could not enable furble status notifications")
            return
        }
        enqueueDescriptorWrite(statusDescriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) {
            if (!it) {
                fail("furble status notification setup failed; retry the connection")
                return@enqueueDescriptorWrite
            }
            if (!currentGatt.setCharacteristicNotification(settings, true)) {
                fail("Android could not enable furble settings indications")
                return@enqueueDescriptorWrite
            }
            enqueueDescriptorWrite(settingsDescriptor, BluetoothGattDescriptor.ENABLE_INDICATION_VALUE) {
                if (!it) {
                    fail("furble settings indication setup failed; retry the connection")
                    return@enqueueDescriptorWrite
                }
                if (auth == null) {
                    finishReady(capability)
                    return@enqueueDescriptorWrite
                }
                if (authDescriptor == null) {
                    fail("furble AUTH indication setup failed; retry the connection")
                    return@enqueueDescriptorWrite
                }
                if (!currentGatt.setCharacteristicNotification(auth, true)) {
                    fail("Android could not enable furble AUTH indications")
                    return@enqueueDescriptorWrite
                }
                enqueueDescriptorWrite(authDescriptor, BluetoothGattDescriptor.ENABLE_INDICATION_VALUE) {
                    if (!it) {
                        fail("furble AUTH indication setup failed; retry the connection")
                        return@enqueueDescriptorWrite
                    }
                    finishReady(capability)
                }
            }
        }
    }

    private fun finishReady(capability: BluetoothGattCharacteristic?) {
        if (capability != null) enqueueCharacteristicRead(FurbleProtocol.CAPABILITY_UUID, optional = true)
        enqueueCharacteristicRead(FurbleProtocol.STATUS_UUID)
        isReady = true
        listener.onReady()
    }

    private fun enqueueCharacteristicRead(uuid: UUID, optional: Boolean = false) {
        val characteristic = characteristic(uuid) ?: return
        operations.addLast(Operation.ReadCharacteristic(characteristic, optional))
        pump()
    }

    private fun enqueueCharacteristicWrite(
        uuid: UUID,
        value: ByteArray,
        writeType: Int,
        waitForCallback: Boolean,
    ) {
        val characteristic = characteristic(uuid) ?: return
        operations.addLast(
            Operation.WriteCharacteristic(
                characteristic = characteristic,
                value = value.copyOf(),
                writeType = writeType,
                waitForCallback = waitForCallback,
                authGeneration = if (uuid == FurbleProtocol.AUTH_UUID) authGeneration else 0L,
            ),
        )
        pump()
    }

    private fun enqueueDescriptorWrite(
        descriptor: BluetoothGattDescriptor,
        value: ByteArray,
        completion: (Boolean) -> Unit,
    ) {
        operations.addLast(Operation.WriteDescriptor(descriptor, value.copyOf(), completion))
        pump()
    }

    private fun characteristic(uuid: UUID): BluetoothGattCharacteristic? {
        return service?.getCharacteristic(uuid) ?: run {
            listener.onError("furble characteristic $uuid is unavailable")
            null
        }
    }

    private fun pump() {
        if (currentOperation != null || operations.isEmpty()) return
        val current = operations.removeFirst()
        currentOperation = current
        val started = try {
            when (current) {
                is Operation.ReadCharacteristic -> gatt?.readCharacteristic(current.characteristic) == true
                is Operation.WriteCharacteristic -> {
                    current.characteristic.writeType = current.writeType
                    current.characteristic.value = current.value
                    gatt?.writeCharacteristic(current.characteristic) == true
                }
                is Operation.WriteDescriptor -> {
                    current.descriptor.value = current.value
                    gatt?.writeDescriptor(current.descriptor) == true
                }
            }
        } catch (securityException: SecurityException) {
            listener.onError("Bluetooth permission was revoked")
            false
        }
        if (!started) {
            finishCurrent(false, "Android rejected the BLE operation")
        } else if (current is Operation.WriteCharacteristic && !current.waitForCallback) {
            // Write-no-response has no reliable ATT completion callback.
            finishCurrent(true, null)
        }
    }

    private fun finishCurrent(success: Boolean, failureMessage: String?) {
        val operation = currentOperation ?: return
        currentOperation = null
        if (operation is Operation.WriteCharacteristic &&
            operation.characteristic.uuid == FurbleProtocol.AUTH_UUID && !success
        ) {
            clearAuthSecrets()
        }
        if (!success && failureMessage != null) listener.onError(failureMessage)
        if (operation is Operation.WriteDescriptor) operation.completion(success)
        pump()
    }

    private fun fail(message: String) {
        listener.onError(message)
        closeInternal(notify = true)
    }

    private fun closeInternal(notify: Boolean) {
        isReady = false
        operations.clear()
        currentOperation = null
        service = null
        locationCharacteristic = null
        statusCharacteristic = null
        settingsCharacteristic = null
        triggerCharacteristic = null
        authCharacteristic = null
        capabilityCharacteristic = null
        clearAuthSecrets()
        val oldGatt = gatt
        gatt = null
        oldGatt?.disconnect()
        oldGatt?.close()
        if (notify) listener.onDisconnected()
    }

    private fun notifyDisconnected() {
        if (gatt != null) closeInternal(notify = true) else listener.onDisconnected()
    }

    private fun dispatchCharacteristic(characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        when (characteristic.uuid) {
            FurbleProtocol.STATUS_UUID -> FurbleProtocol.decodeStatus(value)?.let(listener::onStatus)
            FurbleProtocol.CAPABILITY_UUID -> listener.onCapabilities(FurbleProtocol.parseCapability(value))
            FurbleProtocol.SETTINGS_UUID -> FurbleProtocol.parseSettingsResponse(value)?.let(listener::onSettings)
            FurbleProtocol.AUTH_UUID -> dispatchAuth(value)
        }
    }

    private fun dispatchAuth(value: ByteArray) {
        if (value.size == FurbleProtocol.AUTH_NONCE_SIZE && authChallengePending) {
            if (authAttemptGeneration != authGeneration) return
            val password = authPassword ?: run {
                listener.onError("furble sent an AUTH challenge without a password")
                return
            }
            authPassword = null
            authChallengePending = false
            val response = try {
                FurbleProtocol.encodeAuthResponse(password, value)
            } catch (error: IllegalArgumentException) {
                password.fill(0)
                listener.onError(error.message ?: "Invalid furble AUTH challenge")
                return
            }
            password.fill(0)
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.AUTH_UUID,
                value = response,
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                waitForCallback = true,
            )
            response.fill(0)
            return
        }
        if (value.size == 1) {
            if (authAttemptGeneration != authGeneration) return
            listener.onAuthResult(value[0].toInt() and 0xff)
            authAttemptGeneration = null
            clearAuthSecrets()
        } else {
            listener.onError("furble sent an invalid AUTH indication")
            clearAuthSecrets()
        }
    }

    private fun clearAuthSecrets() {
        authPassword?.fill(0)
        authPassword = null
        authChallengePending = false
        authAttemptGeneration = null
    }

    private sealed interface Operation {
        data class ReadCharacteristic(
            val characteristic: BluetoothGattCharacteristic,
            val optional: Boolean,
        ) : Operation

        data class WriteCharacteristic(
            val characteristic: BluetoothGattCharacteristic,
            val value: ByteArray,
            val writeType: Int,
            val waitForCallback: Boolean,
            val authGeneration: Long,
        ) : Operation

        data class WriteDescriptor(
            val descriptor: BluetoothGattDescriptor,
            val value: ByteArray,
            val completion: (Boolean) -> Unit,
        ) : Operation
    }

    private inner class Callback : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            handler.post {
                if (gatt !== this@GattConnection.gatt) return@post
                if (status == BluetoothGatt.GATT_SUCCESS &&
                    newState == android.bluetooth.BluetoothProfile.STATE_CONNECTED
                ) {
                    listener.onDiscoveringServices()
                    if (!gatt.discoverServices()) fail("Android could not start service discovery")
                } else if (newState == android.bluetooth.BluetoothProfile.STATE_DISCONNECTED) {
                    closeInternal(notify = true)
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            handler.post {
                if (gatt === this@GattConnection.gatt) onServicesDiscovered(gatt, status)
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            handler.post {
                if (gatt === this@GattConnection.gatt) onMtuChanged(mtu, status)
            }
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            val value = characteristic.value?.copyOf() ?: byteArrayOf()
            handler.post {
                if (gatt !== this@GattConnection.gatt) return@post
                if (status == BluetoothGatt.GATT_SUCCESS) dispatchCharacteristic(characteristic, value)
                val operation = currentOperation
                if (operation is Operation.ReadCharacteristic) {
                    if (status != BluetoothGatt.GATT_SUCCESS && operation.optional) {
                        listener.onCapabilities(null)
                    }
                    finishCurrent(
                        status == BluetoothGatt.GATT_SUCCESS,
                        if (operation.optional) null else
                            "furble characteristic read failed with status $status",
                    )
                }
            }
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            handler.post {
                if (gatt !== this@GattConnection.gatt) return@post
                if (status == BluetoothGatt.GATT_SUCCESS) dispatchCharacteristic(characteristic, value.copyOf())
                val operation = currentOperation
                if (operation is Operation.ReadCharacteristic) {
                    if (status != BluetoothGatt.GATT_SUCCESS && operation.optional) {
                        listener.onCapabilities(null)
                    }
                    finishCurrent(
                        status == BluetoothGatt.GATT_SUCCESS,
                        if (operation.optional) null else
                            "furble characteristic read failed with status $status",
                    )
                }
            }
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            val value = characteristic.value?.copyOf() ?: byteArrayOf()
            handler.post {
                if (gatt === this@GattConnection.gatt) dispatchCharacteristic(characteristic, value)
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            handler.post {
                if (gatt === this@GattConnection.gatt) dispatchCharacteristic(characteristic, value.copyOf())
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            handler.post {
                if (gatt !== this@GattConnection.gatt) return@post
                val operation = currentOperation
                if (operation is Operation.WriteCharacteristic &&
                    operation.waitForCallback && operation.characteristic.uuid == characteristic.uuid
                ) {
                    if (operation.characteristic.uuid == FurbleProtocol.AUTH_UUID &&
                        status != BluetoothGatt.GATT_SUCCESS
                    ) {
                        clearAuthSecrets()
                    }
                    finishCurrent(
                        status == BluetoothGatt.GATT_SUCCESS,
                        "furble write failed with status $status",
                    )
                }
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            handler.post {
                if (gatt !== this@GattConnection.gatt) return@post
                val operation = currentOperation
                if (operation is Operation.WriteDescriptor && operation.descriptor.uuid == descriptor.uuid) {
                    finishCurrent(
                        status == BluetoothGatt.GATT_SUCCESS,
                        "furble notification setup failed with status $status",
                    )
                }
            }
        }
    }

    private companion object {
        val CLIENT_CHARACTERISTIC_CONFIGURATION_UUID: UUID =
            UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
