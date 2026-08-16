package com.furble.companion.ui

import android.content.IntentSender
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.weight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.furble.companion.MainViewModel
import com.furble.companion.ble.CompanionUiState
import com.furble.companion.ble.ConnectionState
import com.furble.companion.permissions.PermissionSnapshot
import com.furble.companion.protocol.FurbleProtocol

private enum class CompanionScreen(val label: String) {
    STATUS("Status"),
    SETTINGS("Settings"),
    TRIGGER("Trigger"),
}

@Composable
fun FurbleCompanionApp(
    viewModel: MainViewModel,
    permissionSnapshot: PermissionSnapshot?,
    permissionRequestAttempted: Boolean,
    requestPermissions: () -> Unit,
    openPermissionSettings: () -> Unit,
    launchAssociationChooser: (IntentSender) -> Unit,
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val permissions = permissionSnapshot
    val chooser = state.chooserIntentSender
    LaunchedEffect(chooser) {
        if (chooser != null) {
            viewModel.clearChooserIntentSender()
            launchAssociationChooser(chooser)
        }
    }

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        if (permissions == null || !permissions.allRequired) {
            PermissionRationaleScreen(
                permissions = permissions,
                requestAttempted = permissionRequestAttempted,
                requestPermissions = requestPermissions,
                openPermissionSettings = openPermissionSettings,
            )
        } else {
            CompanionShell(
                state = state,
                viewModel = viewModel,
            )
        }
    }
}

@Composable
private fun PermissionRationaleScreen(
    permissions: PermissionSnapshot?,
    requestAttempted: Boolean,
    requestPermissions: () -> Unit,
    openPermissionSettings: () -> Unit,
) {
    val missing = permissions?.missingLabels ?: listOf("Bluetooth scanning", "Bluetooth connections", "Precise location")
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text("Permissions needed", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(12.dp))
        Text(
            "furble companion uses Bluetooth to associate and communicate with your device. " +
                "Precise location is needed by the phone location provider. The app does not scan or track until you enable GPS.",
        )
        Spacer(Modifier.height(16.dp))
        missing.forEach { Text("• $it") }
        Spacer(Modifier.height(20.dp))
        Button(onClick = requestPermissions) { Text(if (requestAttempted) "Try again" else "Continue") }
        if (requestAttempted) {
            Spacer(Modifier.height(8.dp))
            Text("If Android no longer shows a prompt, use App settings to enable the denied permission.")
            TextButton(onClick = openPermissionSettings) { Text("Open app settings") }
        }
    }
}

@Composable
private fun CompanionShell(
    state: CompanionUiState,
    viewModel: MainViewModel,
) {
    var selectedScreen by rememberSaveable { mutableStateOf(CompanionScreen.STATUS) }
    Scaffold(
        topBar = { TopAppBar(title = { Text("furble companion") }) },
        bottomBar = {
            NavigationBar {
                CompanionScreen.entries.forEach { screen ->
                    NavigationBarItem(
                        selected = selectedScreen == screen,
                        onClick = { selectedScreen = screen },
                        icon = { Text(screen.label.take(1)) },
                        label = { Text(screen.label) },
                    )
                }
            }
        },
    ) { padding ->
        Column(Modifier.fillMaxSize().padding(padding)) {
            state.error?.let { message ->
                Card(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp)) {
                    Row(Modifier.fillMaxWidth().padding(12.dp)) {
                        Text(message, Modifier.weight(1f), color = MaterialTheme.colorScheme.error)
                        TextButton(onClick = viewModel::clearError) { Text("Dismiss") }
                    }
                }
            }
            when (selectedScreen) {
                CompanionScreen.STATUS -> StatusScreen(
                    state = state,
                    onPair = viewModel::startAssociation,
                    onConnect = viewModel::connectAssociatedDevice,
                    onLocationEnabled = viewModel::setLocationEnabled,
                    onLocationInterval = viewModel::setLocationInterval,
                )
                CompanionScreen.SETTINGS -> SettingsScreen(
                    state = state,
                    onRefresh = viewModel::requestSettings,
                    onBooleanChange = viewModel::setBooleanSetting,
                    onUint8Change = viewModel::setUint8Setting,
                )
                CompanionScreen.TRIGGER -> TriggerScreen(
                    state = state,
                    onPressShutter = viewModel::pressShutter,
                    onReleaseShutter = viewModel::releaseShutter,
                    onPressFocus = viewModel::pressFocus,
                    onReleaseFocus = viewModel::releaseFocus,
                    onTimedShutter = viewModel::timedShutter,
                    onReleaseAll = viewModel::releaseAllTriggers,
                )
            }
        }
    }
}

