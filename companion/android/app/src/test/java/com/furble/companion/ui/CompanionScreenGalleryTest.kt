package com.furble.companion.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import com.furble.companion.FurbleApplication
import com.furble.companion.MainViewModel
import com.furble.companion.ble.AssociationState
import com.furble.companion.ble.AuthState
import com.furble.companion.ble.CompanionUiState
import com.furble.companion.ble.ConnectionState
import com.furble.companion.protocol.FurbleProtocol
import java.util.Locale
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * Renders the companion screens that `./gradlew test` otherwise never draws and
 * compares each one with its committed baseline.
 *
 * `CompanionScreenshotTest` covers the first-launch main screen. Everything else
 * the app shows was only ever captured by hand on an emulator, which is how the
 * hand-captured set under `docs/screenshots/` drifted three UI changes behind
 * the source. These captures come from the shipped composables, so they cannot
 * go stale without failing here.
 *
 * The shell takes its state as a value, so this test builds the state a
 * connected, authenticated device reports and then selects each tab through the
 * navigation bar. Clicking is the only interaction the tests perform; nothing
 * here exercises Bluetooth, CompanionDeviceManager, Keychain, or the firmware.
 *
 * Regenerate the baselines after an intended UI change:
 * ./gradlew testDebugUnitTest --tests '*Companion*Test*' -PupdateScreenshots
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "en-rUS-w360dp-h720dp-xhdpi")
class CompanionScreenGalleryTest {
    @get:Rule
    val compose = createComposeRule()

    private lateinit var callingLocale: Locale

    @Before
    fun pinTheRenderingInputs() {
        callingLocale = Locale.getDefault()
        Locale.setDefault(Locale.US)
    }

    @After
    fun restoreTheCallingLocale() {
        Locale.setDefault(callingLocale)
    }

    @Test
    fun thePermissionRationaleMatchesTheCommittedBaseline() {
        val viewModel = MainViewModel(RuntimeEnvironment.getApplication() as FurbleApplication)

        compose.setContent {
            SnapshotFrame {
                FurbleCompanionApp(
                    viewModel = viewModel,
                    permissionSnapshot = null,
                    permissionRequestAttempted = true,
                    requestPermissions = {},
                    openPermissionSettings = {},
                    launchAssociationChooser = {},
                )
            }
        }

        compose.onNodeWithText("Permissions needed").assertIsDisplayed()
        compose.onNodeWithText("Open app settings").assertIsDisplayed()
        verify("companion-rationale", "The permission rationale")
    }

    @Test
    fun theConnectedStatusScreenMatchesTheCommittedBaseline() {
        showShell()
        compose.onNodeWithText("Status").performClick()

        compose.onNodeWithText("furble-38a70").assertIsDisplayed()
        compose.onNodeWithText("Authenticated").assertIsDisplayed()
        compose.onNodeWithText("Phone GPS").assertIsDisplayed()
        verify("companion-status", "The connected status screen")
    }

    @Test
    fun theCameraScreenMatchesTheCommittedBaseline() {
        showShell()
        compose.onNodeWithText("Cameras").performClick()

        compose.onNodeWithText("Fujifilm X-T5").assertIsDisplayed()
        compose.onNodeWithText("Connect selected").assertIsDisplayed()
        verify("companion-cameras", "The camera screen")
    }

    @Test
    fun theSettingsScreenMatchesTheCommittedBaseline() {
        showShell()
        compose.onNodeWithText("Settings").performClick()

        compose.onNodeWithText("Device settings").assertIsDisplayed()
        compose.onNodeWithText("Search settings").assertIsDisplayed()
        verify("companion-settings", "The settings screen")
    }

    @Test
    fun theTriggerScreenMatchesTheCommittedBaseline() {
        showShell()
        compose.onNodeWithText("Trigger").performClick()

        compose.onNodeWithText("Timed shutter").assertIsDisplayed()
        verify("companion-trigger", "The trigger screen")
    }

