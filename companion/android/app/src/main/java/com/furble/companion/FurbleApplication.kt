package com.furble.companion

import android.app.Application
import com.furble.companion.ble.CompanionRepository

class FurbleApplication : Application() {
    lateinit var companionRepository: CompanionRepository
        private set

    override fun onCreate() {
        super.onCreate()
        companionRepository = CompanionRepository(this)
    }
}