@Composable
private fun StatusScreen(
    state: CompanionUiState,
    onPair: () -> Unit,
    onConnect: () -> Unit,
    onLocationEnabled: (Boolean) -> Unit,
    onLocationInterval: (Int) -> Unit,
) {
    var intervalText by remember(state.locationIntervalSeconds) {
        mutableStateOf(state.locationIntervalSeconds.toString())
    }
    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(horizontal = 12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item {
            Spacer(Modifier.height(4.dp))
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text("Association", style = MaterialTheme.typography.titleMedium)
                    Text(
                        when {
                            !state.association.associated -> "No furble is associated"
                            state.association.deviceName.isNullOrBlank() -> "Associated device ${state.association.address ?: "unknown"}"
                            else -> state.association.deviceName ?: "Associated furble"
                        },
                    )
                    Text("Link: ${state.connection.displayName()}")
                    Text("Bonded: ${if (state.association.bonded) "yes" else "no"}")
                    Spacer(Modifier.height(8.dp))
                    if (!state.association.associated) {
                        Button(onClick = onPair, enabled = !state.pairingInProgress) {
                            Text(if (state.pairingInProgress) "Pairing…" else "Pair furble")
                        }
                    } else if (state.connection != ConnectionState.READY &&
                        state.connection != ConnectionState.CONNECTING &&
                        state.connection != ConnectionState.DISCOVERING &&
                        state.connection != ConnectionState.BONDING
                    ) {
                        Button(onClick = onConnect) { Text("Connect") }
                    }
                }
            }
        }
        item {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text("Phone GPS", style = MaterialTheme.typography.titleMedium)
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("Push fixes to furble")
                        Switch(checked = state.locationEnabled, onCheckedChange = onLocationEnabled)
                    }
                    Text("Balanced power updates are batched. Default interval is 10 seconds.")
                    Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        OutlinedTextField(
                            value = intervalText,
                            onValueChange = { intervalText = it.filter(Char::isDigit).take(4) },
                            label = { Text("Interval in seconds") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            singleLine = true,
                            modifier = Modifier.weight(1f),
                        )
                        Spacer(Modifier.width(8.dp))
                        OutlinedButton(
                            onClick = { intervalText.toIntOrNull()?.let(onLocationInterval) },
                        ) { Text("Apply") }
                    }
                    if (state.locationEnabled) Text("Fixes sent this session: ${state.locationFixesSent}")
                }
            }
        }
        item {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text("furble status", style = MaterialTheme.typography.titleMedium)
                    val status = state.status
                    if (status == null) {
                        Text("Waiting for a status notification")
                    } else {
                        InfoRow("Battery", if (status.batteryPercent == 255) "Unknown" else "${status.batteryPercent}%")
                        InfoRow("Battery voltage", "${status.batteryMv} mV")
                        InfoRow("Connection", state.connection.displayName())
                        InfoRow("GPS fix source", status.gpsSource.displayGpsSource())
                        InfoRow("GPS satellites", status.gpsSatellites.toString())
                        InfoRow("Intervalometer", "state ${status.intervalometerState}, ${status.intervalometerRemaining.remainingLabel()}")
                        InfoRow("Cameras", "${status.cameraConnected}/${status.cameraTotal} connected")
                        if (status.charging || status.externalPower) {
                            InfoRow("Power", listOfNotNull(if (status.charging) "charging" else null, if (status.externalPower) "external" else null).joinToString(", "))
                        }
                    }
                }
            }
        }
        item { Spacer(Modifier.height(8.dp)) }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label)
        Text(value, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun SettingsScreen(
    state: CompanionUiState,
    onRefresh: () -> Unit,
    onBooleanChange: (FurbleProtocol.SettingRecord, Boolean) -> Unit,
    onUint8Change: (FurbleProtocol.SettingRecord, Int) -> Unit,
) {
    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(horizontal = 12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item {
            Row(Modifier.fillMaxWidth().padding(top = 8.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Device settings", style = MaterialTheme.typography.headlineSmall)
                Button(onClick = onRefresh, enabled = state.connection == ConnectionState.READY) { Text("Refresh") }
            }
            Text("Settings are read and written as one protocol TLV request per operation.")
        }
        if (state.settingsLoading) item { Text("Reading settings…") }
        if (state.settings.isEmpty() && !state.settingsLoading) {
            item { Text("No settings have been reported yet.") }
        }
        items(state.settings, key = { it.id }) { record ->
            SettingRow(record, onBooleanChange, onUint8Change)
        }
        item { Spacer(Modifier.height(8.dp)) }
    }
}

@Composable
private fun SettingRow(
    record: FurbleProtocol.SettingRecord,
    onBooleanChange: (FurbleProtocol.SettingRecord, Boolean) -> Unit,
    onUint8Change: (FurbleProtocol.SettingRecord, Int) -> Unit,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Text(record.name, style = MaterialTheme.typography.titleMedium)
            Text("Wire id ${record.id}, type ${record.type}")
            if (record.needsRestart) Text("Takes effect after restart", color = MaterialTheme.colorScheme.tertiary)
            when (record.type) {
                FurbleProtocol.SettingType.BOOL -> {
                    val checked = record.value.firstOrNull()?.toInt()?.and(0xFF) == 1
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text(if (checked) "Enabled" else "Disabled")
                        Switch(
                            checked = checked,
                            onCheckedChange = { onBooleanChange(record, it) },
                            enabled = record.value.size == 1,
                        )
                    }
                }
                FurbleProtocol.SettingType.UINT8 -> {
                    var valueText by remember(record.id, record.value.toList()) {
                        mutableStateOf(record.value.firstOrNull()?.toInt()?.and(0xFF)?.toString() ?: "")
                    }
                    Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        OutlinedTextField(
                            value = valueText,
                            onValueChange = { valueText = it.filter(Char::isDigit).take(3) },
                            label = { Text("Value") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            singleLine = true,
                            modifier = Modifier.weight(1f),
                        )
                        Spacer(Modifier.width(8.dp))
                        Button(onClick = { valueText.toIntOrNull()?.let { onUint8Change(record, it) } }) {
                            Text("Save")
                        }
                    }
                }
                else -> Text("${record.displayValue()} (read-only in this app)")
            }
        }
    }
}

