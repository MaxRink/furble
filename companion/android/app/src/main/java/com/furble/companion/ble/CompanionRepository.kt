package com.furble.companion.ble

import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.companion.BluetoothLeDeviceFilter
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.CompanionDeviceManager
import android.companion.ObservingDevicePresenceRequest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Parcelable
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import androidx.core.content.ContextCompat
import com.furble.companion.location.FusedLocationProvider
import com.furble.companion.permissions.AppPermissions
import com.furble.companion.permissions.PermissionSnapshot
import com.furble.companion.protocol.FurbleProtocol
import java.util.regex.Pattern
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

class CompanionRepository(context: Context) {
    companion object {
        const val DEFAULT_LOCATION_INTERVAL_SECONDS = 10
        private const val LOCATION_INTERVAL_KEY = "location_interval_seconds"
        private const val LOCATION_ENABLED_KEY = "location_enabled"
    }

    private val appContext = context.applicationContext
    private val preferences = appContext.getSharedPreferences("companion", Context.MODE_PRIVATE)
    private val handler = Handler(Looper.getMainLooper())
    private val companionDeviceManager =
        appContext.getSystemService(CompanionDeviceManager::class.java)
    private val bluetoothAdapter: BluetoothAdapter? =
        appContext.getSystemService(BluetoothManager::class.java)?.adapter
    private val _state = MutableStateFlow(
        CompanionUiState(
            locationEnabled = preferences.getBoolean(LOCATION_ENABLED_KEY, false),
            locationIntervalSeconds = preferences.getInt(
                LOCATION_INTERVAL_KEY,
                DEFAULT_LOCATION_INTERVAL_SECONDS,
            ),
        ),
    )
    val state: StateFlow<CompanionUiState> = _state.asStateFlow()

    private var permissions = AppPermissions.snapshot(appContext)
    private var associationAddress: String? = null
    private var associationId: Int? = null
    private var devicePresent = false
    private var gattConnection: GattConnection? = null
    private var pendingSettingId: Int? = null
    private var locationProvider: FusedLocationProvider? = null

