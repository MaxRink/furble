package com.furble.companion.protocol

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Checks this app's codec against the firmware's committed golden corpus.
 *
 * `tests/protocol/golden/` is generated from the firmware sources by
 * `make -C tests/protocol generate` and diffed by the Companion protocol tests
 * workflow, so it is the shared definition of every companion packet. The Apple
 * client already reads `tests/protocol/golden/companion_auth.json` at runtime.
 * This test gives the Android client the same link: every location, status,
 * settings and trigger fixture the firmware emits is decoded or reproduced
 * here, so a wire change cannot land on the firmware side without failing this
 * module.
 *
 * The corpus is read from the checkout instead of being copied into the test,
 * because a copy is what drifts. The walk up from the Gradle module directory
 * finds it in a full checkout; a missing corpus fails with that path rather
 * than passing silently.
 */
class FurbleGoldenCorpusTest {
    @Test
    fun everyLocationFixtureMatchesTheAppCodec() {
        val fixtures = fixtures("location")
        assertEquals("the corpus lost its location fixtures", 4, fixtures.size)
        for (fixture in fixtures) {
            if (fixture.valid) {
                val decoded = FurbleProtocol.decodeLocation(fixture.bytes)
                assertNotNull("${fixture.describe()} was rejected by decodeLocation", decoded)
                assertArrayEquals(
                    "${fixture.describe()} no longer re-encodes the same bytes",
                    fixture.bytes,
                    FurbleProtocol.encodeLocation(requireNotNull(decoded)),
                )
            } else {
                assertNull(
                    "${fixture.describe()} is invalid but decodeLocation accepted it",
                    FurbleProtocol.decodeLocation(fixture.bytes),
                )
            }
        }
    }

    @Test
    fun everyStatusFixtureDecodesWithTheFirmwareFields() {
        val fixtures = fixtures("status")
        assertEquals("the corpus lost its status fixtures", 3, fixtures.size)
        for (fixture in fixtures) {
            val bytes = fixture.bytes
            val status = FurbleProtocol.decodeStatus(bytes)
            assertNotNull("${fixture.describe()} was rejected by decodeStatus", status)
            status ?: continue
            assertEquals(fixture.describe(), 1, status.version)
            assertEquals(fixture.describe(), bytes[1].u8(), status.batteryPercent)
            assertEquals(fixture.describe(), bytes.le16(2), status.batteryMv)
            assertEquals(fixture.describe(), bytes.le16(4).toShort().toInt(), status.batteryMa)
            assertEquals(fixture.describe(), bytes[6].u8(), status.powerFlags)
            assertEquals(fixture.describe(), bytes[7].u8(), status.cameraTotal)
            assertEquals(fixture.describe(), bytes[8].u8(), status.cameraConnected)
        }
        // The firmware writes 0xffff for an unknown voltage and the app reports
        // the raw unsigned field, so this pins the unsigned read rather than a
        // sentinel rewrite the app does not do.
        val sentinels = fixtures.first { it.valid && it.file.endsWith("sentinels.bin") }
        assertEquals(
            "the sentinel fixture no longer reads as the maximum unsigned voltage",
            0xFFFF,
            FurbleProtocol.decodeStatus(sentinels.bytes)?.batteryMv,
        )
    }

    @Test
    fun everySettingsResponseFixtureParsesOrIsRejectedAsTheFirmwareDeclares() {
        val fixtures = fixtures("settings-response") + fixtures("settings-list-record")
        assertTrue("the corpus lost its settings responses", fixtures.size >= 200)
        for (fixture in fixtures) {
            val response = FurbleProtocol.parseSettingsResponse(fixture.bytes)
            if (!fixture.valid) {
                assertNull(
                    "${fixture.describe()} is invalid but parseSettingsResponse accepted it",
                    response,
                )
                continue
            }
            assertNotNull("${fixture.describe()} was rejected by parseSettingsResponse", response)
            response ?: continue
            val bytes = fixture.bytes
            assertEquals(fixture.describe(), bytes[0].u8(), response.status)
            assertEquals(fixture.describe(), bytes[1].u8(), response.id)
            assertEquals(fixture.describe(), bytes[2].u8(), response.type)
            // A list record appends its flags after the value, which is the
            // firmware's own layout; a plain response has no flags byte.
            val valueStart = 4
            val valueEnd = valueStart + bytes[3].u8()
            assertEquals(fixture.describe(), valueEnd - valueStart, response.value.size)
            assertArrayEquals(
                "${fixture.describe()} parsed the wrong value bytes",
                bytes.copyOfRange(valueStart, valueEnd),
                response.value,
            )
            if (fixture.kind == "settings-list-record") {
                assertTrue(fixture.describe(), response.isListRecord)
                if (valueEnd < bytes.size) {
                    assertEquals(fixture.describe(), bytes[valueEnd].u8(), response.flags)
                }
            }
        }
    }

