package com.remote60.androiddirect

import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock

/**
 * What a sign-in, a return and a sign-out do with the stored sign-in.
 *
 * The same rules the PC client has (login_flow.hpp), for the same reasons. A sign-in that
 * succeeded leaves the phone holding a DEVICE CREDENTIAL the directory issued -- never the
 * password -- which gets a session later without anything being typed. Every use replaces it.
 * Signing out ends it, here at once and at the directory as soon as the directory can be told.
 *
 * Nothing here is Android. The vault is an interface and so are the directory's calls, which is
 * what lets "the answer arrived after the sign-out" be tested on a JVM: a real server cannot be
 * told to be slow at exactly that moment. The vault the app uses is [KeystoreLoginVault].
 *
 * Every function blocks -- on the lock, for a bounded time, and on the network -- and is meant
 * to be called off the main thread.
 */
object LoginFlow {

    /** What is stored. [serverOrigin] is [DirectoryClient.originKey] of the server that issued it. */
    data class StoredLogin(
        val serverOrigin: String,
        val accountId: String,
        val deviceId: String,
        val deviceCredential: String,
        val revokeToken: String,
    ) {
        /** Never the secrets: this is what ends up in a log line if one is ever built from it. */
        override fun toString(): String = "StoredLogin(device=${tag(deviceId)})"
    }

    /** A sign-out the directory has not been told about yet. */
    data class OwedSignOut(val serverOrigin: String, val deviceId: String, val revokeToken: String) {
        override fun toString(): String = "OwedSignOut(device=${tag(deviceId)})"
    }

    sealed class VaultRead {
        object None : VaultRead()
        data class Ok(val login: StoredLogin) : VaultRead()
        /** There is something stored and it cannot be used: damaged, or the key is gone. */
        data class Unreadable(val why: String) : VaultRead()
    }

    /** Where the stored sign-in lives. Every call is made under [LoginFlow]'s lock. */
    interface Vault {
        fun load(): VaultRead
        fun save(login: StoredLogin): Boolean
        /** True when nothing is stored afterwards, whether or not anything was before. */
        fun erase(): Boolean
        fun generation(): Long
        /** Moves the counter and returns the new value; 0 when it could not be written. */
        fun bumpGeneration(): Long
        fun owed(): List<OwedSignOut>
        /** Adds one, replacing an entry for the same device; drops the oldest past the bound. */
        fun addOwed(owed: OwedSignOut): Boolean
        fun removeOwed(deviceId: String): Boolean
    }

    /** How many sign-outs can be waiting to be told to the directory. */
    const val MAX_OWED = 16

    /** What became of a call, in the terms the caller has to act on. */
    enum class Call {
        OK,
        /** 401: the credential is not accepted. Erase, ask to sign in. */
        REJECTED,
        /** 404/405: a directory from before device credentials. Keep, do without. */
        UNSUPPORTED,
        /** 429: asked too often. Keep, try later. */
        LIMITED,
        /** Anything else that answered: 5xx, a redirect, a body that makes no sense. */
        FAILED,
        /** Nothing answered. */
        UNREACHABLE,
    }

    data class Refreshed(val call: Call, val sessionToken: String = "", val deviceCredential: String = "")

    interface Directory {
        fun refresh(deviceId: String, deviceCredential: String): Refreshed
        fun revoke(deviceId: String, revokeToken: String): Call
    }

    enum class Return {
        SIGNED_IN, NO_CREDENTIAL, SIGNED_OUT, REJECTED, UNREADABLE, SERVER_CANNOT, TRY_LATER, BUSY,
        NOT_SAVED,
    }

    data class ReturnResult(
        val outcome: Return,
        val sessionToken: String = "",
        val accountId: String = "",
        val deviceId: String = "",
        val detail: String = "",
    ) {
        override fun toString(): String = "ReturnResult($outcome device=${tag(deviceId)})"
    }

