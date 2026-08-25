@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.furble.companion.ui

import android.content.IntentSender
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.AlertDialog
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.furble.companion.MainViewModel
import com.furble.companion.ble.CompanionUiState
import com.furble.companion.ble.ConnectionState
import com.furble.companion.ble.AuthState
import com.furble.companion.ble.protectedReady
import com.furble.companion.permissions.PermissionSnapshot
import com.furble.companion.protocol.FurbleProtocol
import com.furble.companion.protocol.SettingEditorKind
import com.furble.companion.protocol.SettingMetadata
import kotlin.math.roundToInt

private enum class CompanionScreen(val label: String, val icon: ImageVector) {
    STATUS("Status", Icons.Filled.Info),
    SETTINGS("Settings", Icons.Filled.Settings),
    TRIGGER("Trigger", Icons.Filled.PlayArrow),
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
    val visibleScreens = CompanionScreen.entries.filter { screen ->
        screen != CompanionScreen.SETTINGS || state.settingsSupported && state.protectedReady()
    }
    LaunchedEffect(state.settingsSupported, state.protectedReady()) {
        if ((!state.settingsSupported || !state.protectedReady()) && selectedScreen == CompanionScreen.SETTINGS) {
            selectedScreen = CompanionScreen.STATUS
        }
    }
    Scaffold(
        topBar = { TopAppBar(title = { Text("furble companion") }) },
        bottomBar = {
            NavigationBar {
                visibleScreens.forEach { screen ->
                    NavigationBarItem(
                        selected = selectedScreen == screen,
                        onClick = { selectedScreen = screen },
                        icon = { Icon(screen.icon, contentDescription = screen.label) },
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
                    onAuthenticate = viewModel::authenticate,
                    onForgetPassword = viewModel::forgetPassword,
                )
                CompanionScreen.SETTINGS -> SettingsScreen(
                    state = state,
                    onRefresh = viewModel::requestSettings,
                    onChange = viewModel::requestSettingChange,
                    onConfirmDangerous = viewModel::confirmPendingSetting,
                    onCancelDangerous = viewModel::cancelPendingSetting,
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
    onAuthenticate: (String) -> Unit,
    onForgetPassword: () -> Unit,
) {
    var intervalText by remember(state.locationIntervalSeconds) {
        mutableStateOf(state.locationIntervalSeconds.toString())
    }
    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(horizontal = 12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        if (state.authSupported) item {
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
            AuthenticationCard(
                state = state,
                onAuthenticate = onAuthenticate,
                onForgetPassword = onForgetPassword,
            )
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
private fun AuthenticationCard(
    state: CompanionUiState,
    onAuthenticate: (String) -> Unit,
    onForgetPassword: () -> Unit,
) {
    var password by remember { mutableStateOf("") }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Companion password", style = MaterialTheme.typography.titleMedium)
            Text(state.auth.displayName())
            Text(
                "The password is optional on furble. It is sent only as an HMAC challenge response and is stored encrypted with Android Keystore after acceptance.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            OutlinedTextField(
                value = password,
                onValueChange = { password = FurbleProtocol.truncateUtf8(it) },
                label = { Text("Password") },
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                singleLine = true,
                enabled = state.connection == ConnectionState.READY && state.auth != AuthState.DROPPED,
                modifier = Modifier.fillMaxWidth(),
            )
            Text("${password.toByteArray(Charsets.UTF_8).size}/${FurbleProtocol.COMPANION_PASSWORD_MAX} UTF-8 bytes")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        onAuthenticate(password)
                        password = ""
                    },
                    enabled = password.isNotEmpty() && state.connection == ConnectionState.READY &&
                        state.auth != AuthState.AUTHENTICATING && state.auth != AuthState.DROPPED,
                ) { Text("Authenticate") }
                if (state.storedPassword) {
                    OutlinedButton(onClick = onForgetPassword) { Text("Forget") }
                }
            }
        }
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
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
    onConfirmDangerous: () -> Unit,
    onCancelDangerous: () -> Unit,
) {
    var searchText by rememberSaveable { mutableStateOf("") }
    val filteredSettings = state.settings.filter { record ->
        val query = searchText.trim()
        query.isEmpty() || record.name.contains(query, ignoreCase = true) ||
            record.metadata?.key?.contains(query, ignoreCase = true) == true ||
            record.id.toString() == query
    }
    val groupedSettings = filteredSettings.groupBy { it.metadata?.group ?: "Other" }

    state.pendingSettingConfirmation?.let { pending ->
        AlertDialog(
            onDismissRequest = onCancelDangerous,
            title = { Text("Confirm risky change") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(pending.record.name)
                    Text(
                        pending.record.dangerousConsequence
                            ?: "This setting can affect the active companion link.",
                    )
                    Text("The write is sent only after you confirm.")
                }
            },
            confirmButton = { TextButton(onClick = onConfirmDangerous) { Text("Write setting") } },
            dismissButton = { TextButton(onClick = onCancelDangerous) { Text("Cancel") } },
        )
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(horizontal = 12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item {
            Row(Modifier.fillMaxWidth().padding(top = 8.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Device settings", style = MaterialTheme.typography.headlineSmall)
                Button(onClick = onRefresh, enabled = state.connection == ConnectionState.READY) { Text("Refresh") }
            }
            Text("Settings use the firmware wire_id table and one TLV request per operation.")
            OutlinedTextField(
                value = searchText,
                onValueChange = { searchText = it },
                label = { Text("Search settings") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
        }
        if (state.settingsLoading) item { Text("Reading settings…") }
        if (state.settings.isEmpty() && !state.settingsLoading) {
            item { Text("No settings have been reported yet.") }
        }
        if (filteredSettings.isEmpty() && state.settings.isNotEmpty()) {
            item { Text("No settings match the search.") }
        }
        groupedSettings.forEach { (group, records) ->
            item(key = "settings-group-$group") {
                Text(group, style = MaterialTheme.typography.titleMedium)
            }
            items(records, key = { it.id }) { record ->
                SettingRow(record, onChange)
            }
        }
        item { Spacer(Modifier.height(8.dp)) }
    }
}

@Composable
private fun SettingRow(
    record: FurbleProtocol.SettingRecord,
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
) {
    val metadata = record.metadata
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Text(record.name, style = MaterialTheme.typography.titleMedium)
            Text("Wire id ${record.id}, ${recordTypeLabel(record.type)}")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (record.needsRestart) {
                    Text("Restart required", color = MaterialTheme.colorScheme.tertiary)
                } else {
                    Text("Applies immediately", color = MaterialTheme.colorScheme.primary)
                }
                if (record.isDangerous) {
                    Text("Confirm before write", color = MaterialTheme.colorScheme.error)
                }
            }
            when {
                metadata == null || metadata.wireType != record.type || !record.editable -> {
                    Text("${record.displayValue()} (read-only)")
                }
                else -> when (metadata.editor) {
                    SettingEditorKind.SWITCH -> BooleanEditor(record, onChange)
                    SettingEditorKind.ENUM -> EnumEditor(record, metadata, onChange)
                    SettingEditorKind.RANGE -> RangeEditor(record, metadata, onChange)
                    SettingEditorKind.UINT32_STEPPER -> Uint32Stepper(record, metadata, onChange)
                    SettingEditorKind.THEME -> ThemeEditor(record, metadata, onChange)
                    SettingEditorKind.INTERVAL -> IntervalEditor(record, onChange)
                    SettingEditorKind.READ_ONLY -> Text("${record.displayValue()} (read-only)")
                }
            }
        }
    }
}

@Composable
private fun BooleanEditor(
    record: FurbleProtocol.SettingRecord,
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
) {
    val checked = record.value.firstOrNull()?.toInt()?.and(0xFF) == 1
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(if (checked) "Enabled" else "Disabled")
        Switch(
            checked = checked,
            onCheckedChange = { onChange(record, byteArrayOf(if (it) 1 else 0)) },
        )
    }
}

@Composable
private fun EnumEditor(
    record: FurbleProtocol.SettingRecord,
    metadata: SettingMetadata,
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
) {
    val rawValue = numericSettingValue(record)
    val selected = metadata.options.firstOrNull { it.value.toLong() == rawValue }
    var expanded by remember(record.id, record.value.toList()) { mutableStateOf(false) }
    Box(Modifier.fillMaxWidth()) {
        OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
            Text(selected?.label ?: rawValue?.toString() ?: "Invalid value")
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            metadata.options.forEach { option ->
                DropdownMenuItem(
                    text = { Text(option.label) },
                    onClick = {
                        expanded = false
                        encodeNumericSetting(record.type, option.value.toLong())?.let {
                            onChange(record, it)
                        }
                    },
                )
            }
        }
    }
}

@Composable
private fun ThemeEditor(
    record: FurbleProtocol.SettingRecord,
    metadata: SettingMetadata,
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
) {
    val themes = metadata.stringOptions
    val current = record.value.toString(Charsets.UTF_8)
    var expanded by remember(record.id, record.value.toList()) { mutableStateOf(false) }
    Box(Modifier.fillMaxWidth()) {
        OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
            Text(if (current in themes) current else "Unknown theme: $current")
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            themes.forEach { theme ->
                DropdownMenuItem(
                    text = { Text(theme) },
                    onClick = {
                        expanded = false
                        onChange(record, theme.toByteArray(Charsets.UTF_8))
                    },
                )
            }
        }
    }
}

@Composable
private fun RangeEditor(
    record: FurbleProtocol.SettingRecord,
    metadata: SettingMetadata,
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
) {
    val range = metadata.range ?: return
    val initial = numericSettingValue(record)?.toInt()?.coerceIn(range.min, range.max) ?: range.min
    var current by remember(record.id, record.value.toList()) { mutableStateOf(initial.toFloat()) }
    val steps = ((range.max - range.min) / range.step - 1).coerceAtLeast(0)
    val label = metadata.options.firstOrNull { it.value == current.roundToInt() }?.label
        ?: buildString {
            append(current.roundToInt())
            if (!range.unit.isNullOrBlank() && range.unit != "mask" && range.unit != "timeout") {
                append(" ")
                append(range.unit)
            }
        }
    Column {
        Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Slider(
            value = current,
            onValueChange = { value ->
                val snapped = ((value - range.min) / range.step).roundToInt() * range.step + range.min
                current = snapped.coerceIn(range.min, range.max).toFloat()
            },
            valueRange = range.min.toFloat()..range.max.toFloat(),
            steps = steps,
            onValueChangeFinished = {
                if (record.type == FurbleProtocol.SettingType.UINT8) {
                    onChange(record, byteArrayOf(current.roundToInt().toByte()))
                }
            },
        )
    }
}

@Composable
private fun Uint32Stepper(
    record: FurbleProtocol.SettingRecord,
    metadata: SettingMetadata,
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
) {
    val options = metadata.options
    val initial = numericSettingValue(record)?.toInt()
    var index by remember(record.id, record.value.toList()) {
        mutableStateOf(options.indexOfFirst { it.value == initial }.coerceAtLeast(0))
    }
    val selected = options.getOrNull(index)
    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        OutlinedButton(
            onClick = {
                if (index > 0) {
                    index -= 1
                    encodeNumericSetting(record.type, options[index].value.toLong())?.let {
                        onChange(record, it)
                    }
                }
            },
            enabled = index > 0,
        ) { Text("−") }
        Text(selected?.label ?: numericSettingValue(record)?.toString() ?: "Invalid", Modifier.weight(1f))
        OutlinedButton(
            onClick = {
                if (index < options.lastIndex) {
                    index += 1
                    encodeNumericSetting(record.type, options[index].value.toLong())?.let {
                        onChange(record, it)
                    }
                }
            },
            enabled = index < options.lastIndex,
        ) { Text("+") }
    }
}

