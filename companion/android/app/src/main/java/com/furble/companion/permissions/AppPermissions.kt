package com.furble.companion.permissions

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat

data class PermissionSnapshot(
    val bluetoothScan: Boolean,
    val bluetoothConnect: Boolean,
    val fineLocation: Boolean,
) {
    val bluetoothReady: Boolean
        get() = bluetoothScan && bluetoothConnect

    val allRequired: Boolean
        get() = bluetoothReady && fineLocation

    val missingLabels: List<String>
        get() = buildList {
            if (!bluetoothScan) add("Bluetooth scanning")
            if (!bluetoothConnect) add("Bluetooth connections")
            if (!fineLocation) add("Precise location")
        }
}

object AppPermissions {
    fun requestablePermissions(): Array<String> = buildList {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        add(Manifest.permission.ACCESS_FINE_LOCATION)
    }.toTypedArray()

    fun snapshot(context: Context): PermissionSnapshot = PermissionSnapshot(
        bluetoothScan = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || context.has(Manifest.permission.BLUETOOTH_SCAN),
        bluetoothConnect = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || context.has(Manifest.permission.BLUETOOTH_CONNECT),
        fineLocation = context.has(Manifest.permission.ACCESS_FINE_LOCATION),
    )

    private fun Context.has(permission: String): Boolean =
        ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
}