    enum class Remembered { STORED, NOT_ISSUED, SUPERSEDED, NOT_SAVED }

    data class SignedIn(
        val sessionToken: String,
        val deviceId: String,
        val deviceCredential: String,
        val revokeToken: String,
    ) {
        val issued: Boolean
            get() = deviceId.isNotEmpty() && deviceCredential.isNotEmpty() && revokeToken.isNotEmpty()

        override fun toString(): String = "SignedIn(device=${tag(deviceId)})"
    }

    data class SignOutResult(
        val localDone: Boolean,
        val serverTold: Boolean,
        val owed: Boolean,
        val detail: String = "",
    )

    /**
     * One lock for the app. The app is one process, so a lock in memory covers everything that
     * can reach the vault; the counter in the vault is what outlives the process.
     */
    private val lock = ReentrantLock()

    private inline fun <T> locked(waitMs: Long, busy: () -> T, body: () -> T): T {
        if (!lock.tryLock(waitMs, TimeUnit.MILLISECONDS)) return busy()
        try {
            return body()
        } finally {
            lock.unlock()
        }
    }

    /** How a device is named in a log: enough to tell two apart, not enough to use. */
    fun tag(deviceId: String): String = if (deviceId.isEmpty()) "-" else deviceId.take(8)

    private fun settled(call: Call): Boolean =
        // REJECTED: the directory does not accept the revoke token, which is also what it
        // answers for a device it no longer has. UNSUPPORTED: it issues no devices to end.
        call == Call.OK || call == Call.REJECTED || call == Call.UNSUPPORTED

    // ------------------------------------------------------------------ coming back

    /** One attempt to come back signed in: lock, read, refresh, store, unlock. */
    fun comeBack(
        vault: Vault,
        directory: Directory,
        serverOrigin: String,
        log: (String) -> Unit = {},
        lockWaitMs: Long = 8000,
    ): ReturnResult = locked(lockWaitMs, busy = {
        log("sign-in store: could not be had in time; nothing was sent")
        ReturnResult(Return.BUSY, detail = "busy")
    }) {
        val stored = when (val read = vault.load()) {
            is VaultRead.None -> return@locked ReturnResult(Return.NO_CREDENTIAL)
            is VaultRead.Unreadable -> {
                // Left where it is; the next sign-in that succeeds writes over it.
                log("sign-in store: the stored sign-in cannot be used: ${read.why}")
                return@locked ReturnResult(Return.UNREADABLE, detail = read.why)
            }
            is VaultRead.Ok -> read.login
        }
        if (stored.serverOrigin != serverOrigin) {
            log("sign-in store: the stored sign-in was issued by another server; kept, not used")
            return@locked ReturnResult(Return.NO_CREDENTIAL, accountId = stored.accountId,
                deviceId = stored.deviceId)
        }
        if (vault.owed().any { it.deviceId == stored.deviceId }) {
            // A sign-out got as far as being written down. It is finished, not undone.
            vault.bumpGeneration()
            val erased = vault.erase()
            log("sign-in store: device ${tag(stored.deviceId)} has a sign-out owed; its credential " +
                (if (erased) "is erased" else "COULD NOT BE ERASED") + " and it is not used")
            return@locked ReturnResult(Return.SIGNED_OUT, accountId = stored.accountId,
                deviceId = stored.deviceId)
        }

        val refreshed = directory.refresh(stored.deviceId, stored.deviceCredential)
        when (refreshed.call) {
            Call.OK -> {
                if (refreshed.sessionToken.isEmpty() || refreshed.deviceCredential.isEmpty()) {
                    log("sign-in store: device ${tag(stored.deviceId)}: an answer without the next credential")
                    return@locked ReturnResult(Return.TRY_LATER, accountId = stored.accountId,
                        deviceId = stored.deviceId)
                }
                if (!vault.save(stored.copy(deviceCredential = refreshed.deviceCredential))) {
                    // The directory replaced the credential and the replacement is not stored.
                    // The session is not used on the strength of a credential about to be lost.
                    log("sign-in store: device ${tag(stored.deviceId)}: refreshed, but the new " +
                        "credential could not be stored")
                    return@locked ReturnResult(Return.NOT_SAVED, accountId = stored.accountId,
                        deviceId = stored.deviceId)
                }
                log("sign-in store: device ${tag(stored.deviceId)}: signed in without a password")
                ReturnResult(Return.SIGNED_IN, refreshed.sessionToken, stored.accountId, stored.deviceId)
            }
            Call.REJECTED -> {
                vault.bumpGeneration()
                val erased = vault.erase()
                log("sign-in store: device ${tag(stored.deviceId)}: the directory refused the " +
                    "credential; it " + (if (erased) "is erased" else "COULD NOT BE ERASED"))
                ReturnResult(Return.REJECTED, accountId = stored.accountId, deviceId = stored.deviceId)
            }
            Call.UNSUPPORTED -> {
                log("sign-in store: the directory has no refresh route; the credential is kept")
                ReturnResult(Return.SERVER_CANNOT, accountId = stored.accountId,
                    deviceId = stored.deviceId)
            }
            Call.LIMITED, Call.FAILED, Call.UNREACHABLE -> {
                log("sign-in store: device ${tag(stored.deviceId)}: no answer about the credential " +
                    "(${refreshed.call}); it is kept")
                ReturnResult(Return.TRY_LATER, accountId = stored.accountId,
                    deviceId = stored.deviceId, detail = refreshed.call.name)
            }
        }
    }

