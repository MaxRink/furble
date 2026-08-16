package com.furble.companion.location

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.os.Build
import android.os.Looper
import androidx.core.content.ContextCompat
import com.furble.companion.protocol.FurbleProtocol
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationCallback
import com.google.android.gms.location.LocationRequest
import com.google.android.gms.location.LocationResult
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import java.time.Instant
import java.time.ZoneOffset
import kotlin.math.roundToInt

/** Balanced-power, batched location updates with no background service. */
class FusedLocationProvider(
    context: Context,
    private val onFixBatch: (List<FurbleProtocol.LocationFix>) -> Unit,
    private val onError: (String) -> Unit,
) {
    private val appContext = context.applicationContext
    private val client: FusedLocationProviderClient =
        LocationServices.getFusedLocationProviderClient(appContext)
    private var callback: LocationCallback? = null

    @SuppressLint("MissingPermission")
    fun start(intervalSeconds: Int) {
        stop()
        if (ContextCompat.checkSelfPermission(
                appContext,
                Manifest.permission.ACCESS_FINE_LOCATION,
            ) != android.content.pm.PackageManager.PERMISSION_GRANTED
        ) {
            onError("Precise location permission is required for GPS updates")
            return
        }

        val intervalMs = intervalSeconds.coerceIn(1, 3600).toLong() * 1_000L
        val request = LocationRequest.Builder(
            Priority.PRIORITY_BALANCED_POWER_ACCURACY,
            intervalMs,
        )
            .setMinUpdateIntervalMillis(intervalMs)
            .setMaxUpdateDelayMillis(intervalMs * 3L)
            .setWaitForAccurateLocation(false)
            .build()
        val newCallback = object : LocationCallback() {
            override fun onLocationResult(result: LocationResult) {
                val fixes = result.locations.mapNotNull(::toFix)
                if (fixes.isNotEmpty()) onFixBatch(fixes)
            }
        }
        callback = newCallback
        try {
            client.requestLocationUpdates(request, newCallback, Looper.getMainLooper())
                .addOnFailureListener { onError("Location updates could not start: ${it.message ?: "unknown error"}") }
        } catch (securityException: SecurityException) {
            callback = null
            onError("Precise location permission was revoked")
        }
    }

    fun stop() {
        callback?.let(client::removeLocationUpdates)
        callback = null
    }

    private fun toFix(location: Location): FurbleProtocol.LocationFix? {
        val positionValid = location.latitude.isFinite() && location.longitude.isFinite()
        val timeValid = location.time > 0L
        if (!positionValid && !timeValid) return null

        val nowElapsedNanos = android.os.SystemClock.elapsedRealtimeNanos()
        val ageMs = if (location.elapsedRealtimeNanos > 0L &&
            nowElapsedNanos >= location.elapsedRealtimeNanos
        ) {
            (nowElapsedNanos - location.elapsedRealtimeNanos) / 1_000_000L
        } else {
            (System.currentTimeMillis() - location.time).coerceAtLeast(0L)
        }
        val utc = if (timeValid) Instant.ofEpochMilli(location.time).atZone(ZoneOffset.UTC) else null
        val extras = location.extras
        val satellites = extras?.getInt("satellites", 0) ?: 0
        val accuracyMeters = if (location.hasAccuracy() && location.accuracy.isFinite()) {
            location.accuracy.roundToInt().coerceIn(0, 254)
        } else {
            null
        }

        return FurbleProtocol.LocationFix(
            positionValid = positionValid,
            timeValid = timeValid,
            altitudeValid = location.hasAltitude() && location.altitude.isFinite(),
            satellites = satellites,
            accuracyMeters = accuracyMeters,
            latitude = location.latitude,
            longitude = location.longitude,
            altitude = if (location.hasAltitude()) location.altitude else 0.0,
            year = utc?.year ?: 0,
            month = utc?.monthValue ?: 0,
            day = utc?.dayOfMonth ?: 0,
            hour = utc?.hour ?: 0,
            minute = utc?.minute ?: 0,
            second = utc?.second ?: 0,
            centisecond = if (timeValid) ((location.time % 1000L + 1000L) % 1000L / 10L).toInt() else 0,
            ageMs = ageMs.coerceIn(0L, 0xFFFF_FFFFL),
        )
    }
}