@Composable
private fun IntervalEditor(
    record: FurbleProtocol.SettingRecord,
    onChange: (FurbleProtocol.SettingRecord, ByteArray) -> Unit,
) {
    val decoded = FurbleProtocol.decodeInterval(record.value)
    if (decoded == null) {
        Text("${record.displayValue()} (invalid interval blob)")
        return
    }
    var count by remember(record.id, record.value.toList()) { mutableStateOf(decoded.count) }
    var delay by remember(record.id, record.value.toList()) { mutableStateOf(decoded.delay) }
    var shutter by remember(record.id, record.value.toList()) { mutableStateOf(decoded.shutter) }
    var wait by remember(record.id, record.value.toList()) { mutableStateOf(decoded.wait) }

    IntervalPartEditor("Count", count) { count = it }
    IntervalPartEditor("Delay", delay) { delay = it }
    IntervalPartEditor("Shutter", shutter) { shutter = it }
    IntervalPartEditor("Wait", wait) { wait = it }
    Button(
        onClick = {
            runCatching {
                FurbleProtocol.encodeInterval(FurbleProtocol.IntervalSetting(count, delay, shutter, wait))
            }.getOrNull()?.let { onChange(record, it) }
        },
        modifier = Modifier.fillMaxWidth(),
    ) { Text("Save interval") }
}

