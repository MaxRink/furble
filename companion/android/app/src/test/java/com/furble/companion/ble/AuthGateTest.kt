package com.furble.companion.ble

import com.furble.companion.security.PasswordStore
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private class FakePasswordStore : PasswordStore {
    private var value: ByteArray? = null
    override fun read(): ByteArray? = value?.copyOf()
    override fun write(password: ByteArray) { value = password.copyOf() }
    override fun clear() { value = null }
}

class AuthGateTest {
    @Test
    fun protectedOperationsRequireAuthenticatedSessionWhenAuthIsSupported() {
        assertFalse(CompanionUiState(authSupported = true).protectedReady())
        assertTrue(
            CompanionUiState(authSupported = true, auth = AuthState.AUTHENTICATED).protectedReady(),
        )
        assertTrue(
            CompanionUiState(authSupported = true, auth = AuthState.NOT_REQUIRED).protectedReady(),
        )
        assertTrue(CompanionUiState(authSupported = false).protectedReady())
    }

    @Test
    fun forgettingFakePasswordCannotRestoreIt() {
        val store = FakePasswordStore()
        store.write("secret".toByteArray())
        assertTrue(store.read() != null)
        store.clear()
        assertNull(store.read())
    }

    @Test
    fun authInputSurvivesAsyncBoundaryAfterCallerWipesBuffer() {
        var queued: (() -> Unit)? = null
        var observed: ByteArray? = null
        val dispatcher = AuthInputDispatcher { queued = it }
        val callerBuffer = "saved password".toByteArray()
        dispatcher.submit(callerBuffer) {
            observed = it.copyOf()
            it.fill(0)
        }
        callerBuffer.fill(0)
        queued!!.invoke()
        assertTrue(observed!!.contentEquals("saved password".toByteArray()))
    }

    @Test
    fun canceledAuthGenerationRejectsQueuedAndStaleResults() {
        val tracker = AuthAttemptTracker()
        val first = tracker.begin()
        assertTrue(tracker.accepts(first))
        tracker.cancel()
        assertFalse(tracker.accepts(first))
        val second = tracker.begin()
        assertFalse(tracker.accepts(first))
        assertTrue(tracker.accepts(second))
    }
}
