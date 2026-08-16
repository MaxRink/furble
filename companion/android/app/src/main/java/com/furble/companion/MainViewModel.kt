package com.furble.companion

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import com.furble.companion.ble.CompanionRepository
import com.furble.companion.protocol.FurbleProtocol

class MainViewModel(application: Application) : AndroidViewModel(application) {
    private val repository: CompanionRepository =
        (application as FurbleApplication).companionRepository

    val state = repository.state

    fun onPermissionsChanged() = repository.onPermissionsChanged()
    fun startAssociation() = repository.startAssociation()
    fun onAssociationChooserResult(resultCode: Int, data: android.content.Intent?) =
        repository.onAssociationChooserResult(resultCode, data)
    fun clearChooserIntentSender() = repository.clearChooserIntentSender()
    fun refreshAssociation() = repository.refreshAssociation()
    fun connectAssociatedDevice() = repository.connectAssociatedDevice()
    fun setLocationEnabled(enabled: Boolean) = repository.setLocationEnabled(enabled)
    fun setLocationInterval(seconds: Int) = repository.setLocationInterval(seconds)
    fun requestSettings() = repository.requestSettings()
    fun setBooleanSetting(record: FurbleProtocol.SettingRecord, value: Boolean) =
        repository.setBooleanSetting(record, value)
    fun setUint8Setting(record: FurbleProtocol.SettingRecord, value: Int) =
        repository.setUint8Setting(record, value)
    fun pressShutter() = repository.pressShutter()
    fun releaseShutter() = repository.releaseShutter()
    fun pressFocus() = repository.pressFocus()
    fun releaseFocus() = repository.releaseFocus()
    fun timedShutter(holdMs: Int) = repository.timedShutter(holdMs)
    fun releaseAllTriggers() = repository.releaseAllTriggers()
    fun clearError() = repository.clearError()

    override fun onCleared() {
        repository.releaseAllTriggers()
        super.onCleared()
    }
}
