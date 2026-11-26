package com.furble.companion.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
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
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.Locale
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.fail
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
 * Regenerate the baseline after an intended UI change:
 * ./gradlew testDebugUnitTest --tests '*CompanionScreenshotTest*' -PupdateScreenshots
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "en-rUS-w360dp-h720dp-xhdpi")
class CompanionScreenshotTest {
    @get:Rule
    val compose = createComposeRule()

    private val screenshotDir = File(
        System.getProperty("furble.screenshot.dir") ?: "src/test/screenshots",
    )
    private val actualDir = File(
        System.getProperty("furble.screenshot.actualDir") ?: "build/outputs/screenshots",
    )
    private val updateSnapshots = System.getProperty("furble.screenshot.update") == "true"
    private val baseline = File(screenshotDir, "$SNAPSHOT_NAME.png")

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
            CompositionLocalProvider(LocalDensity provides Density(density = DENSITY, fontScale = 1f)) {
                Box(Modifier.size(SNAPSHOT_WIDTH_DP.dp, SNAPSHOT_HEIGHT_DP.dp).testTag(SNAPSHOT_TAG)) {
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

        if (updateSnapshots) {
            writeBaseline(actual)
            println("Wrote the UI snapshot baseline to ${baseline.absolutePath}")
            return
        }

        val expected = if (baseline.isFile) BitmapFactory.decodeFile(baseline.path) else null
        if (expected == null) {
            fail(
                "The UI snapshot baseline ${baseline.absolutePath} is missing or unreadable. " +
                    "The rendered screen is at ${writeActual(actual).absolutePath}. Regenerate with $REGENERATE_COMMAND",
            )
            return
        }

        assertEquals(
            "The ${SNAPSHOT_NAME} baseline changed size, so its layout moved. Regenerate with $REGENERATE_COMMAND",
            "${expected.width}x${expected.height}",
            "${actual.width}x${actual.height}",
        )

        val differing = countDifferingPixels(expected, actual)
        val differingFraction = differing.toDouble() / (actual.width.toLong() * actual.height)
        if (differingFraction > MAX_DIFFERING_FRACTION) {
            fail(
                "The main screen differs from ${baseline.name} in $differing pixels " +
                    "(${String.format(Locale.US, "%.3f", differingFraction * 100)} percent, " +
                    "allowed ${MAX_DIFFERING_FRACTION * 100} percent). " +
                    "The rendered screen is at ${writeActual(actual).absolutePath}. " +
                    "Regenerate the baseline only after reviewing that image with $REGENERATE_COMMAND",
            )
        }
        println(
            "The main screen matches ${baseline.name} at ${actual.width}x${actual.height} " +
                "with $differing differing pixels.",
        )
    }

    // Regeneration replaces the committed baseline, which is also this task's
    // declared input. Write a sibling temp file first and move it into place, so
    // an interrupted run cannot leave a truncated baseline behind.
    private fun writeBaseline(actual: Bitmap) {
        val directory = baseline.parentFile
        directory?.mkdirs()
        val temporary = File(directory, "${baseline.name}.tmp")
        temporary.outputStream().use { actual.compress(Bitmap.CompressFormat.PNG, 100, it) }
        Files.move(temporary.toPath(), baseline.toPath(), StandardCopyOption.REPLACE_EXISTING)
    }

    private fun writeActual(actual: Bitmap): File {
        actualDir.mkdirs()
        val target = File(actualDir, "$SNAPSHOT_NAME-actual.png")
        target.outputStream().use { actual.compress(Bitmap.CompressFormat.PNG, 100, it) }
        return target
    }

    private fun countDifferingPixels(expected: Bitmap, actual: Bitmap): Int {
        val expectedPixels = expected.readPixels()
        val actualPixels = actual.readPixels()
        return expectedPixels.indices.count { index ->
            channelsDiffer(expectedPixels[index], actualPixels[index])
        }
    }

    private fun Bitmap.readPixels(): IntArray {
        val pixels = IntArray(width * height)
        getPixels(pixels, 0, width, 0, 0, width, height)
        return pixels
    }

    // The tolerance is a safety margin, not a measured font difference: the
    // baseline rendered on macOS arm64 matched a linux/amd64 render with 0
    // differing pixels. It keeps an unproven runner from failing on glyph noise,
    // and it is why a small copy edit can pass. Layout-scale changes move far
    // more pixels than it allows.
    private fun channelsDiffer(expected: Int, actual: Int): Boolean = ARGB_SHIFTS.any { shift ->
        val expectedChannel = (expected ushr shift) and 0xFF
        val actualChannel = (actual ushr shift) and 0xFF
        abs(expectedChannel - actualChannel) > CHANNEL_TOLERANCE
    }

    private companion object {
        const val SNAPSHOT_NAME = "companion-main"
        const val SNAPSHOT_TAG = SNAPSHOT_NAME
        const val SNAPSHOT_WIDTH_DP = 360
        const val SNAPSHOT_HEIGHT_DP = 720
        const val DENSITY = 2f
        const val CHANNEL_TOLERANCE = 2
        const val MAX_DIFFERING_FRACTION = 0.001
        val ARGB_SHIFTS = listOf(24, 16, 8, 0)
        const val REGENERATE_COMMAND =
            "./gradlew testDebugUnitTest --tests '*CompanionScreenshotTest*' -PupdateScreenshots"
    }
}
