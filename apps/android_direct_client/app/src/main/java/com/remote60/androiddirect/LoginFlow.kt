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
 * One rule runs through this file: WHEN SOMETHING COULD NOT BE WRITTEN, THE STEP AFTER IT IS NOT
 * TAKEN. The counter, the record of a sign-out owed and the credential are each what makes the
 * next step safe, and each write reports whether it happened. None of those reports is dropped.
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

    /** The list of sign-outs owed. Nothing stored is an empty [Ok]. */
    sealed class OwedRead {
        data class Ok(val list: List<OwedSignOut>) : OwedRead()
        /**
         * Something is stored and cannot be read. It is NOT an empty list: what it held may
         * include the sign-out of the very credential that is stored.
         */
        data class Unreadable(val why: String) : OwedRead()
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
        fun owed(): OwedRead
        /**
         * Adds one, replacing an entry for the same device. Refused -- false, nothing written --
         * when the list cannot be read (it is not written over) or already holds [MAX_OWED]
         * other devices (none is dropped to make room).
         */
        fun addOwed(owed: OwedSignOut): Boolean
        fun removeOwed(deviceId: String): Boolean
        /**
         * Only when the list cannot be read: keeps it under another name and leaves no list, so
         * that a new one can be started. False when that could not be done, or there was
         * nothing unreadable to set aside.
         */
        fun setAsideOwed(): Boolean
    }

    /** How many sign-outs can be waiting to be told to the directory. Past this, none is added. */
    const val MAX_OWED = 16

    /** What became of a call, in the terms the caller has to act on. */
    enum class Call {
        OK,
        /** 401: the credential is not accepted. Erase, ask to sign in. */
        REJECTED,
        /**
         * 404/405: a directory from before device credentials. A refresh does without. A
         * sign-out is NOT done: the device is alive again when a newer server is back.
         */
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
        /** What is stored is another account's than the one asked for. Not presented. */
        OTHER_ACCOUNT,
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

    /**
     * [localDone] false: the sign-out was NOT made and the stored sign-in is still there. The
     * caller says so; it does not show the user signed out.
     */
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

    /**
     * Whether the directory's answer means there is nothing left to send for this device.
     * REJECTED: it does not accept the revoke token, which is also what it answers for a device
     * it no longer has. NOT UNSUPPORTED: a 404 says the server in front of us has no such route
     * -- one from before device credentials, put back in a rollback -- and the device is still
     * in its store, alive again the moment the newer server returns.
     */
    private fun endedAtDirectory(call: Call): Boolean = call == Call.OK || call == Call.REJECTED

    /**
     * Ends a device the directory issued and this app will not keep. Told now if it can be,
     * written down as owed if it cannot -- and if neither, that is said.
     */
    private fun endUnkeptDevice(
        vault: Vault, directory: Directory, serverOrigin: String, signedIn: SignedIn, why: String,
        log: (String) -> Unit, lockWaitMs: Long,
    ) {
        val told = directory.revoke(signedIn.deviceId, signedIn.revokeToken)
        if (endedAtDirectory(told)) {
            log("sign-in store: device ${tag(signedIn.deviceId)} was not kept ($why) and the " +
                "directory was told: $told")
            return
        }
        val recorded = locked(lockWaitMs, busy = { false }) {
            vault.addOwed(OwedSignOut(serverOrigin, signedIn.deviceId, signedIn.revokeToken))
        }
        log("sign-in store: device ${tag(signedIn.deviceId)} was not kept ($why); the directory " +
            "could not be told ($told) and that is " +
            (if (recorded) "recorded as owed"
             else "NOT recorded: the device stays alive at the directory until it expires"))
    }

    // ------------------------------------------------------------------ coming back

    /**
     * One attempt to come back signed in: lock, read, refresh, store, unlock.
     *
     * [onlyForAccount], when not empty, is the account a screen already shows: a sign-in stored
     * since by another account is not presented for it ([Return.OTHER_ACCOUNT]).
     */
    fun comeBack(
        vault: Vault,
        directory: Directory,
        serverOrigin: String,
        log: (String) -> Unit = {},
        lockWaitMs: Long = 8000,
        onlyForAccount: String = "",
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
        if (onlyForAccount.isNotEmpty() && stored.accountId != onlyForAccount) {
            // Somebody signed in as another account since this screen did. The session it would
            // get is theirs, shown under this screen's name. Not presented; left for its owner.
            log("sign-in store: the stored sign-in belongs to another account than this screen's; " +
                "it is not used")
            return@locked ReturnResult(Return.OTHER_ACCOUNT, accountId = stored.accountId,
                deviceId = stored.deviceId, detail = "another account is signed in on this device")
        }
        val owed = when (val list = vault.owed()) {
            is OwedRead.Unreadable -> {
                // Whether this credential was signed out of is written in a list that cannot be
                // read. It may have been. Nothing is presented on the strength of not knowing.
                log("sign-in store: the list of owed sign-outs cannot be read; the stored " +
                    "credential is not used")
                return@locked ReturnResult(Return.UNREADABLE, accountId = stored.accountId,
                    deviceId = stored.deviceId, detail = "the list of sign-outs cannot be read")
            }
            is OwedRead.Ok -> list.list
        }
        if (owed.any { it.deviceId == stored.deviceId }) {
            // A sign-out got as far as being written down. It is finished, not undone -- but
            // only behind the counter: without that, an answer to a sign-in from before the
            // sign-out could still store itself.
            val barrier = vault.bumpGeneration() != 0L
            val erased = barrier && vault.erase()
            log("sign-in store: device ${tag(stored.deviceId)} has a sign-out owed; its credential is " +
                (if (erased) "erased" else if (barrier) "STILL STORED (could not be erased)"
                 else "left stored (the counter could not be written)") + " and it is not used")
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
                // Erased behind the counter, like every other way a credential leaves; if the
                // counter cannot be written it stays, and is refused again.
                val barrier = vault.bumpGeneration() != 0L
                val erased = barrier && vault.erase()
                log("sign-in store: device ${tag(stored.deviceId)}: the directory refused the " +
                    "credential; it is " + (if (erased) "erased" else if (barrier)
                        "STILL STORED (could not be erased)" else "left stored (the counter could not be written)"))
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
     * Moving the counter is what makes every answer still in flight late.
     *
     * 0: the counter could not be had or written. THE SIGN-IN IS NOT MADE -- no request is sent.
     * Without the counter nothing could tell its answer from one that arrives after a sign-out.
     */
    fun beginSignIn(vault: Vault, log: (String) -> Unit = {}, lockWaitMs: Long = 8000): Long =
        locked(lockWaitMs, busy = {
            log("sign-in store: could not be had in time; the sign-in is not made")
            0L
        }) {
            val generation = vault.bumpGeneration()
            if (generation == 0L) log("sign-in store: the counter could not be written; the sign-in is not made")
            generation
        }

    /**
     * Stores what a successful sign-in was given -- if this sign-in is still the latest thing
     * that happened to the vault. When it is not, nothing is stored and the device it was
     * issued is ended; when the directory cannot be told, that is recorded as owed.
     *
     * Asked whether or not the directory issued anything: an answer with nothing issued is late
     * like any other. The sign-in stored before it is ended either way -- written down as owed,
     * or, when that cannot be written, ended at the directory first; never simply written over.
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
        var outcome = Remembered.SUPERSEDED
        var why = "something happened to the store after this sign-in began"
        var previousDevice = ""
        // A device that could not be written down as owed and was ended at the directory
        // instead, on the first pass. The second pass finds it still stored and knows.
        var endedAtDirectory = ""
        var toTell: StoredLogin? = null

        for (pass in 0 until 2) {
            toTell = null
            val done = locked(lockWaitMs, busy = {
                outcome = Remembered.NOT_SAVED
                why = "the store could not be had"
                true
            }) inner@{
                if (generation == 0L) {
                    outcome = Remembered.NOT_SAVED
                    why = "the store could not be had when the sign-in began"
                    return@inner true
                }
                if (vault.generation() != generation) {
                    outcome = Remembered.SUPERSEDED
                    return@inner true
                }
                // The user is here and has just proved who they are. A list that cannot be read
                // is set aside -- kept -- so that a new one can be started. Whether or not a
                // sign-in is stored beside it: left unreadable, it blocks every return after this.
                if (vault.owed() is OwedRead.Unreadable && vault.setAsideOwed()) {
                    log("sign-in store: the list of owed sign-outs could not be read; it is " +
                        "kept aside and a new one is started")
                }
                val read = vault.load()
                val previous = (read as? VaultRead.Ok)?.login?.takeIf {
                    it.serverOrigin == serverOrigin && it.deviceId != signedIn.deviceId
                }
                if (previous != null && previous.deviceId != endedAtDirectory) {
                    if (!vault.addOwed(OwedSignOut(previous.serverOrigin, previous.deviceId,
                            previous.revokeToken))) {
                        if (pass == 1) {
                            outcome = Remembered.NOT_SAVED
                            why = "the sign-in it replaces could not be written down as owed"
                            return@inner true
                        }
                        log("sign-in store: device ${tag(previous.deviceId)}: could not be written " +
                            "down as owed; the directory is told before it is replaced")
                        toTell = previous
                        return@inner false   // out of the lock for the call, then round again
                    }
                }
                if (previous != null) previousDevice = previous.deviceId

                // The counter moves again before anything is stored or erased: it makes any
                // second answer to this sign-in late, and it is the test of whether the counter
                // can be written at all. A sign-out that could not write it did not happen.
                if (vault.bumpGeneration() == 0L) {
                    outcome = Remembered.NOT_SAVED
                    why = "the counter could not be written"
                    return@inner true
                }
                if (!signedIn.issued) {
                    // The directory issued nothing to keep. What WAS kept still goes.
                    if (previous != null && !vault.erase()) {
                        outcome = Remembered.NOT_SAVED
                        why = "the sign-in it replaces could not be erased"
                        return@inner true
                    }
                    outcome = Remembered.NOT_ISSUED
                    return@inner true
                }
                if (vault.save(StoredLogin(serverOrigin, accountId, signedIn.deviceId,
                        signedIn.deviceCredential, signedIn.revokeToken))) {
                    outcome = Remembered.STORED
                } else {
                    outcome = Remembered.NOT_SAVED
                    why = "the credential could not be written"
                }
                true
            }
            if (done) break
            val previous = toTell ?: break
            val told = directory.revoke(previous.deviceId, previous.revokeToken)
            if (!endedAtDirectory(told)) {
                outcome = Remembered.NOT_SAVED
                why = "the sign-in it replaces could neither be written down as owed nor ended " +
                    "at the directory"
                break
            }
            endedAtDirectory = previous.deviceId
        }

        if (outcome == Remembered.STORED || outcome == Remembered.NOT_ISSUED) {
            log("sign-in store: " + (if (signedIn.issued) "device ${tag(signedIn.deviceId)}: stored"
                else "the directory issued no credential; nothing is stored") +
                (if (previousDevice.isEmpty()) "" else "; device ${tag(previousDevice)} before it is " +
                    (if (previousDevice == endedAtDirectory) "ended" else "owed a sign-out")))
            return outcome
        }
        if (signedIn.issued) {
            endUnkeptDevice(vault, directory, serverOrigin, signedIn, why, log, lockWaitMs)
        } else {
            log("sign-in store: the sign-in is not used: $why")
        }
        return outcome
    }

    /**
     * Ends the device a sign-in was issued, for a caller that has decided not to use it: the
     * screen signed out while it was in flight, or what had to follow it failed.
     */
    fun discardSignIn(
        vault: Vault, directory: Directory, serverOrigin: String, signedIn: SignedIn, why: String,
        log: (String) -> Unit = {}, lockWaitMs: Long = 8000,
    ) {
        if (signedIn.deviceId.isEmpty() || signedIn.revokeToken.isEmpty()) return
        endUnkeptDevice(vault, directory, serverOrigin, signedIn, why, log, lockWaitMs)
    }

    // ------------------------------------------------------------------ signing out

    /**
     * Signs this device out. Under the lock: the counter moves (or nothing is done), the
     * sign-out is written down as owed, and only if it was is the credential erased. Then the
     * directory is told.
     *
     * When the sign-out cannot be written down, the credential is kept -- it holds the only
     * means of ending the device -- unless the directory itself confirms the device is ended.
     */
    fun signOut(
        vault: Vault,
        directory: Directory,
        serverOrigin: String,
        log: (String) -> Unit = {},
        lockWaitMs: Long = 8000,
    ): SignOutResult {
        var stored: StoredLogin? = null
        var recorded = false
        val local = locked(lockWaitMs, busy = {
            log("sign-in store: could not be had in time; the credential was not erased")
            SignOutResult(false, false, false, "busy")
        }) {
            // The counter first: from here every answer still in flight is late. If it cannot
            // be written, a sign-in answered a moment from now would store itself over this
            // sign-out. So nothing else is done.
            if (vault.bumpGeneration() == 0L) {
                log("sign-in store: the counter could not be written; the sign-out is not made")
                return@locked SignOutResult(false, false, false, "the sign-out could not be recorded")
            }
            when (val read = vault.load()) {
                is VaultRead.None -> SignOutResult(true, false, false, "nothing was stored")
                is VaultRead.Unreadable -> {
                    // It opens nothing and names no device to end. It goes.
                    val erased = vault.erase()
                    log("sign-in store: the stored sign-in could not be read (${read.why}); " +
                        (if (erased) "erased" else "COULD NOT BE ERASED"))
                    SignOutResult(erased, false, false,
                        if (erased) "what was stored could not be read, and is removed"
                        else "the stored sign-in could not be removed")
                }
                is VaultRead.Ok -> {
                    if (read.login.serverOrigin != serverOrigin) {
                        // Another server's. Signing out of this one does not touch it.
                        return@locked SignOutResult(true, false, false,
                            "nothing of this server's was stored")
                    }
                    stored = read.login
                    // Written down BEFORE the credential goes, and the credential goes only if
                    // it was. If the app dies between the two, the credential is there with a
                    // record saying it is not to be used, which comeBack reads first.
                    recorded = vault.addOwed(OwedSignOut(serverOrigin, read.login.deviceId,
                        read.login.revokeToken))
                    when {
                        !recorded -> SignOutResult(false, false, false,
                            "the sign-out could not be written down")
                        !vault.erase() -> SignOutResult(false, false, true,
                            "the credential could not be erased")
                        else -> SignOutResult(true, false, true)
                    }
                }
            }
        }
        val ended = stored ?: return local

        val told = directory.revoke(ended.deviceId, ended.revokeToken)
        if (!recorded) {
            if (endedAtDirectory(told)) {
                // The lock was let go for the call; a sign-in may have stored a family of its
                // own meanwhile, alive at the directory. Only THIS device's credential goes.
                var replaced = false
                val erased = locked(lockWaitMs, busy = { false }) {
                    val now = vault.load()
                    val stillThis = now is VaultRead.Ok && now.login.serverOrigin == serverOrigin &&
                        now.login.deviceId == ended.deviceId
                    when {
                        stillThis -> vault.erase()
                        now is VaultRead.Unreadable -> false
                        else -> { replaced = true; true }
                    }
                }
                if (replaced) {
                    log("sign-in store: device ${tag(ended.deviceId)}: the directory ended it ($told); " +
                        "what is stored now is not it, and is left as it is")
                    return SignOutResult(true, told == Call.OK, false)
                }
                log("sign-in store: device ${tag(ended.deviceId)}: the directory ended it ($told); " +
                    "the credential is " + (if (erased) "erased" else "STILL STORED (could not be erased)"))
                return SignOutResult(erased, told == Call.OK, false,
                    if (erased) "" else "the stored sign-in could not be removed")
            }
            log("sign-in store: device ${tag(ended.deviceId)}: NOT signed out -- nothing could be " +
                "written down and the directory could not be told ($told); the credential is kept")
            return SignOutResult(false, false, false,
                "the sign-out could not be written down and the server could not be told")
        }
        if (endedAtDirectory(told)) {
            if (!local.localDone) {
                // The credential could not be erased. The record beside it is what keeps it from
                // being used, so the record stays even though the directory has been told.
                log("sign-in store: device ${tag(ended.deviceId)}: the directory answered $told; " +
                    "the record is kept because the credential is still stored")
                return SignOutResult(false, told == Call.OK, true, local.detail)
            }
            val cleared = locked(lockWaitMs, busy = { false }) { vault.removeOwed(ended.deviceId) }
            log("sign-in store: device ${tag(ended.deviceId)}: signed out; the directory answered $told")
            return SignOutResult(true, told == Call.OK, !cleared)
        }
        log("sign-in store: device ${tag(ended.deviceId)}: signed out here; the directory could " +
            "not be told ($told) and that is recorded as owed")
        return SignOutResult(local.localDone, false, true,
            local.detail.ifEmpty { "the server could not be told yet" })
    }

    /**
     * Tells the directory about every sign-out still owed; returns how many remain. A list that
     * cannot be read counts as one: something is owed that cannot be settled, which is not none.
     */
    fun settleOwedSignOuts(
        vault: Vault,
        directory: Directory,
        serverOrigin: String,
        log: (String) -> Unit = {},
        lockWaitMs: Long = 8000,
    ): Int {
        val owed = locked(lockWaitMs, busy = { OwedRead.Ok(emptyList()) }) { vault.owed() }
        val list = when (owed) {
            is OwedRead.Unreadable -> {
                log("sign-in store: the list of owed sign-outs cannot be read; nothing is sent " +
                    "and nothing is removed")
                return 1
            }
            is OwedRead.Ok -> owed.list
        }
        var remaining = 0
        for (one in list) {
            if (one.serverOrigin != serverOrigin) {
                ++remaining   // owed to another server; kept for when that one is the server
                continue
            }
            val told = directory.revoke(one.deviceId, one.revokeToken)
            if (endedAtDirectory(told)) {
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
