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

internal class AuthInputDispatcher(private val post: (() -> Unit) -> Unit) {
    fun submit(input: ByteArray, consume: (ByteArray) -> Unit) {
        val owned = input.copyOf()
        post { consume(owned) }
    }
}

internal class AuthAttemptTracker {
    private var generation = 0L
    private var activeGeneration: Long? = null

    fun begin(): Long {
        activeGeneration = ++generation
        return generation
    }

    fun cancel() {
        activeGeneration = null
        generation++
    }

    fun accepts(candidate: Long): Boolean = activeGeneration == candidate && candidate == generation
}

internal enum class AuthPacketAction { RESULT, CHALLENGE, PASSWORD_REQUIRED, INVALID }

internal fun classifyAuthPacket(value: ByteArray, challengePending: Boolean): AuthPacketAction {
    if (value.size == FurbleProtocol.AUTH_RESULT_PACKET_SIZE &&
        runCatching { FurbleProtocol.decodeAuthResult(value) }.isSuccess
    ) return AuthPacketAction.RESULT
    if (value.size == FurbleProtocol.AUTH_CHALLENGE_PACKET_SIZE &&
        runCatching { FurbleProtocol.decodeAuthChallenge(value) }.isSuccess
    ) return if (challengePending) AuthPacketAction.CHALLENGE else AuthPacketAction.PASSWORD_REQUIRED
    return AuthPacketAction.INVALID
}

internal fun cameraResponseCompletes(
    operation: Int,
    cameraId: Int,
    record: FurbleProtocol.CameraRecord,
): Boolean {
    if (operation == FurbleProtocol.CameraOperation.LIST) {
        return record.isTerminator || record.status != FurbleProtocol.CameraStatus.OK
    }
    if (record.status != FurbleProtocol.CameraStatus.OK) return true
    return record.cameraId == cameraId &&
        record.cameraType == 0 && record.flags == 0 && record.progress == 0 &&
        record.rssi == FurbleProtocol.CAMERA_RSSI_UNKNOWN &&
        record.state == FurbleProtocol.CameraState.IDLE && record.name.isEmpty()
}

internal data class CameraRequest(val operation: Int, val cameraId: Int)

internal class CameraRequestQueue {
    private val pending = ArrayDeque<CameraRequest>()
    var inFlight: CameraRequest? = null
        private set

    fun enqueue(request: CameraRequest) {
        pending.addLast(request)
    }

    fun startNext(): CameraRequest? {
        if (inFlight != null || pending.isEmpty()) return null
        return pending.removeFirst().also { inFlight = it }
    }

    fun complete(): CameraRequest? = inFlight.also { inFlight = null }

