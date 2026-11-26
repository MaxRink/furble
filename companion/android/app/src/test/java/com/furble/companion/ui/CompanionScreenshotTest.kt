package com.furble.companion.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
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
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import com.furble.companion.FurbleApplication
import com.furble.companion.MainViewModel
import com.furble.companion.permissions.PermissionSnapshot
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
 * Renders the companion main screen off-device and compares it with the
 * committed baseline in app/src/test/screenshots.
 *
 * Every rendering input is pinned: API level and locale through @Config, density
 * and font scale through the composition, and the size through the capture
 * region. The screen reads no clock, no location fix and no network value, so
 * the same source produces the same pixels.
 *
 * Two known nodes are asserted before the capture, because the repository is
 * read asynchronously: a late emission has to fail on the node that moved rather
 * than on an unexplained pixel count.
 *
 * Regenerate the baselines after an intended UI change:
 * ./gradlew testDebugUnitTest --tests '*Companion*Test*' -PupdateScreenshots
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "en-rUS-w360dp-h720dp-xhdpi")
class CompanionScreenshotTest {
    @get:Rule
    val compose = createComposeRule()

    private val snapshot = UiSnapshot.forScreen(SNAPSHOT_NAME, "The main screen")

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
    fun mainScreenMatchesTheCommittedBaseline() {
        val application = RuntimeEnvironment.getApplication() as FurbleApplication
        val viewModel = MainViewModel(application)

        compose.setContent {
            CompositionLocalProvider(LocalDensity provides Density(density = UiSnapshot.DENSITY, fontScale = 1f)) {
                Box(Modifier.size(UiSnapshot.WIDTH_DP.dp, UiSnapshot.HEIGHT_DP.dp).testTag(SNAPSHOT_TAG)) {
                    FurbleCompanionApp(
                        viewModel = viewModel,
                        permissionSnapshot = PermissionSnapshot(
                            bluetoothScan = true,
                            bluetoothConnect = true,
                            fineLocation = true,
                        ),
                        permissionRequestAttempted = false,
                        requestPermissions = {},
                        openPermissionSettings = {},
                        launchAssociationChooser = {},
                    )
                }
            }
        }

        // The repository state arrives asynchronously. These two nodes sit at the
        // top of the state-dependent content, so a late emission fails with a
        // readable assertion instead of a pixel count nobody can interpret.
        compose.onNodeWithText("furble companion").assertIsDisplayed()
        compose.onNodeWithText("Not authenticated in this session").assertIsDisplayed()

        val actual = compose.onNodeWithTag(SNAPSHOT_TAG).captureToImage().asAndroidBitmap()
        snapshot.verify(actual)
    }

    private companion object {
        const val SNAPSHOT_NAME = "companion-main"
        const val SNAPSHOT_TAG = SNAPSHOT_NAME
    }
}