    // ------------------------------------------------------------------ signing in

    /**
     * Marks the start of a sign-in the user asked for; returns the generation it runs under.
     * Moving the counter is what makes every answer still in flight late. 0 = not remembered.
     */
    fun beginSignIn(vault: Vault, log: (String) -> Unit = {}, lockWaitMs: Long = 8000): Long =
        locked(lockWaitMs, busy = {
            log("sign-in store: could not be had in time; this sign-in will not be remembered")
            0L
        }) { vault.bumpGeneration() }

    /**
     * Stores what a successful sign-in was given -- if this sign-in is still the latest thing
     * that happened to the vault. When it is not, the credential is not stored and the device
     * it belongs to is ended; when the directory cannot be told, that is recorded as owed.
     *
     * Only [Remembered.STORED] and [Remembered.NOT_ISSUED] mean the session may be used.
     */
    fun rememberSignIn(
        vault: Vault,
        directory: Directory,
        serverOrigin: String,
        generation: Long,
        accountId: String,
        signedIn: SignedIn,
        log: (String) -> Unit = {},
        lockWaitMs: Long = 8000,
    ): Remembered {
        if (!signedIn.issued) return Remembered.NOT_ISSUED
        var why = "something happened to the store after this sign-in began"
        val outcome = locked(lockWaitMs, busy = {
            why = "the store could not be had"
            Remembered.NOT_SAVED
        }) {
            if (generation == 0L) {
                why = "the store could not be had when the sign-in began"
                return@locked Remembered.NOT_SAVED
            }
            if (vault.generation() != generation) return@locked Remembered.SUPERSEDED
            // A sign-in made on purpose starts a new family; the one before it is ended.
            val previous = vault.load()
            if (previous is VaultRead.Ok && previous.login.serverOrigin == serverOrigin &&
                previous.login.deviceId != signedIn.deviceId) {
                vault.addOwed(OwedSignOut(serverOrigin, previous.login.deviceId,
                    previous.login.revokeToken))
            }
            val saved = vault.save(StoredLogin(serverOrigin, accountId, signedIn.deviceId,
                signedIn.deviceCredential, signedIn.revokeToken))
            if (saved) {
                Remembered.STORED
            } else {
                why = "the credential could not be written"
                Remembered.NOT_SAVED
            }
        }
        if (outcome == Remembered.STORED) {
            log("sign-in store: device ${tag(signedIn.deviceId)}: stored")
            return outcome
        }
        // Not kept. Nothing is left that would sign this device in later unasked.
        val told = directory.revoke(signedIn.deviceId, signedIn.revokeToken)
        if (settled(told)) {
            log("sign-in store: device ${tag(signedIn.deviceId)} was not kept ($why) and the " +
                "directory was told: $told")
        } else {
            val recorded = locked(lockWaitMs, busy = { false }) {
                vault.addOwed(OwedSignOut(serverOrigin, signedIn.deviceId, signedIn.revokeToken))
            }
            log("sign-in store: device ${tag(signedIn.deviceId)} was not kept ($why); the " +
                "directory could not be told ($told) and that is " +
                (if (recorded) "recorded as owed" else "NOT recorded"))
        }
        return outcome
    }