@Composable
private fun IntervalPartEditor(
    label: String,
    part: FurbleProtocol.IntervalPart,
    onChange: (FurbleProtocol.IntervalPart) -> Unit,
) {
    val units = listOf("Value", "Infinite", "ms", "sec", "min")
    var valueText by remember(label, part) { mutableStateOf(part.value.toString()) }
    var expanded by remember(label, part) { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        OutlinedTextField(
            value = valueText,
            onValueChange = { text ->
                valueText = text.filter(Char::isDigit).take(5)
                valueText.toIntOrNull()?.let { onChange(part.copy(value = it)) }
            },
            label = { Text(label) },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            singleLine = true,
            modifier = Modifier.weight(0.45f),
        )
        Box(Modifier.weight(0.55f)) {
            OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
                Text(units.getOrElse(part.unit) { "Unknown" })
            }
            DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                units.forEachIndexed { unit, unitLabel ->
                    DropdownMenuItem(
                        text = { Text(unitLabel) },
                        onClick = {
                            expanded = false
                            onChange(part.copy(unit = unit))
                        },
                    )
                }
            }
        }
    }
}

private fun numericSettingValue(record: FurbleProtocol.SettingRecord): Long? = when (record.type) {
    FurbleProtocol.SettingType.UINT8 -> record.value.firstOrNull()?.toLong()?.and(0xFF)
    FurbleProtocol.SettingType.UINT32 -> FurbleProtocol.decodeUint32(record.value)
    else -> null
}

