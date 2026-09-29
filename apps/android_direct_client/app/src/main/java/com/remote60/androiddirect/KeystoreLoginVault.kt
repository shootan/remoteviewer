package com.remote60.androiddirect

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Where the phone keeps what lets it come back signed in.
 *
 * A preferences file of its own ([PREFS]), holding two values encrypted with a key that lives in
 * the Android Keystore and never leaves it:
 *
 *   login   the device credential the directory issued at sign-in
 *   owed    sign-outs the directory has not been told about yet, a bounded list -- one that
 *           cannot be read is never written over, and is only set aside (owed.unreadable)
 *           by a sign-in the user makes
 *
 * and one that is not a secret: the counter a late answer is checked against.
 *
 * The password is not among them, and neither is a session: a session lives in memory
 * ([DirectoryClient.session]) and ends with the process.
 *
 * WHAT THE PROTECTION IS AND IS NOT. The key cannot be read out of the phone, so a copy of the
 * preferences file -- from a backup, or from the file system of a rooted phone -- does not open
 * without the phone itself. The file is also left out of backups (res/xml/backup_rules.xml,
 * data_extraction_rules.xml): restored onto another phone it would be unreadable there anyway,
 * and unreadable is handled, but a credential has no business travelling. A program running as
 * this app can use the key; nothing here claims otherwise.
 *
 * When the key is gone -- a restore, a factory reset of the keystore -- what is stored cannot be
 * opened. That is [LoginFlow.VaultRead.Unreadable] and the app asks for the password.
 */
class KeystoreLoginVault(context: Context) : LoginFlow.Vault {

    private val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    override fun load(): LoginFlow.VaultRead {
        val sealed = prefs.getString(KEY_LOGIN, null) ?: return LoginFlow.VaultRead.None
        val plain = open(sealed) ?: return LoginFlow.VaultRead.Unreadable("could not be opened on this phone")
        return try {
            val json = JSONObject(plain)
            val login = LoginFlow.StoredLogin(
                serverOrigin = json.getString("origin"),
                accountId = json.getString("account"),
                deviceId = json.getString("device"),
                deviceCredential = json.getString("credential"),
                revokeToken = json.getString("revoke"),
            )
            if (login.serverOrigin.isEmpty() || login.accountId.isEmpty() || login.deviceId.isEmpty() ||
                login.deviceCredential.isEmpty() || login.revokeToken.isEmpty()) {
                LoginFlow.VaultRead.Unreadable("is not a sign-in credential")
            } else {
                LoginFlow.VaultRead.Ok(login)
            }
        } catch (e: Exception) {
            LoginFlow.VaultRead.Unreadable("is not a sign-in credential")
        }
    }

    override fun save(login: LoginFlow.StoredLogin): Boolean {
        val plain = JSONObject()
            .put("origin", login.serverOrigin)
            .put("account", login.accountId)
            .put("device", login.deviceId)
            .put("credential", login.deviceCredential)
            .put("revoke", login.revokeToken)
            .toString()
        val sealed = seal(plain) ?: return false
        // commit, not apply: the caller tells the user they are signed in on the strength of
        // this having been written, and apply() reports nothing.
        return prefs.edit().putString(KEY_LOGIN, sealed).commit()
    }

    override fun erase(): Boolean = prefs.edit().remove(KEY_LOGIN).commit()

    override fun generation(): Long = prefs.getLong(KEY_GENERATION, 0L)

    override fun bumpGeneration(): Long {
        val next = generation() + 1
        return if (prefs.edit().putLong(KEY_GENERATION, next).commit()) next else 0L
    }

    override fun owed(): LoginFlow.OwedRead {
        val sealed = prefs.getString(KEY_OWED, null) ?: return LoginFlow.OwedRead.Ok(emptyList())
        val plain = open(sealed) ?: return LoginFlow.OwedRead.Unreadable("could not be opened on this phone")
        return try {
            val array = JSONArray(plain)
            val list = (0 until array.length()).map { i ->
                val item = array.getJSONObject(i)
                val owed = LoginFlow.OwedSignOut(
                    item.getString("origin"), item.getString("device"), item.getString("revoke"))
                if (owed.serverOrigin.isEmpty() || owed.deviceId.isEmpty() ||
                    owed.revokeToken.isEmpty()) {
                    return LoginFlow.OwedRead.Unreadable("holds an entry that is not a sign-out")
                }
                owed
            }
            LoginFlow.OwedRead.Ok(list)
        } catch (e: Exception) {
            LoginFlow.OwedRead.Unreadable("is not a list of sign-outs")
        }
    }

