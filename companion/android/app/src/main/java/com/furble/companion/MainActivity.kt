package com.furble.companion

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.furble.companion.permissions.AppPermissions
import com.furble.companion.permissions.PermissionSnapshot
import com.furble.companion.ui.FurbleCompanionApp

class MainActivity : ComponentActivity() {
    private val viewModel: MainViewModel by viewModels()
    private var permissionSnapshot by mutableStateOf<PermissionSnapshot?>(null)
    private var permissionRequestAttempted by mutableStateOf(false)

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) {
        permissionSnapshot = AppPermissions.snapshot(this)
        viewModel.onPermissionsChanged()
    }

    private val chooserLauncher = registerForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult(),
    ) { result ->
        viewModel.onAssociationChooserResult(result.resultCode, result.data)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        permissionSnapshot = AppPermissions.snapshot(this)
        viewModel.onPermissionsChanged()
        setContent {
            FurbleCompanionApp(
                viewModel = viewModel,
                permissionSnapshot = permissionSnapshot,
                permissionRequestAttempted = permissionRequestAttempted,
                requestPermissions = {
                    permissionRequestAttempted = true
                    permissionLauncher.launch(AppPermissions.requestablePermissions())
                },
                openPermissionSettings = ::openPermissionSettings,
                launchAssociationChooser = { sender ->
                    chooserLauncher.launch(IntentSenderRequest.Builder(sender).build())
                },
            )
        }
    }

    override fun onResume() {
        super.onResume()
        permissionSnapshot = AppPermissions.snapshot(this)
        viewModel.onPermissionsChanged()
    }

    override fun onStop() {
        // A held trigger is released before the Activity stops. If the process
        // is killed before this write, furble's dead-man timeout remains the
        // final safety mechanism.
        viewModel.releaseAllTriggers()
        super.onStop()
    }

    private fun openPermissionSettings() {
        startActivity(
            Intent(
                Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.parse("package:$packageName"),
            ),
        )
    }
}