private fun encodeNumericSetting(type: Int, value: Long): ByteArray? = when (type) {
    FurbleProtocol.SettingType.UINT8 -> value.takeIf { it in 0..255 }?.toByteArray()
    FurbleProtocol.SettingType.UINT32 -> runCatching { FurbleProtocol.encodeUint32(value) }.getOrNull()
    else -> null
}

private fun Long.toByteArray(): ByteArray = byteArrayOf(toByte())

private fun recordTypeLabel(type: Int): String = when (type) {
    FurbleProtocol.SettingType.BOOL -> "bool"
    FurbleProtocol.SettingType.UINT8 -> "uint8"
    FurbleProtocol.SettingType.UINT32 -> "uint32"
    FurbleProtocol.SettingType.STRING -> "string"
    FurbleProtocol.SettingType.BLOB -> "blob"
    else -> "type $type"
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
    val ready = state.connection == ConnectionState.READY && state.protectedReady()
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

private fun AuthState.displayName(): String = when (this) {
    AuthState.UNKNOWN -> "Not authenticated in this session"
    AuthState.AUTHENTICATING -> "Authenticating…"
    AuthState.AUTHENTICATED -> "Authenticated"
    AuthState.NOT_REQUIRED -> "Password not required"
    AuthState.REJECTED -> "Password rejected. Retry with the current password."
    AuthState.DROPPED -> "Locked for this connection after three failures"
}

private fun Int.displayGpsSource(): String = when (this) {
    1 -> "Wired UART GPS"
    2 -> "Phone companion"
    else -> "No fix"
}

private fun Int.remainingLabel(): String = if (this == 0xFFFF) "infinite shots" else "$this shots remaining"
