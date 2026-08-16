package com.furble.companion.ble

import android.os.Handler
import android.os.Looper
import com.furble.companion.protocol.FurbleProtocol

/**
 * Keeps a companion press alive only while the user is holding it. The
 * protocol has no separate heartbeat opcode, so a repeated press packet is
 * the app-side keep-alive. The firmware dead-man timeout must be longer than
 * this interval; a missing packet or a dropped link then causes firmware to
 * release the camera output.
 */
class DeadManTriggerController(
    private val send: (operation: Int, holdMs: Int) -> Unit,
    private val onActiveChanged: (shutterHeld: Boolean, focusHeld: Boolean) -> Unit,
) {
    companion object {
        const val HEARTBEAT_INTERVAL_MS = 1_000L
    }

    private val handler = Handler(Looper.getMainLooper())
    private var shutterHeld = false
    private var focusHeld = false
    private var timedShutterGeneration = 0

    private val shutterHeartbeat = object : Runnable {
        override fun run() {
            if (!shutterHeld) return
            send(FurbleProtocol.TriggerOperation.SHUTTER_PRESS, 0)
            handler.postDelayed(this, HEARTBEAT_INTERVAL_MS)
        }
    }

    private val focusHeartbeat = object : Runnable {
        override fun run() {
            if (!focusHeld) return
            send(FurbleProtocol.TriggerOperation.FOCUS_PRESS, 0)
            handler.postDelayed(this, HEARTBEAT_INTERVAL_MS)
        }
    }

    fun pressShutter() {
        if (shutterHeld) return
        shutterHeld = true
        send(FurbleProtocol.TriggerOperation.SHUTTER_PRESS, 0)
        handler.removeCallbacks(shutterHeartbeat)
        handler.postDelayed(shutterHeartbeat, HEARTBEAT_INTERVAL_MS)
        publishState()
    }

    fun releaseShutter() {
        if (!shutterHeld) return
        shutterHeld = false
        handler.removeCallbacks(shutterHeartbeat)
        send(FurbleProtocol.TriggerOperation.SHUTTER_RELEASE, 0)
        publishState()
    }

    fun pressFocus() {
        if (focusHeld) return
        focusHeld = true
        send(FurbleProtocol.TriggerOperation.FOCUS_PRESS, 0)
        handler.removeCallbacks(focusHeartbeat)
        handler.postDelayed(focusHeartbeat, HEARTBEAT_INTERVAL_MS)
        publishState()
    }

    fun releaseFocus() {
        if (!focusHeld) return
        focusHeld = false
        handler.removeCallbacks(focusHeartbeat)
        send(FurbleProtocol.TriggerOperation.FOCUS_RELEASE, 0)
        publishState()
    }

    fun timedShutter(holdMs: Int) {
        require(holdMs in 1..0xFFFF)
        timedShutterGeneration += 1
        val generation = timedShutterGeneration
        shutterHeld = true
        handler.removeCallbacks(shutterHeartbeat)
        send(FurbleProtocol.TriggerOperation.TIMED_SHUTTER, holdMs)
        publishState()
        handler.postDelayed({
            if (generation == timedShutterGeneration) {
                shutterHeld = false
                publishState()
            }
        }, holdMs.toLong())
    }

    /** Release while the process is still able to write, such as Activity stop. */
    fun releaseAll() {
        timedShutterGeneration += 1
        if (shutterHeld) send(FurbleProtocol.TriggerOperation.SHUTTER_RELEASE, 0)
        if (focusHeld) send(FurbleProtocol.TriggerOperation.FOCUS_RELEASE, 0)
        shutterHeld = false
        focusHeld = false
        handler.removeCallbacks(shutterHeartbeat)
        handler.removeCallbacks(focusHeartbeat)
        publishState()
    }

    /** The link is already gone, so firmware's dead-man release is authoritative. */
    fun onLinkLost() {
        timedShutterGeneration += 1
        shutterHeld = false
        focusHeld = false
        handler.removeCallbacks(shutterHeartbeat)
        handler.removeCallbacks(focusHeartbeat)
        publishState()
    }

    private fun publishState() = onActiveChanged(shutterHeld, focusHeld)
}