    /**
     * Composes the shipped shell with the state a connected, authenticated
     * device reports, so the Settings, Cameras and Trigger tabs exist. The
     * repository behind `MainViewModel` stays in its initial state; only the
     * callbacks are wired to it.
     */
    private fun showShell() {
        val viewModel = MainViewModel(RuntimeEnvironment.getApplication() as FurbleApplication)

        compose.setContent {
            SnapshotFrame {
                CompanionShell(state = connectedState(), viewModel = viewModel)
            }
        }

        compose.onNodeWithText("furble companion").assertIsDisplayed()
        // Every tab is present only for a protected, capability-carrying link, so
        // this also proves the state above reached the navigation bar.
        listOf("Status", "Cameras", "Settings", "Trigger").forEach { label ->
            compose.onNodeWithText(label).assertIsDisplayed()
        }
    }

    private fun verify(name: String, screen: String) {
        val actual = compose.onNodeWithTag(CAPTURE_TAG).captureToImage().asAndroidBitmap()
        UiSnapshot.forScreen(name, screen).verify(actual)
    }

    @Composable
    private fun SnapshotFrame(content: @Composable () -> Unit) {
        CompositionLocalProvider(LocalDensity provides Density(density = UiSnapshot.DENSITY, fontScale = 1f)) {
            Box(Modifier.size(UiSnapshot.WIDTH_DP.dp, UiSnapshot.HEIGHT_DP.dp).testTag(CAPTURE_TAG)) {
                content()
            }
        }
    }

    private fun connectedState(): CompanionUiState = CompanionUiState(
        association = AssociationState(
            associated = true,
            address = "C4:7C:8D:11:22:33",
            deviceName = "furble-38a70",
            present = true,
            bonded = true,
        ),
        connection = ConnectionState.READY,
        auth = AuthState.AUTHENTICATED,
        authSupported = true,
        storedPassword = true,
        status = FurbleProtocol.StatusSnapshot(
            version = 1,
            batteryPercent = 78,
            batteryMv = 4020,
            batteryMa = -96,
            powerFlags = 0x03,
            cameraTotal = 2,
            cameraConnected = 1,
            controlState = 1,
            gpsSource = 2,
            gpsSatellites = 9,
            intervalometerState = 0,
            intervalometerRemaining = 0,
            uptimeSeconds = 4321,
        ),
        capability = FurbleProtocol.CapabilitySnapshot(version = 1, wireVersion = 2, features = 0x03),
        cameraCharacteristicAvailable = true,
        camerasSupported = true,
        cameras = listOf(
            FurbleProtocol.CameraRecord(
                status = FurbleProtocol.SettingsStatus.OK,
                cameraId = 0,
                cameraType = 1,
                flags = FurbleProtocol.CameraFlag.SAVED or FurbleProtocol.CameraFlag.SELECTED or
                    FurbleProtocol.CameraFlag.TARGET or FurbleProtocol.CameraFlag.CONNECTED,
                progress = 100,
                rssi = -48,
                state = FurbleProtocol.CameraState.CONNECTED,
                name = "Fujifilm X-T5",
            ),
            FurbleProtocol.CameraRecord(
                status = FurbleProtocol.SettingsStatus.OK,
                cameraId = 1,
                cameraType = 1,
                flags = FurbleProtocol.CameraFlag.SAVED,
                progress = 0,
                rssi = -71,
                state = FurbleProtocol.CameraState.IDLE,
                name = "Fujifilm X100V",
            ),
        ),
        settingsSupported = true,
        settings = listOf(
            setting(1, FurbleProtocol.SettingType.UINT8, byteArrayOf(0x48)),
            setting(4, FurbleProtocol.SettingType.UINT8, byteArrayOf(0x24), FurbleProtocol.SettingFlag.DANGEROUS),
            setting(5, FurbleProtocol.SettingType.BOOL, byteArrayOf(1)),
            setting(
                40,
                FurbleProtocol.SettingType.UINT8,
                byteArrayOf(0x7e.toByte()),
                FurbleProtocol.SettingFlag.NEEDS_RESTART,
            ),
            setting(65, FurbleProtocol.SettingType.UINT8, byteArrayOf(0x00)),
        ),
        locationEnabled = true,
        locationIntervalSeconds = 10,
        locationFixesSent = 128,
    )

    private fun setting(id: Int, type: Int, value: ByteArray, flags: Int = 0) =
        FurbleProtocol.SettingRecord(id = id, type = type, value = value, flags = flags)

    private companion object {
        const val CAPTURE_TAG = "companion-gallery"
    }
}
