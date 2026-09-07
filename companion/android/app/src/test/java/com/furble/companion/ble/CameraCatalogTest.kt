package com.furble.companion.ble

import com.furble.companion.protocol.FurbleProtocol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CameraCatalogTest {
    @Test
    fun listTerminatorReplacesSnapshotAndRemovesAbsentIds() {
        val catalog = CameraCatalog()
        val existing = camera(1, "old", FurbleProtocol.CameraFlag.SAVED)
        val replacement = camera(2, "new", FurbleProtocol.CameraFlag.SAVED)
        assertEquals(CameraRecordDisposition.UPDATED, catalog.accept(existing))

        assertTrue(catalog.beginList())
        assertEquals(CameraRecordDisposition.PENDING, catalog.accept(replacement))
        assertEquals(
            CameraRecordDisposition.SNAPSHOT,
            catalog.accept(camera(0xFF, "", 0)),
        )
        assertEquals(listOf(2), catalog.records.map { it.cameraId })
    }

    @Test
    fun actionStatusOnlyAcknowledgementsDoNotOverwriteLiveRows() {
        val catalog = CameraCatalog()
        val live = camera(7, "Fujifilm", FurbleProtocol.CameraFlag.SAVED)
        assertEquals(CameraRecordDisposition.UPDATED, catalog.accept(live))

        val acknowledgement = camera(7, "", 0)
        assertEquals(CameraRecordDisposition.IGNORED, catalog.accept(acknowledgement))
        assertEquals(live, catalog.records.single())
        assertEquals(CameraRecordDisposition.IGNORED, catalog.accept(camera(0xFF, "", 0)))
    }

    @Test
    fun rejectedListClosesTransactionSoTheNextRefreshCanStart() {
        val catalog = CameraCatalog()
        assertTrue(catalog.beginList())
        assertEquals(
            CameraRecordDisposition.REJECTED,
            catalog.accept(camera(0xFF, "", 0, FurbleProtocol.CameraStatus.BUSY)),
        )
        assertTrue(catalog.beginList())
    }

    private fun camera(id: Int, name: String, flags: Int, status: Int = FurbleProtocol.CameraStatus.OK) =
        FurbleProtocol.CameraRecord(
            status = status,
            cameraId = id,
            cameraType = if (name.isEmpty()) 0 else 1,
            flags = flags,
            progress = 0,
            rssi = if (name.isEmpty()) 0 else FurbleProtocol.CAMERA_RSSI_UNKNOWN,
            state = FurbleProtocol.CameraState.IDLE,
            name = name,
        )
}
