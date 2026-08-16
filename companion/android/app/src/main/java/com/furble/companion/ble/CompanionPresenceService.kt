package com.furble.companion.ble

import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import android.companion.DevicePresenceEvent
import android.os.Build
import androidx.annotation.RequiresApi
import com.furble.companion.FurbleApplication

/**
 * A system-bound presence callback surface. It does no polling, owns no
 * notification, and never becomes a foreground service.
 */
class CompanionPresenceService : CompanionDeviceService() {
    private val repository: CompanionRepository
        get() = (application as FurbleApplication).companionRepository

    @Suppress("DEPRECATION")
    override fun onDeviceAppeared(address: String) {
        repository.onDeviceAppeared(address)
    }

    @RequiresApi(33)
    override fun onDeviceAppeared(associationInfo: AssociationInfo) {
        repository.onDeviceAppeared(associationInfo.deviceMacAddress?.toString())
    }

    @Suppress("DEPRECATION")
    override fun onDeviceDisappeared(address: String) {
        repository.onDeviceDisappeared(address)
    }

    @RequiresApi(33)
    override fun onDeviceDisappeared(associationInfo: AssociationInfo) {
        repository.onDeviceDisappeared(associationInfo.deviceMacAddress?.toString())
    }

    @RequiresApi(Build.VERSION_CODES.BAKLAVA)
    override fun onDevicePresenceEvent(event: DevicePresenceEvent) {
        when (event.event) {
            DevicePresenceEvent.EVENT_BLE_APPEARED,
            DevicePresenceEvent.EVENT_BT_CONNECTED,
            -> repository.onDeviceAppeared(null)

            DevicePresenceEvent.EVENT_BLE_DISAPPEARED,
            DevicePresenceEvent.EVENT_BT_DISCONNECTED,
            DevicePresenceEvent.EVENT_ASSOCIATION_REMOVED,
            -> repository.onDeviceDisappeared(null)
        }
    }
}