@Composable
private fun TriggerScreen(
    state: CompanionUiState,
    onPressShutter: () -> Unit,
    onReleaseShutter: () -> Unit,
    onPressFocus: () -> Unit,
    onReleaseFocus: () -> Unit,
    onTimedShutter: (Int) -> Unit,
    onReleaseAll: () -> Unit,
) {
    var holdText by rememberSaveable { mutableStateOf("1000") }
    val ready = state.connection == ConnectionState.READY
    DisposableEffect(Unit) {
        onDispose { onReleaseAll() }
    }
    LazyColumn(Modifier.fillMaxSize().padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text("Remote trigger", style = MaterialTheme.typography.headlineSmall)
                    Text("Controls are available only while furble reports an active bonded link.")
                    Spacer(Modifier.height(12.dp))
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = onPressShutter, enabled = ready && !state.shutterHeld, modifier = Modifier.weight(1f)) {
                            Text("Press shutter")
                        }
                        OutlinedButton(onClick = onReleaseShutter, enabled = state.shutterHeld, modifier = Modifier.weight(1f)) {
                            Text("Release shutter")
                        }
                    }
                    Text(if (state.shutterHeld) "Shutter hold is alive" else "Shutter is released")
                }
            }
        }
        item {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text("Focus", style = MaterialTheme.typography.titleMedium)
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = onPressFocus, enabled = ready && !state.focusHeld, modifier = Modifier.weight(1f)) {
                            Text("Press focus")
                        }
                        OutlinedButton(onClick = onReleaseFocus, enabled = state.focusHeld, modifier = Modifier.weight(1f)) {
                            Text("Release focus")
                        }
                    }
                }
            }
        }
        item {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text("Timed shutter", style = MaterialTheme.typography.titleMedium)
                    Text("The device measures hold_ms and performs the release locally.")
                    Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        OutlinedTextField(
                            value = holdText,
                            onValueChange = { holdText = it.filter(Char::isDigit).take(5) },
                            label = { Text("Hold in ms") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            singleLine = true,
                            modifier = Modifier.weight(1f),
                        )
                        Spacer(Modifier.width(8.dp))
                        Button(
                            onClick = { holdText.toIntOrNull()?.let(onTimedShutter) },
                            enabled = ready,
                        ) { Text("Timed press") }
                    }
                    HorizontalDivider(Modifier.padding(vertical = 12.dp))
                    Text("While a manual hold is active, the app repeats the held press every ${com.furble.companion.ble.DeadManTriggerController.HEARTBEAT_INTERVAL_MS / 1000} second. Leaving the app sends release; if the process or link disappears, furble's dead-man timeout releases the output.")
                }
            }
        }
    }
}

private fun ConnectionState.displayName(): String = when (this) {
    ConnectionState.NO_ASSOCIATION -> "Not associated"
    ConnectionState.ASSOCIATED -> "Associated"
    ConnectionState.OUT_OF_RANGE -> "Out of range"
    ConnectionState.BONDING -> "Bonding"
    ConnectionState.CONNECTING -> "Connecting"
    ConnectionState.DISCOVERING -> "Discovering services"
    ConnectionState.READY -> "Connected"
    ConnectionState.PERMISSION_DENIED -> "Permission denied"
    ConnectionState.ERROR -> "Error"
}

private fun Int.displayGpsSource(): String = when (this) {
    1 -> "Wired UART GPS"
    2 -> "Phone companion"
    else -> "No fix"
}

private fun Int.remainingLabel(): String = if (this == 0xFFFF) "infinite shots" else "$this shots remaining"