    fun clear() {
        pending.clear()
        inFlight = null
    }
}

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
        fun onCamera(record: FurbleProtocol.CameraRecord)
        fun onCameraListStarted()
        fun onCameraAvailability(available: Boolean)
        fun onAuthAvailability(supported: Boolean)
        fun onAuthResult(result: Int)
        fun onDisconnected()
        fun onError(message: String)
    }

    private val appContext = context.applicationContext
    private val handler = Handler(Looper.getMainLooper())
    private val authInputDispatcher = AuthInputDispatcher { task -> handler.post { task() } }
    private val operations = ArrayDeque<Operation>()
    private val cameraRequests = CameraRequestQueue()
    private val callback = Callback()

    private var gatt: BluetoothGatt? = null
    private var service: BluetoothGattService? = null
    private var locationCharacteristic: BluetoothGattCharacteristic? = null
    private var statusCharacteristic: BluetoothGattCharacteristic? = null
    private var settingsCharacteristic: BluetoothGattCharacteristic? = null
    private var triggerCharacteristic: BluetoothGattCharacteristic? = null
    private var cameraCharacteristic: BluetoothGattCharacteristic? = null
    private var authCharacteristic: BluetoothGattCharacteristic? = null
    private var capabilityCharacteristic: BluetoothGattCharacteristic? = null
    private var currentOperation: Operation? = null
    private var isReady = false
    private var mtu = 23
    private var authPassword: ByteArray? = null
    private var authChallengePending = false
    private val authAttemptTracker = AuthAttemptTracker()
    private var authGeneration = 0L
    private var cameraResponseTimeout: Runnable? = null
    private var operationTimeout: Runnable? = null

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
            authAttemptTracker.cancel()
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

    fun requestCameras() {
        handler.post {
            queueCameraRequest(CameraRequest(FurbleProtocol.CameraOperation.LIST, 0xFF))
        }
    }

    fun setCamera(operation: Int, cameraId: Int) {
        handler.post {
            queueCameraRequest(CameraRequest(operation, cameraId))
        }
    }

    private fun queueCameraRequest(request: CameraRequest) {
        if (!isReady) {
            listener.onError("furble is not ready for camera operations")
            return
        }
        if (cameraCharacteristic == null) {
            listener.onError("furble camera characteristic is unavailable")
            return
        }
        cameraRequests.enqueue(request)
        pumpCameraRequest()
    }

    private fun pumpCameraRequest() {
        if (!isReady) return
        val request = cameraRequests.startNext() ?: return
        enqueueCharacteristicWrite(
            uuid = FurbleProtocol.CAMERAS_UUID,
            value = if (request.operation == FurbleProtocol.CameraOperation.LIST) {
                FurbleProtocol.encodeCameraListRequest()
            } else {
                FurbleProtocol.encodeCameraRequest(request.operation, request.cameraId)
            },
            writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
            waitForCallback = true,
        )
    }

    private fun armCameraResponseTimeout(request: CameraRequest) {
        cameraResponseTimeout?.let(handler::removeCallbacks)
        val timeout = Runnable {
            if (cameraRequests.inFlight != request) return@Runnable
            abortCameraRequests()
            listener.onError("furble camera operation timed out")
            closeInternal(notify = true)
        }
        cameraResponseTimeout = timeout
        handler.postDelayed(timeout, CAMERA_RESPONSE_TIMEOUT_MS)
    }

    private fun armOperationTimeout(operation: Operation) {
        operationTimeout?.let(handler::removeCallbacks)
        val timeout = Runnable {
            if (currentOperation !== operation) return@Runnable
            abortCameraRequests()
            listener.onError("furble ATT operation timed out")
            closeInternal(notify = true)
        }
        operationTimeout = timeout
        handler.postDelayed(timeout, ATT_OPERATION_TIMEOUT_MS)
    }

    private fun completeCameraRequest() {
        cameraResponseTimeout?.let(handler::removeCallbacks)
        cameraResponseTimeout = null
        cameraRequests.complete()
    }

    private fun abortCameraRequests() {
        cameraResponseTimeout?.let(handler::removeCallbacks)
        cameraResponseTimeout = null
        cameraRequests.clear()
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
        authInputDispatcher.submit(passwordUtf8, ::authenticateOwned)
    }

    /** Starts the framed handshake for firmware with an empty password. */
    fun authenticateWithoutPassword() {
        handler.post {
            if (!isReady) {
                listener.onError("furble is not ready for authentication")
                return@post
            }
            if (authCharacteristic == null) {
                listener.onError("The furble does not expose its AUTH characteristic")
                return@post
            }
            authPassword?.fill(0)
            authPassword = null
            authChallengePending = false
            authGeneration = authAttemptTracker.begin()
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.AUTH_UUID,
                value = FurbleProtocol.encodeAuthBegin(),
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                waitForCallback = true,
            )
        }
    }

    private fun authenticateOwned(passwordUtf8: ByteArray) {
        try {
            if (!isReady) {
                listener.onError("furble is not ready for authentication")
                return
            }
            if (passwordUtf8.size !in 1..FurbleProtocol.COMPANION_PASSWORD_MAX) {
                listener.onError("Companion password must be 1..${FurbleProtocol.COMPANION_PASSWORD_MAX} UTF-8 bytes")
                return
            }
            if (authCharacteristic == null) {
                listener.onError("The furble does not expose its AUTH characteristic")
                return
            }
            authPassword?.fill(0)
            authPassword = passwordUtf8.copyOf()
            authChallengePending = true
            authGeneration = authAttemptTracker.begin()
            enqueueCharacteristicWrite(
                uuid = FurbleProtocol.AUTH_UUID,
                value = FurbleProtocol.encodeAuthBegin(),
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                waitForCallback = true,
            )
        } finally {
            // Once copied into authPassword or the queued operation, the
            // dispatch-owned input is no longer needed.
            passwordUtf8.fill(0)
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
        cameraCharacteristic = service?.getCharacteristic(FurbleProtocol.CAMERAS_UUID)
        authCharacteristic = service?.getCharacteristic(FurbleProtocol.AUTH_UUID)
        capabilityCharacteristic = service?.getCharacteristic(FurbleProtocol.CAPABILITY_UUID)
        if (service == null || locationCharacteristic == null || statusCharacteristic == null ||
            settingsCharacteristic == null || triggerCharacteristic == null
        ) {
            fail("The furble companion service is missing a required characteristic")
            return
        }
        listener.onCameraAvailability(cameraCharacteristic != null)
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
                    configureCameraNotifications(capability)
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
                    configureCameraNotifications(capability)
                }
            }
        }
    }

    private fun configureCameraNotifications(capability: BluetoothGattCharacteristic?) {
        val currentGatt = gatt ?: return
        val camera = cameraCharacteristic
        if (camera == null) {
            finishReady(capability)
            return
        }
        val descriptor = camera.getDescriptor(CLIENT_CHARACTERISTIC_CONFIGURATION_UUID)
        if (descriptor == null) {
            fail("furble camera notification descriptor is missing")
            return
        }
        if (!currentGatt.setCharacteristicNotification(camera, true)) {
            fail("Android could not enable furble camera notifications")
            return
        }
        // The camera characteristic carries both indications (responses) and
        // notifications (live state). NimBLE accepts both CCCD bits together.
        enqueueDescriptorWrite(descriptor, byteArrayOf(0x03, 0x00)) {
            if (!it) {
                fail("furble camera notification setup failed; retry the connection")
                return@enqueueDescriptorWrite
            }
            finishReady(capability)
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
        if (started) {
            if (current is Operation.WriteCharacteristic &&
                current.characteristic.uuid == FurbleProtocol.CAMERAS_UUID &&
                current.waitForCallback
            ) {
                cameraRequests.inFlight?.let { request ->
                    if (request.operation == FurbleProtocol.CameraOperation.LIST) {
                        listener.onCameraListStarted()
                    }
                    armCameraResponseTimeout(request)
                }
            }
            if (current is Operation.ReadCharacteristic ||
                current is Operation.WriteDescriptor ||
                current is Operation.WriteCharacteristic && current.waitForCallback
            ) armOperationTimeout(current)
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
        operationTimeout?.let(handler::removeCallbacks)
        operationTimeout = null
        currentOperation = null
        if (operation is Operation.WriteCharacteristic &&
            operation.characteristic.uuid == FurbleProtocol.AUTH_UUID && !success
        ) {
            clearAuthSecrets()
        }
        val failedCameraWrite = operation is Operation.WriteCharacteristic &&
            operation.characteristic.uuid == FurbleProtocol.CAMERAS_UUID && !success
        if (failedCameraWrite) {
            abortCameraRequests()
        }
        if (!success && failureMessage != null) listener.onError(failureMessage)
        if (operation is Operation.WriteDescriptor) operation.completion(success)
        if (failedCameraWrite) {
            closeInternal(notify = true)
            return
        }
        pump()
    }

    private fun fail(message: String) {
        listener.onError(message)
        closeInternal(notify = true)
    }

    private fun closeInternal(notify: Boolean) {
        isReady = false
        operationTimeout?.let(handler::removeCallbacks)
        operationTimeout = null
        abortCameraRequests()
        operations.clear()
        currentOperation = null
        service = null
        locationCharacteristic = null
        statusCharacteristic = null
        settingsCharacteristic = null
        triggerCharacteristic = null
        cameraCharacteristic = null
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
            FurbleProtocol.CAMERAS_UUID -> {
                val record = FurbleProtocol.parseCameraRecord(value)
                if (record == null) {
                    abortCameraRequests()
                    listener.onError("furble sent an invalid camera record")
                    closeInternal(notify = true)
                } else {
                    val request = cameraRequests.inFlight
                    if (request != null && cameraResponseCompletes(request.operation, request.cameraId, record)) {
                        completeCameraRequest()
                    }
                    listener.onCamera(record)
                    if (request != null && cameraRequests.inFlight == null) pumpCameraRequest()
                }
            }
            FurbleProtocol.AUTH_UUID -> dispatchAuth(value)
        }
    }

    private fun dispatchAuth(value: ByteArray) {
        when (classifyAuthPacket(value, authChallengePending)) {
            AuthPacketAction.RESULT -> {
                if (!authAttemptTracker.accepts(authGeneration)) return
                listener.onAuthResult(FurbleProtocol.decodeAuthResult(value))
                clearAuthSecrets()
                return
            }
            AuthPacketAction.PASSWORD_REQUIRED -> {
                listener.onError("furble requires a password for authentication")
                clearAuthSecrets()
                return
            }
            AuthPacketAction.INVALID -> {
                listener.onError("furble sent an invalid AUTH indication")
                clearAuthSecrets()
                return
            }
            AuthPacketAction.CHALLENGE -> Unit
        }

        if (!authAttemptTracker.accepts(authGeneration)) return
        val password = authPassword ?: run {
            listener.onError("furble sent an AUTH challenge without a password")
            clearAuthSecrets()
            return
        }
        val nonce = try {
            FurbleProtocol.decodeAuthChallenge(value)
        } catch (error: IllegalArgumentException) {
            password.fill(0)
            listener.onError(error.message ?: "Invalid furble AUTH challenge")
            clearAuthSecrets()
            return
        }
        authPassword = null
        authChallengePending = false
        val response = try {
            FurbleProtocol.encodeAuthResponse(password, nonce)
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
    }

    private fun clearAuthSecrets() {
        authPassword?.fill(0)
        authPassword = null
        authChallengePending = false
        authAttemptTracker.cancel()
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
                val operation = currentOperation
                if (operation is Operation.ReadCharacteristic && operation.characteristic === characteristic) {
                    if (status == BluetoothGatt.GATT_SUCCESS) dispatchCharacteristic(characteristic, value)
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
                val operation = currentOperation
                if (operation is Operation.ReadCharacteristic && operation.characteristic === characteristic) {
                    if (status == BluetoothGatt.GATT_SUCCESS) dispatchCharacteristic(characteristic, value.copyOf())
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
                    operation.waitForCallback && operation.characteristic === characteristic
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
                if (operation is Operation.WriteDescriptor && operation.descriptor === descriptor) {
                    finishCurrent(
                        status == BluetoothGatt.GATT_SUCCESS,
                        "furble notification setup failed with status $status",
                    )
                }
            }
        }
    }

    private companion object {
        const val CAMERA_RESPONSE_TIMEOUT_MS = 5000L
        const val ATT_OPERATION_TIMEOUT_MS = 5000L
        val CLIENT_CHARACTERISTIC_CONFIGURATION_UUID: UUID =
            UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
