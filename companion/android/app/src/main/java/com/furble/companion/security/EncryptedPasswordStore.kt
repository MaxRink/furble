package com.furble.companion.security

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.nio.ByteBuffer
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Stores the companion password as AES-GCM ciphertext. The Android Keystore key
 * is non-exportable, so a copied preferences file cannot be decrypted on
 * another device. Password values and plaintext are never logged.
 */
class EncryptedPasswordStore(context: Context) {
    companion object {
        private const val PREFS = "companion_auth"
        private const val CIPHERTEXT = "password_ciphertext"
        private const val KEY_ALIAS = "furble_companion_password_v1"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val IV_SIZE = 12
        private const val TAG_SIZE_BITS = 128
    }

    private val preferences = context.applicationContext
        .getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun read(): String? {
        val encoded = preferences.getString(CIPHERTEXT, null) ?: return null
        return runCatching {
            val packed = Base64.decode(encoded, Base64.NO_WRAP)
            require(packed.size > IV_SIZE)
            val iv = packed.copyOfRange(0, IV_SIZE)
            val ciphertext = packed.copyOfRange(IV_SIZE, packed.size)
            Cipher.getInstance(TRANSFORMATION).run {
                init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(TAG_SIZE_BITS, iv))
                String(doFinal(ciphertext), Charsets.UTF_8)
            }
        }.getOrElse {
            // A restored preferences file has no matching Keystore key. Do not
            // keep retrying a corrupt or cross-device ciphertext.
            clear()
            null
        }
    }

    fun write(password: String) {
        require(password.isNotEmpty())
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, key())
        val plaintext = password.toByteArray(Charsets.UTF_8)
        try {
            val ciphertext = cipher.doFinal(plaintext)
            val packed = ByteBuffer.allocate(cipher.iv.size + ciphertext.size)
            packed.put(cipher.iv)
            packed.put(ciphertext)
            preferences.edit()
                .putString(CIPHERTEXT, Base64.encodeToString(packed.array(), Base64.NO_WRAP))
                .apply()
        } finally {
            plaintext.fill(0)
        }
    }

    fun clear() {
        preferences.edit().remove(CIPHERTEXT).apply()
    }

    private fun key(): SecretKey {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
            .apply {
                init(
                    KeyGenParameterSpec.Builder(
                        KEY_ALIAS,
                        KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                    )
                        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                        .setRandomizedEncryptionRequired(true)
                        .build(),
                )
            }
            .generateKey()
    }
}