    private val triggerController = DeadManTriggerController(
        send = { operation, holdMs -> gattConnection?.sendTrigger(operation, holdMs) },
        onActiveChanged = { shutterHeld, focusHeld ->
            _state.update { it.copy(shutterHeld = shutterHeld, focusHeld = focusHeld) }
        },
    )

    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            val device = intent.getParcelableExtraCompat<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                ?: return
            if (device.address != associationAddress) return
            when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR)) {
                BluetoothDevice.BOND_BONDED -> {
                    _state.update { it.copy(association = it.association.copy(bonded = true)) }
                    if (devicePresent) connectBondedDevice()
                }
                BluetoothDevice.BOND_NONE -> {
                    setError("furble bonding was not completed")
                    _state.update { it.copy(association = it.association.copy(bonded = false)) }
                }
            }
        }
    }

    private val associationCallback = object : CompanionDeviceManager.Callback() {
        override fun onDeviceFound(chooserLauncher: android.content.IntentSender) {
            handler.post {
                _state.update {
                    it.copy(pairingInProgress = true, chooserIntentSender = chooserLauncher, error = null)
                }
            }
        }

        override fun onFailure(error: CharSequence?) {
            handler.post {
                _state.update {
                    it.copy(
                        pairingInProgress = false,
                        chooserIntentSender = null,
                        error = error?.toString() ?: "No furble device was found",
                    )
                }
            }
        }
    }

    init {
        ContextCompat.registerReceiver(
            appContext,
            bondReceiver,
            IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED),
            // Bluetooth broadcasts originate in the system Bluetooth process.
            ContextCompat.RECEIVER_EXPORTED,
        )
        locationProvider = FusedLocationProvider(
            context = appContext,
            onFixBatch = ::onFixBatch,
            onError = ::setError,
        )
        refreshAssociation()
    }

    fun onPermissionsChanged(snapshot: PermissionSnapshot = AppPermissions.snapshot(appContext)) {
        handler.post {
            permissions = snapshot
            if (!snapshot.bluetoothReady) {
                stopGattAndLocation(clearData = true)
                _state.update { it.copy(connection = ConnectionState.PERMISSION_DENIED) }
            } else {
                refreshAssociation()
            }
            if (!snapshot.fineLocation) {
                locationProvider?.stop()
                _state.update { it.copy(locationEnabled = false) }
            }
        }
    }

    fun startAssociation() {
        handler.post {
            if (!permissions.allRequired) {
                setError("Bluetooth and precise location permissions are required before pairing")
                return@post
            }
            val serviceFilter = BluetoothLeDeviceFilter.Builder()
                .setScanFilter(
                    ScanFilter.Builder()
                        .setServiceUuid(ParcelUuid(FurbleProtocol.SERVICE_UUID))
                        .build(),
                )
                .build()
            val nameFilter = BluetoothLeDeviceFilter.Builder()
                .setNamePattern(Pattern.compile("^furble-", Pattern.CASE_INSENSITIVE))
                .build()
            val request = AssociationRequest.Builder()
                .addDeviceFilter(serviceFilter)
                .addDeviceFilter(nameFilter)
                .setSingleDevice(true)
                .build()
            try {
                _state.update { it.copy(pairingInProgress = true, error = null) }
                companionDeviceManager.associate(request, associationCallback, handler)
            } catch (securityException: SecurityException) {
                _state.update { it.copy(pairingInProgress = false) }
                setError("Android denied the companion device scan")
            } catch (unsupported: UnsupportedOperationException) {
                _state.update { it.copy(pairingInProgress = false) }
                setError("This phone does not support companion device association")
            }
        }
    }

    fun onAssociationChooserResult(resultCode: Int, data: Intent?) {
        handler.post {
            _state.update { it.copy(pairingInProgress = false, chooserIntentSender = null) }
            if (resultCode != Activity.RESULT_OK) {
                setError("Pairing was canceled")
                return@post
            }
            val associationInfo = data?.extractAssociationInfo()
            associationId = associationInfo?.id ?: associationId
            val modernDevice = if (Build.VERSION.SDK_INT >= 34) {
                associationInfo?.associatedDevice?.bleDevice?.device
            } else {
                null
            }
            val device = data?.extractBluetoothDevice() ?: modernDevice
            if (device != null) associationAddress = device.address
            refreshAssociation(preferredAddress = device?.address)
        }
    }

    fun clearChooserIntentSender() {
        _state.update { it.copy(chooserIntentSender = null) }
    }

    fun refreshAssociation() = refreshAssociation(preferredAddress = null)

    private fun refreshAssociation(preferredAddress: String?) {
        handler.post {
            try {
                @Suppress("DEPRECATION")
                val legacyAddresses = companionDeviceManager.associations
                val modernAssociations = if (Build.VERSION.SDK_INT >= 31) {
                    companionDeviceManager.myAssociations
                } else {
                    emptyList()
                }
                val modernAddress = if (Build.VERSION.SDK_INT >= 33) {
                    modernAssociations.firstOrNull()?.deviceMacAddress?.toString()
                } else {
                    null
                }
                associationAddress = preferredAddress ?: legacyAddresses.firstOrNull() ?: modernAddress
                associationId = modernAssociations.firstOrNull()?.id
                val address = associationAddress
                if (address == null) {
                    devicePresent = false
                    stopGattAndLocation(clearData = true)
                    _state.value = _state.value.copy(
                        association = AssociationState(),
                        connection = ConnectionState.NO_ASSOCIATION,
                    )
                    return@post
                }
                val device = bluetoothAdapter?.getRemoteDevice(address)
                val bonded = device?.bondState == BluetoothDevice.BOND_BONDED
                _state.update {
                    it.copy(
                        association = AssociationState(
                            associated = true,
                            address = address,
                            deviceName = runCatching { device?.name }.getOrNull(),
                            present = devicePresent,
                            bonded = bonded,
                        ),
                        connection = when {
                            it.connection == ConnectionState.READY -> it.connection
                            devicePresent -> ConnectionState.ASSOCIATED
                            else -> ConnectionState.OUT_OF_RANGE
                        },
                        error = null,
                    )
                }
                observePresence(address)
                if (devicePresent && gattConnection == null) ensureBondAndConnect()
            } catch (securityException: SecurityException) {
                stopGattAndLocation(clearData = true)
                _state.update { it.copy(connection = ConnectionState.PERMISSION_DENIED) }
                setError("Bluetooth permission is required to read the association")
            } catch (illegalArgument: IllegalArgumentException) {
                setError("The associated Bluetooth address is not available")
            }
        }
    }

    fun onDeviceAppeared(address: String?) {
        handler.post {
            if (address != null) associationAddress = address
            devicePresent = true
            refreshAssociation(preferredAddress = associationAddress)
        }
    }

    fun onDeviceDisappeared(address: String?) {
        handler.post {
            if (address != null && address != associationAddress) return@post
            devicePresent = false
            triggerController.onLinkLost()
            stopGattAndLocation(clearData = true)
            if (_state.value.association.associated) {
                _state.update { it.copy(association = it.association.copy(present = false), connection = ConnectionState.OUT_OF_RANGE) }
            }
        }
    }

    fun connectAssociatedDevice() {
        handler.post {
            if (!permissions.bluetoothReady) {
                _state.update { it.copy(connection = ConnectionState.PERMISSION_DENIED) }
                return@post
            }
            if (associationAddress == null) {
                setError("Pair a furble device before connecting")
                return@post
            }
            devicePresent = true
            _state.update { it.copy(association = it.association.copy(present = true)) }
            ensureBondAndConnect()
        }
    }

    fun setLocationEnabled(enabled: Boolean) {
        handler.post {
            if (enabled && !permissions.fineLocation) {
                setError("Precise location permission is required before enabling GPS")
                return@post
            }
            preferences.edit().putBoolean(LOCATION_ENABLED_KEY, enabled).apply()
            _state.update { it.copy(locationEnabled = enabled, error = if (enabled) it.error else null) }
            syncLocationUpdates()
        }
    }

    fun setLocationInterval(seconds: Int) {
        val safeSeconds = seconds.coerceIn(1, 3600)
        preferences.edit().putInt(LOCATION_INTERVAL_KEY, safeSeconds).apply()
        _state.update { it.copy(locationIntervalSeconds = safeSeconds) }
        syncLocationUpdates()
    }

    fun requestSettings() {
        _state.update { it.copy(settingsLoading = true) }
        gattConnection?.requestSettingsList()
    }

    fun setBooleanSetting(record: FurbleProtocol.SettingRecord, value: Boolean) {
        if (record.type != FurbleProtocol.SettingType.BOOL) return
        pendingSettingId = record.id
        gattConnection?.setSetting(record.id, byteArrayOf(if (value) 1 else 0))
    }

    fun setUint8Setting(record: FurbleProtocol.SettingRecord, value: Int) {
        if (record.type != FurbleProtocol.SettingType.UINT8 || value !in 0..255) {
            setError("A uint8 setting must be between 0 and 255")
            return
        }
        pendingSettingId = record.id
        gattConnection?.setSetting(record.id, byteArrayOf(value.toByte()))
    }

    fun pressShutter() = triggerController.pressShutter()
    fun releaseShutter() = triggerController.releaseShutter()
    fun pressFocus() = triggerController.pressFocus()
    fun releaseFocus() = triggerController.releaseFocus()
    fun timedShutter(holdMs: Int) {
        if (holdMs in 1..0xFFFF) triggerController.timedShutter(holdMs)
        else setError("Timed shutter hold must be between 1 and 65535 ms")
    }

    fun releaseAllTriggers() = triggerController.releaseAll()

    fun clearError() = _state.update { it.copy(error = null) }

    private fun ensureBondAndConnect() {
        val address = associationAddress ?: return
        val device = try {
            bluetoothAdapter?.getRemoteDevice(address)
        } catch (illegalArgument: IllegalArgumentException) {
            null
        }
        if (device == null) {
            setError("The associated furble device is unavailable")
            return
        }
        when (device.bondState) {
            BluetoothDevice.BOND_BONDED -> connectBondedDevice(device)
            BluetoothDevice.BOND_BONDING -> {
                _state.update { it.copy(connection = ConnectionState.BONDING) }
            }
            else -> try {
                _state.update { it.copy(connection = ConnectionState.BONDING) }
                if (!device.createBond()) setError("Android could not start furble bonding")
            } catch (securityException: SecurityException) {
                setError("Bluetooth permission is required to bond with furble")
            }
        }
    }

    private fun connectBondedDevice(device: BluetoothDevice? = null) {
        val bondedDevice = device ?: run {
            val address = associationAddress ?: return
            runCatching { bluetoothAdapter?.getRemoteDevice(address) }.getOrNull()
        } ?: return
        gattConnection?.close()
        _state.update {
            it.copy(
                connection = ConnectionState.CONNECTING,
                status = null,
                settings = emptyList(),
                settingsLoading = false,
                error = null,
            )
        }
        lateinit var session: GattConnection
        session = GattConnection(
            context = appContext,
            device = bondedDevice,
            listener = object : GattConnection.Listener {
                override fun onDiscoveringServices() {
                    if (gattConnection === session) _state.update { it.copy(connection = ConnectionState.DISCOVERING) }
                }

                override fun onReady() {
                    if (gattConnection !== session) return
                    _state.update { it.copy(connection = ConnectionState.READY, settingsLoading = true) }
                    syncLocationUpdates()
                }

                override fun onStatus(snapshot: FurbleProtocol.StatusSnapshot) {
                    if (gattConnection === session) _state.update { it.copy(status = snapshot) }
                }

                override fun onSettings(response: FurbleProtocol.SettingsResponse) {
                    if (gattConnection !== session) return
                    handleSettingsResponse(response)
                }

                override fun onDisconnected() {
                    if (gattConnection !== session) return
                    gattConnection = null
                    triggerController.onLinkLost()
                    locationProvider?.stop()
                    _state.update {
                        it.copy(
                            connection = if (devicePresent) ConnectionState.ASSOCIATED else ConnectionState.OUT_OF_RANGE,
                            status = null,
                            settings = emptyList(),
                            settingsLoading = false,
                        )
                    }
                }

                override fun onError(message: String) {
                    if (gattConnection === session) setError(message)
                }
            },
        )
        gattConnection = session
        session.connect()
    }

    private fun handleSettingsResponse(response: FurbleProtocol.SettingsResponse) {
        if (response.isTerminator) {
            _state.update { it.copy(settingsLoading = false) }
            return
        }
        if (response.status != FurbleProtocol.SettingsStatus.OK) {
            setError("furble rejected setting ${response.id}: status ${response.status}")
            _state.update { it.copy(settingsLoading = false) }
            return
        }
        val record = FurbleProtocol.SettingRecord(
            id = response.id,
            type = response.type,
            value = response.value,
            flags = response.flags,
        )
        _state.update { current ->
            val existing = current.settings.indexOfFirst { it.id == record.id }
            val updated = if (existing >= 0) {
                current.settings.toMutableList().also { it[existing] = record }
            } else {
                current.settings + record
            }
            current.copy(settings = updated)
        }
        if (pendingSettingId == response.id) pendingSettingId = null
    }

    private fun onFixBatch(fixes: List<FurbleProtocol.LocationFix>) {
        if (!_state.value.locationEnabled || !permissions.fineLocation ||
            !devicePresent || !_state.value.association.associated ||
            _state.value.connection != ConnectionState.READY
        ) return
        val connection = gattConnection ?: return
        fixes.forEach { connection.writeLocation(FurbleProtocol.encodeLocation(it)) }
        _state.update { it.copy(locationFixesSent = it.locationFixesSent + fixes.size) }
    }

    private fun syncLocationUpdates() {
        val shouldRun = _state.value.locationEnabled && permissions.fineLocation &&
            _state.value.association.associated && devicePresent &&
            _state.value.connection == ConnectionState.READY
        if (shouldRun) {
            locationProvider?.start(_state.value.locationIntervalSeconds)
        } else {
            locationProvider?.stop()
        }
    }

    private fun stopGattAndLocation(clearData: Boolean) {
        locationProvider?.stop()
        gattConnection?.close()
        gattConnection = null
        triggerController.onLinkLost()
        if (clearData) {
            _state.update { it.copy(status = null, settings = emptyList(), settingsLoading = false) }
        }
    }

    private fun observePresence(address: String) {
        try {
            if (Build.VERSION.SDK_INT >= 36 && associationId != null) {
                val request = ObservingDevicePresenceRequest.Builder()
                    .setAssociationId(associationId!!)
                    .build()
                companionDeviceManager.startObservingDevicePresence(request)
            } else if (Build.VERSION.SDK_INT >= 31) {
                @Suppress("DEPRECATION")
                companionDeviceManager.startObservingDevicePresence(address)
            }
        } catch (securityException: SecurityException) {
            setError("Android could not register furble presence callbacks")
        } catch (unsupported: UnsupportedOperationException) {
            setError("This phone cannot monitor furble presence")
        }
    }

    private fun setError(message: String) {
        handler.post { _state.update { it.copy(error = message) } }
    }

    @Suppress("DEPRECATION")
    private fun Intent.extractBluetoothDevice(): BluetoothDevice? {
        if (Build.VERSION.SDK_INT >= 33) {
            getParcelableExtra(CompanionDeviceManager.EXTRA_DEVICE, BluetoothDevice::class.java)?.let { return it }
            getParcelableExtra(CompanionDeviceManager.EXTRA_DEVICE, ScanResult::class.java)?.device?.let { return it }
        } else {
            getParcelableExtra<Parcelable>(CompanionDeviceManager.EXTRA_DEVICE)?.let {
                when (it) {
                    is BluetoothDevice -> return it
                    is ScanResult -> return it.device
                }
            }
        }
        return null
    }

    private fun Intent.extractAssociationInfo(): AssociationInfo? {
        if (Build.VERSION.SDK_INT < 33) return null
        return getParcelableExtra(CompanionDeviceManager.EXTRA_ASSOCIATION, AssociationInfo::class.java)
    }

    @Suppress("DEPRECATION")
    private inline fun <reified T> Intent.getParcelableExtraCompat(key: String): T? =
        if (Build.VERSION.SDK_INT >= 33) getParcelableExtra(key, T::class.java) else getParcelableExtra(key)
}