    @Test
    fun everyValidSettingsRequestIsReproducedByTheAppEncoder() {
        val fixtures = fixtures("settings-request")
        assertTrue("the corpus lost its settings requests", fixtures.size >= 100)
        for (fixture in fixtures) {
            if (!fixture.valid) continue
            val bytes = fixture.bytes
            val encoded = when (bytes[0].u8()) {
                FurbleProtocol.SettingsOperation.LIST -> FurbleProtocol.encodeSettingsListRequest()
                FurbleProtocol.SettingsOperation.GET -> FurbleProtocol.encodeSettingsGet(bytes[1].u8())
                FurbleProtocol.SettingsOperation.SET -> FurbleProtocol.encodeSettingsSet(
                    bytes[1].u8(),
                    bytes.copyOfRange(3, bytes.size),
                )
                else -> null
            }
            assertArrayEquals(
                "${fixture.describe()} is not what the app encoder produces",
                bytes,
                encoded,
            )
        }
    }

    @Test
    fun everyValidTriggerFixtureIsReproducedByTheAppEncoder() {
        val fixtures = fixtures("trigger")
        assertEquals("the corpus lost its trigger fixtures", 11, fixtures.size)
        for (fixture in fixtures) {
            if (!fixture.valid) continue
            val operation = fixture.bytes[1].u8()
            // Only the timed shutter carries a hold field, which is exactly the
            // rule CompanionService::handleTrigger enforces.
            val hold = if (operation == TIMED_SHUTTER_OPERATION) fixture.bytes.le16(2) else 0
            assertArrayEquals(
                "${fixture.describe()} is not what the app encoder produces",
                fixture.bytes,
                FurbleProtocol.encodeTrigger(operation, hold),
            )
        }
    }

    @Test
    fun theSharedAuthFixtureProducesTheFirmwareProof() {
        val text = goldenFile("companion_auth.json").readText(Charsets.UTF_8)
        val password = jsonValue(text, "password_utf8").toByteArray(Charsets.UTF_8)
        val nonce = hex(jsonValue(text, "nonce"))
        val proof = byteArrayOf(FurbleProtocol.AUTH_VERSION.toByte(), FurbleProtocol.AUTH_OP_PROOF.toByte()) +
            FurbleProtocol.hmacSha256(password, nonce).copyOfRange(0, 16)
        assertArrayEquals(
            "the app no longer produces the firmware's own HMAC challenge proof",
            hex(jsonValue(text, "proof")),
            proof,
        )
        assertEquals(
            "the app no longer decodes the firmware's authenticated result",
            FurbleProtocol.AUTH_RESULT_AUTHENTICATED,
            FurbleProtocol.decodeAuthResult(hex(jsonValue(text, "result_authenticated"))),
        )
        assertArrayEquals(
            "the app no longer builds the firmware's challenge frame",
            hex(jsonValue(text, "challenge")),
            byteArrayOf(FurbleProtocol.AUTH_VERSION.toByte(), FurbleProtocol.AUTH_OP_BEGIN.toByte()) + nonce,
        )
    }

    private data class Fixture(
        val file: String,
        val kind: String,
        val valid: Boolean,
        val bytes: ByteArray,
    ) {
        fun describe(): String = "golden/$file"
    }

    private fun fixtures(kind: String): List<Fixture> {
        val fixtures = mutableListOf<Fixture>()
        for (line in corpusLines()) {
            val file = jsonValueOrNull(line, "file") ?: continue
            val lineKind = jsonValueOrNull(line, "kind") ?: continue
            if (lineKind != kind) continue
            fixtures += Fixture(
                file = file,
                kind = lineKind,
                valid = jsonValue(line, "valid") == "true",
                bytes = hex(jsonValue(line, "expected_bytes")),
            )
        }
        require(fixtures.isNotEmpty()) { "the shared golden corpus has no $kind fixtures" }
        return fixtures
    }

    private fun corpusLines(): List<String> =
        goldenFile("corpus.json").readLines(Charsets.UTF_8)
            .map(String::trim)
            .filter { it.startsWith("{\"file\"") }

    private fun goldenFile(name: String): File {
        var directory: File? = File(".").absoluteFile
        repeat(CHECKOUT_WALK) {
            val candidate = directory?.resolve("tests/protocol/golden/$name")
            if (candidate != null && candidate.isFile) return candidate
            directory = directory?.parentFile
        }
        throw AssertionError(
            "the shared golden corpus tests/protocol/golden/$name was not found from " +
                "${File(".").absolutePath}. Run the test from the repository checkout.",
        )
    }

    // The corpus is written by the firmware generator and the auth fixture is
    // hand formatted, so one pattern accepts both forms: an unbracketed scalar
    // ("valid":true) and a quoted string ("proof": "0101c5"). The optional
    // quote is captured so its closing partner has to match.
    private fun jsonValueOrNull(text: String, key: String): String? {
        val match = Regex("\"$key\"\\s*:\\s*(\"?)([^\",]*)\\1").find(text)
        return match?.groupValues?.get(2)
    }

    private fun jsonValue(line: String, key: String): String =
        requireNotNull(jsonValueOrNull(line, key)) { "corpus line has no \"$key\": $line" }

    private fun hex(value: String): ByteArray =
        value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()

    private fun Byte.u8(): Int = toInt() and 0xFF

    private fun ByteArray.le16(offset: Int): Int =
        this[offset].u8() or (this[offset + 1].u8() shl 8)

    private companion object {
        const val CHECKOUT_WALK = 8
        const val TIMED_SHUTTER_OPERATION = 4
    }
}
