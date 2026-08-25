package com.furble.companion.ble

import com.furble.companion.security.PasswordStore
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private class FakePasswordStore : PasswordStore {
    private var value: String? = null
    override fun read(): String? = value
    override fun write(password: String) { value = password }
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
        store.write("secret")
        assertTrue(store.read() != null)
        store.clear()
        assertNull(store.read())
    }
}