    override fun addOwed(owed: LoginFlow.OwedSignOut): Boolean {
        // A list that cannot be read is not written over: what it held may be the sign-out of
        // the credential that is stored, and with it gone that credential would be used again.
        // A full one drops nothing to make room: every entry is a device still alive.
        val read = owed() as? LoginFlow.OwedRead.Ok ?: return false
        val others = read.list.filter { it.deviceId != owed.deviceId }
        if (others.size >= LoginFlow.MAX_OWED) return false
        return writeOwed(others + owed)
    }

    override fun removeOwed(deviceId: String): Boolean {
        val read = owed() as? LoginFlow.OwedRead.Ok ?: return false
        return writeOwed(read.list.filter { it.deviceId != deviceId })
    }

    override fun setAsideOwed(): Boolean {
        if (owed() !is LoginFlow.OwedRead.Unreadable) return false
        val sealed = prefs.getString(KEY_OWED, null) ?: return false
        // Kept as it was, in one entry, beside the list: never read again by the app, and
        // there to be looked at. A second set-aside replaces the first.
        return prefs.edit().putString(KEY_OWED_UNREADABLE, sealed).remove(KEY_OWED).commit()
    }

    private fun writeOwed(list: List<LoginFlow.OwedSignOut>): Boolean {
        if (list.isEmpty()) return prefs.edit().remove(KEY_OWED).commit()
        val array = JSONArray()
        for (one in list) {
            array.put(JSONObject().put("origin", one.serverOrigin).put("device", one.deviceId)
                .put("revoke", one.revokeToken))
        }
        val sealed = seal(array.toString()) ?: return false
        return prefs.edit().putString(KEY_OWED, sealed).commit()
    }

    // ------------------------------------------------------------------ the key

    private fun key(create: Boolean): SecretKey? = try {
        val store = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (store.getKey(KEY_ALIAS, null) as? SecretKey) ?: if (!create) null else {
            val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
            generator.init(
                KeyGenParameterSpec.Builder(
                    KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    // Coming back signed in happens when the app starts, with nobody there to
                    // present a fingerprint. The lock screen is the device's own protection.
                    .setUserAuthenticationRequired(false)
                    .build())
            generator.generateKey()
        }
    } catch (e: Exception) {
        null
    }

    /** "iv:ciphertext", both base64. A fresh iv every time, chosen by the keystore. */
    private fun seal(plain: String): String? = try {
        val secret = key(create = true)
        if (secret == null) {
            null
        } else {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, secret)
            val sealed = cipher.doFinal(plain.toByteArray(Charsets.UTF_8))
            Base64.encodeToString(cipher.iv, Base64.NO_WRAP) + ":" +
                Base64.encodeToString(sealed, Base64.NO_WRAP)
        }
    } catch (e: Exception) {
        null
    }

    private fun open(sealed: String): String? = try {
        val split = sealed.indexOf(':')
        val secret = key(create = false)
        if (split <= 0 || secret == null) {
            null
        } else {
            val iv = Base64.decode(sealed.substring(0, split), Base64.NO_WRAP)
            val body = Base64.decode(sealed.substring(split + 1), Base64.NO_WRAP)
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, secret, GCMParameterSpec(128, iv))
            String(cipher.doFinal(body), Charsets.UTF_8)
        }
    } catch (e: Exception) {
        // Tampered with, truncated, or sealed with a key this phone no longer has.
        null
    }

    companion object {
        /** Named in res/xml/backup_rules.xml and data_extraction_rules.xml; keep the three in step. */
        const val PREFS = "gnlink_login"
        const val KEY_ALIAS = "gnlink.login.v1"
        private const val KEYSTORE = "AndroidKeyStore"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val KEY_LOGIN = "login"
        private const val KEY_OWED = "owed"
        private const val KEY_OWED_UNREADABLE = "owed.unreadable"
        private const val KEY_GENERATION = "generation"
    }
}
