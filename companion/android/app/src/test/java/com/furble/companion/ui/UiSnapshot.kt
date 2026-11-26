package com.furble.companion.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.Locale
import kotlin.math.abs
import org.junit.Assert.assertEquals
import org.junit.Assert.fail

/**
 * The pixel comparison shared by every UI snapshot in this module.
 *
 * One comparator keeps the tolerance, the failure text and the regeneration
 * command identical for every screen. The tolerance is a safety margin, not a
 * measured font difference: the committed baselines rendered on macOS arm64
 * matched a linux/amd64 render with 0 differing pixels. Layout-scale changes move
 * an order of magnitude more pixels than it allows, so a copy edit under about a
 * dozen characters can still pass.
 */
internal class UiSnapshot(
    private val name: String,
    private val screen: String,
    private val baseline: File,
    private val actualDir: File,
    private val update: Boolean,
) {
    fun verify(actual: Bitmap) {
        if (update) {
            writeBaseline(actual)
            println("Wrote the $name baseline to ${baseline.absolutePath}")
            return
        }

        val expected = if (baseline.isFile) BitmapFactory.decodeFile(baseline.path) else null
        if (expected == null) {
            fail(
                "The baseline ${baseline.absolutePath} is missing or unreadable. " +
                    "The rendered screen is at ${writeActual(actual).absolutePath}. " +
                    "Regenerate with $REGENERATE_COMMAND",
            )
            return
        }

        assertEquals(
            "The $name baseline changed size, so its layout moved. Regenerate with $REGENERATE_COMMAND",
            "${expected.width}x${expected.height}",
            "${actual.width}x${actual.height}",
        )

        val differing = countDifferingPixels(expected, actual)
        val differingFraction = differing.toDouble() / (actual.width.toLong() * actual.height)
        if (differingFraction > MAX_DIFFERING_FRACTION) {
            fail(
                "$screen differs from ${baseline.name} in $differing pixels " +
                    "(${String.format(Locale.US, "%.3f", differingFraction * 100)} percent, " +
                    "allowed ${MAX_DIFFERING_FRACTION * 100} percent). " +
                    "The rendered screen is at ${writeActual(actual).absolutePath}. " +
                    "Regenerate the baseline only after reviewing that image with $REGENERATE_COMMAND",
            )
        }
        println(
            "$screen matches ${baseline.name} at ${actual.width}x${actual.height} " +
                "with $differing differing pixels.",
        )
    }

    // Regeneration replaces the committed baseline, which is also this task's
    // declared input. Write a sibling temp file first and move it into place, so
    // an interrupted run cannot leave a truncated baseline behind.
    private fun writeBaseline(actual: Bitmap) {
        baseline.parentFile?.mkdirs()
        val temporary = File(baseline.parentFile, "${baseline.name}.tmp")
        temporary.outputStream().use { actual.compress(Bitmap.CompressFormat.PNG, 100, it) }
        Files.move(temporary.toPath(), baseline.toPath(), StandardCopyOption.REPLACE_EXISTING)
    }

    private fun writeActual(actual: Bitmap): File {
        actualDir.mkdirs()
        val target = File(actualDir, "$name-actual.png")
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

    private fun channelsDiffer(expected: Int, actual: Int): Boolean = ARGB_SHIFTS.any { shift ->
        val expectedChannel = (expected ushr shift) and 0xFF
        val actualChannel = (actual ushr shift) and 0xFF
        abs(expectedChannel - actualChannel) > CHANNEL_TOLERANCE
    }

    companion object {
        const val DENSITY = 2f
        const val WIDTH_DP = 360
        const val HEIGHT_DP = 720
        const val CHANNEL_TOLERANCE = 2
        const val MAX_DIFFERING_FRACTION = 0.001
        const val REGENERATE_COMMAND =
            "./gradlew testDebugUnitTest --tests '*Companion*Test*' -PupdateScreenshots"
        private val ARGB_SHIFTS = listOf(24, 16, 8, 0)

        /** Reads the same properties `app/build.gradle.kts` sets for every test task. */
        fun forScreen(name: String, screen: String): UiSnapshot = UiSnapshot(
            name = name,
            screen = screen,
            baseline = File(screenshotDir(), "$name.png"),
            actualDir = File(System.getProperty("furble.screenshot.actualDir") ?: "build/outputs/screenshots"),
            update = System.getProperty("furble.screenshot.update") == "true",
        )

        private fun screenshotDir(): File =
            File(System.getProperty("furble.screenshot.dir") ?: "src/test/screenshots")
    }
}