    // ------------------------------------------------------------------ signing out

    /**
     * Signs this device out. The counter moves, the sign-out is written down as owed, the
     * credential is erased -- under the lock -- and only then is the directory told.
     */
    fun signOut(
        vault: Vault,
        directory: Directory,
        serverOrigin: String,
        log: (String) -> Unit = {},
        lockWaitMs: Long = 8000,
    ): SignOutResult {
        var stored: StoredLogin? = null
        var owed = false
        val local = locked(lockWaitMs, busy = {
            log("sign-in store: could not be had in time; the credential was not erased")
            SignOutResult(false, false, false, "busy")
        }) {
            vault.bumpGeneration()
            val read = vault.load()
            if (read is VaultRead.Ok && read.login.serverOrigin != serverOrigin) {
                // Another server's. Signing out of this one does not touch it.
                return@locked SignOutResult(true, false, false, "nothing of this server's was stored")
            }
            if (read is VaultRead.Ok) {
                stored = read.login
                // Written down BEFORE the credential goes: if the app dies between the two,
                // the credential is there with a record saying it is not to be used.
                owed = vault.addOwed(OwedSignOut(serverOrigin, read.login.deviceId,
                    read.login.revokeToken))
            }
            SignOutResult(vault.erase(), false, owed)
        }
        val ended = stored ?: return local
        if (!local.localDone) return local

        val told = directory.revoke(ended.deviceId, ended.revokeToken)
        if (settled(told)) {
            val cleared = locked(lockWaitMs, busy = { false }) { vault.removeOwed(ended.deviceId) }
            log("sign-in store: device ${tag(ended.deviceId)}: signed out; the directory answered $told")
            return SignOutResult(true, told == Call.OK, !cleared)
        }
        log("sign-in store: device ${tag(ended.deviceId)}: signed out here; the directory could " +
            "not be told ($told) and that is " + (if (owed) "recorded as owed" else "NOT recorded"))
        return SignOutResult(true, false, owed, "the server could not be told yet")
    }

    /** Tells the directory about every sign-out still owed; returns how many remain. */
    fun settleOwedSignOuts(
        vault: Vault,
        directory: Directory,
        serverOrigin: String,
        log: (String) -> Unit = {},
        lockWaitMs: Long = 8000,
    ): Int {
        val owed = locked(lockWaitMs, busy = { emptyList() }) { vault.owed() }
        var remaining = 0
        for (one in owed) {
            if (one.serverOrigin != serverOrigin) {
                ++remaining   // owed to another server; kept for when that one is the server
                continue
            }
            val told = directory.revoke(one.deviceId, one.revokeToken)
            if (settled(told)) {
                val cleared = locked(lockWaitMs, busy = { false }) { vault.removeOwed(one.deviceId) }
                if (!cleared) ++remaining
                log("sign-in store: device ${tag(one.deviceId)}: an owed sign-out was settled ($told)")
            } else {
                ++remaining
                log("sign-in store: device ${tag(one.deviceId)}: a sign-out is still owed ($told)")
            }
        }
        return remaining
    }
}
