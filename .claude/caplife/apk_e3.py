A = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/'
K = A + 'main/java/com/remote60/androiddirect/'


def edit(p, reps, count_all=None):
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        if count_all and a in count_all:
            assert n == count_all[a], (p, a[:80], n)
        else:
            assert n == 1, (p, a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', p.rsplit('/', 1)[-1])


# ---------------------------------------------------------------- StandbyKeep.kt
edit(K + 'StandbyKeep.kt', [
    ("""    /**
     * nativeStandbyStatus: [exists, state, lost, handedOff, needsCapability, tailActive, strays]. r2 D1 (Codex
     * codex_ka_e_src_r1.md): [tailActive] = the old session's datagrams still reach the lease (what the host still sends
     * it, observed), [strays] = how many so far (the tail's cost, counted apart).
     */""", """    /**
     * nativeStandbyStatus: [exists, state, lost, handedOff, needsCapability, tailActive, strays, punches]. r2 D1 (Codex
     * codex_ka_e_src_r1.md): [tailActive] = datagrams from the target that are no standby traffic, HelloAck or Punch
     * still reach the lease (observed -- not a proof the host's grant ended, not a count of video), [strays] = how many
     * so far. r3 (codex_ka_e_src_r2.md D1): [punches] = the host's Punches, counted apart (r2 counted them as strays).
     */"""),
    ("""        val tailActive: Boolean = false,
        val strays: Long = 0L,
    ) {""", """        val tailActive: Boolean = false,
        val strays: Long = 0L,
        val punches: Long = 0L,
    ) {"""),
    ("""                    tailActive = v.size > 5 && v[5] != 0L, strays = if (v.size > 6) v[6] else 0L)""",
     """                    tailActive = v.size > 5 && v[5] != 0L, strays = if (v.size > 6) v[6] else 0L,
                    punches = if (v.size > 7) v[7] else 0L)"""),
    ("""    /** The native runner's key of a PC. */
    fun nativeKey(key: RecentHosts.Key): String = SessionPark.entryKey(key.directoryOrigin, key.accountId, key.hostId)""",
     """    /** The native runner's key of a PC. */
    fun nativeKey(key: RecentHosts.Key): String = SessionPark.entryKey(key.directoryOrigin, key.accountId, key.hostId)

    /**
     * r3 (Codex codex_ka_e_src_r2.md R2): the native runner is one per process, an app screen (Activity) is not. Each
     * screen is an owner with its own key prefix ("o<n>|", n never reused in this process); its leases are keyed under
     * it and its cleanup releases that prefix only -- a late cleanup of an old screen never takes the new screen's leases.
     */
    fun ownerPrefix(n: Long): String = "o$n|"
    fun ownerKey(ownerPrefix: String, key: RecentHosts.Key): String = ownerPrefix + nativeKey(key)"""),
])

# ---------------------------------------------------------------- NativeSessionBridge.kt
edit(K + 'NativeSessionBridge.kt', [
    ("""    external fun nativeStandbyReleaseAll()""",
     """    external fun nativeStandbyReleaseAll()
    // r3 R2: the leases whose key starts with [prefix] (one app screen's) -- no other
    external fun nativeStandbyReleaseOwner(prefix: String): Int"""),
])

# ---------------------------------------------------------------- native_bridge.cpp
edit(A + 'main/cpp/native_bridge.cpp', [
    ("""// [exists, state (StandbyEntryClient::State: 0 Idle, 1 Admitting, 2 Parked, 3 Activating, 4 Active, 5 Unsupported,
// 6 Waiting), lost count, handed off, needs a capability, tail active, strays] -- r2 D1: the last two are the old
// session's datagrams the lease still receives (observed) and how many so far""",
     """// [exists, state (StandbyEntryClient::State: 0 Idle, 1 Admitting, 2 Parked, 3 Activating, 4 Active, 5 Unsupported,
// 6 Waiting), lost count, handed off, needs a capability, tail active, strays, punches] -- r2 D1: datagrams from the
// target that are no standby traffic / HelloAck / Punch, still arriving (observed) and how many so far; r3: the host's
// Punches, counted apart"""),
    ("""  const jlong v[7] = {st.exists ? 1 : 0, static_cast<jlong>(st.state), static_cast<jlong>(st.lost), st.handedOff ? 1 : 0,
                      st.needsCapability ? 1 : 0, st.tailActive ? 1 : 0, static_cast<jlong>(st.strays)};
  jlongArray out = env->NewLongArray(7);
  if (out) env->SetLongArrayRegion(out, 0, 7, v);""",
     """  const jlong v[8] = {st.exists ? 1 : 0, static_cast<jlong>(st.state), static_cast<jlong>(st.lost), st.handedOff ? 1 : 0,
                      st.needsCapability ? 1 : 0, st.tailActive ? 1 : 0, static_cast<jlong>(st.strays),
                      static_cast<jlong>(st.punches)};
  jlongArray out = env->NewLongArray(8);
  if (out) env->SetLongArrayRegion(out, 0, 8, v);"""),
    ("""extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyReleaseAll(JNIEnv* /* env */, jobject /* this */) {
  if (auto* r = standby_runner()) r->ReleaseAll();
}""", """extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyReleaseAll(JNIEnv* /* env */, jobject /* this */) {
  if (auto* r = standby_runner()) r->ReleaseAll();
}

// r3 (Codex codex_ka_e_src_r2.md R2): one app screen's leases (its key prefix) and no other -- the runner is the
// process's, the screens come and go. How many went.
extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyReleaseOwner(JNIEnv* env, jobject /* this */,
                                                                             jstring prefix) {
  auto* r = standby_runner();
  return r ? static_cast<jint>(r->ReleaseWhere(jstring_to_string(env, prefix))) : 0;
}"""),
])

# ---------------------------------------------------------------- MainActivity.kt
M = K + 'MainActivity.kt'
edit(M, [
    # R2: the owner
    ("""        private const val ESTABLISH_LIMIT_MS = 20_000L""",
     """        private const val ESTABLISH_LIMIT_MS = 20_000L
        // r3 R2: each Activity is one owner of standby leases in this process's native runner (never reused)
        private val STANDBY_OWNERS = java.util.concurrent.atomic.AtomicLong()"""),
    ("""    private val standbyBook = StandbyKeep.Book()""",
     """    private val standbyBook = StandbyKeep.Book()
    // r3 (Codex codex_ka_e_src_r2.md R2): this screen's own key prefix in the process's runner -- every native standby
    // call goes through standbyKey(), and this screen's cleanup releases this prefix only (a late one never takes the
    // leases of the screen after it)
    private val standbyOwner = StandbyKeep.ownerPrefix(STANDBY_OWNERS.incrementAndGet())
    private fun standbyKey(key: RecentHosts.Key): String = StandbyKeep.ownerKey(standbyOwner, key)"""),
    ("""        diagnosticsLog.log("standby_release_all", "reason=$reason leases=${keys.size}")
        standbyExecutor.execute { NativeSessionBridge.nativeStandbyReleaseAll() }""",
     """        diagnosticsLog.log("standby_release_all", "reason=$reason leases=${keys.size} owner=$standbyOwner")
        val owner = standbyOwner
        standbyExecutor.execute {
            val gone = NativeSessionBridge.nativeStandbyReleaseOwner(owner)
            diagnosticsLog.log("standby_release_owner", "owner=$owner released=$gone")
        }"""),
    # D1 logs
    ("""                diagnosticsLog.log("standby_back", "host=${key.hostId} strays=${lease.strays} tail=${lease.tailActive}")""",
     """                diagnosticsLog.log("standby_back",
                    "host=${key.hostId} strays=${lease.strays} punches=${lease.punches} tail=${lease.tailActive}")"""),
    ("""                diagnosticsLog.log("standby_tail_quiet", "host=${key.hostId} strays=${lease.strays}")""",
     """                diagnosticsLog.log("standby_tail_quiet", "host=${key.hostId} strays=${lease.strays} punches=${lease.punches}")"""),
    # R1: a pending list return goes with its entry / with everything
    ("""    private fun forgetKept(handle: Long) {
        kept.remove(handle)""", """    private fun forgetKept(handle: Long) {
        // r3 R1 (Codex codex_ka_e_src_r2.md): a "PC 목록" still fencing this entry goes with it -- its end never runs
        // later (it would start a lease for a PC the user let go)
        listReturnEnds.remove(handle)?.let { diagnosticsLog.log("list_return_dropped", "handle=$handle host=${it.key?.hostId.orEmpty()}") }
        kept.remove(handle)"""),
    ("""    private fun endAllKept(reason: String) {
        heldTap = null""", """    private fun endAllKept(reason: String) {
        heldTap = null
        // r3 R1: every "PC 목록" still fencing goes too (the native end-all below ends their sessions) -- none of them
        // starts a lease afterwards, under this owner or the next sign-in
        if (listReturnEnds.isNotEmpty()) {
            diagnosticsLog.log("list_return_dropped", "all=${listReturnEnds.size} reason=$reason")
            listReturnEnds.clear()
        }"""),
    ("""                val h = keptEntry?.handle ?: return@setOnClickListener
                diagnosticsLog.log("park_disconnect_tap", "handle=$h host=${host.hostName}")""",
     """                val h = keptEntry?.handle ?: return@setOnClickListener
                diagnosticsLog.log("park_disconnect_tap", "handle=$h host=${host.hostName}")
                // r3 R1: its 끊기 lets this PC go -- not kept automatically again (as a lease's 끊기)
                keepSuppression.suppress(rk)"""),
    ("""                diagnosticsLog.log("standby_first_transition", "host=${rk?.hostId.orEmpty()} new_directory_requests=1")
                rk?.let { startStandby(it, "first_transition") }""",
     """                diagnosticsLog.log("standby_first_transition", "host=${rk?.hostId.orEmpty()} new_directory_requests=1")
                rk?.let { startStandby(it, "first_transition") }  // r3 R1: startStandby checks owner / sign-in / 끊기"""),
    ("""    private fun startStandby(key: RecentHosts.Key, reason: String) {
        if (standbyBook.has(key) || !ensureStandbyInit()) return""",
     """    private fun startStandby(key: RecentHosts.Key, reason: String) {
        if (standbyBook.has(key)) return
        // r3 R1 (Codex codex_ka_e_src_r2.md): a lease only for this live screen, the sign-in it belongs to, and a PC the
        // user has not let go (끊기) -- whatever asked for it (a list return's end, the app's own preparation)
        val why = when {
            isDestroyed || isFinishing -> "owner_ended"
            DirectoryClient.session().isEmpty() || key.accountId != DirectoryClient.sessionAccount() ||
                key.directoryOrigin != directoryOrigin() -> "not_this_sign_in"
            keepSuppression.isSuppressed(key) -> "let_go"
            else -> null
        }
        if (why != null) {
            diagnosticsLog.log("standby_start_skipped", "host=${key.hostId} reason=$reason why=$why")
            return
        }
        if (!ensureStandbyInit()) return"""),
    # R3: the prepared return carried to its fallback
    ("""    private data class PendingCommit(
        val handle: Long, val op: Long, val gen: Long, val epoch: Long, val startedAtMs: Long,
        val pathKind: String, val manual: Boolean,
    )""", """    // r3 R3 (Codex codex_ka_e_src_r2.md): preparedReturn = the user went back to a PC on standby -- its activation, or
    // that same tap's own ordinary fallback (whatever path it took): the clipboard starts from a baseline, as back from
    // standby. A separate cold connect the user asked for starts fresh, as before.
    private data class PendingCommit(
        val handle: Long, val op: Long, val gen: Long, val epoch: Long, val startedAtMs: Long,
        val pathKind: String, val manual: Boolean, val preparedReturn: Boolean = false,
    )"""),
    ("""                    pendingCommit = PendingCommit(handle, op, gen, epoch, t0, "standby", manual = false)""",
     """                    pendingCommit = PendingCommit(handle, op, gen, epoch, t0, "standby", manual = false, preparedReturn = true)"""),
    ("""                connectToDirectoryHost(host, TapIntent.RETRY)""",
     """                connectToDirectoryHost(host, TapIntent.RETRY, preparedReturn = true)  // r3 R3: still the return"""),
    ("""    private fun connectToDirectoryHost(host: DirectoryClient.Host, intent: TapIntent = TapIntent.USER) {""",
     """    private fun connectToDirectoryHost(
        host: DirectoryClient.Host, intent: TapIntent = TapIntent.USER, preparedReturn: Boolean = false,
    ) {
        var returning = preparedReturn"""),
    ("""            is StandbyKeep.Tap.Cold -> if (st.releaseFirst) releaseStandby(rk, "cold_tap")""",
     """            is StandbyKeep.Tap.Cold -> if (st.releaseFirst) {
                releaseStandby(rk, "cold_tap")
                // r3 R3: this PC was on standby (its lease not ready yet) -- the user goes back to it: a return
                returning = true
            }"""),
    ("""                    pendingCommit = PendingCommit(handle, op, madeGeneration, epoch, SystemClock.elapsedRealtime(),
                        pathKind, manual = false)""",
     """                    pendingCommit = PendingCommit(handle, op, madeGeneration, epoch, SystemClock.elapsedRealtime(),
                        pathKind, manual = false, preparedReturn = returning)"""),
    ("""        // r2 N3 (clipboard r3): back from standby, what the phone holds is this new binding's baseline -- not sent; if it
        // cannot be read, nothing is pushed until a real change. A cold connect starts as before.
        when (val c = StandbyKeep.clipboardOnStart(p.pathKind == "standby", if (p.pathKind == "standby") readLocalClipboardText() else null)) {""",
     """        // r2 N3 (clipboard r3): back from standby, what the phone holds is this new binding's baseline -- not sent; if it
        // cannot be read, nothing is pushed until a real change. A cold connect starts as before. r3 R3: "back from
        // standby" is the user's return (preparedReturn), not the path it took -- its ordinary fallback is one too.
        val returning = p.preparedReturn || p.pathKind == "standby"
        when (val c = StandbyKeep.clipboardOnStart(returning, if (returning) readLocalClipboardText() else null)) {"""),
    ("""                diagnosticsLog.log("clipboard_baseline", "start=seed units=${c.text.length} handle=${p.handle}")""",
     """                diagnosticsLog.log("clipboard_baseline", "start=seed units=${c.text.length} handle=${p.handle} path=${p.pathKind}")"""),
    ("""                diagnosticsLog.log("clipboard_baseline", "start=hold handle=${p.handle}")""",
     """                diagnosticsLog.log("clipboard_baseline", "start=hold handle=${p.handle} path=${p.pathKind}")"""),
    ("StandbyKeep.nativeKey(", "standbyKey("),
], count_all={"StandbyKeep.nativeKey(": 5})

# ---------------------------------------------------------------- StandbyKeepTest.kt
edit(A + 'test/java/com/remote60/androiddirect/StandbyKeepTest.kt', [
    ("""        // an older bridge's 5-element status: no tail
        assertFalse(StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0)).tailActive)
    }""", """        // an older bridge's 5-element status: no tail
        assertFalse(StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0)).tailActive)
    }

    @Test
    fun punchesAreReadApartFromStrays() {
        // r3 D1: the 8th element is the host's Punches; a 7-element status reads none
        val l = StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0, 0, 3, 25))
        assertEquals(3L, l.strays)
        assertEquals(25L, l.punches)
        assertFalse(l.tailActive)
        assertEquals(0L, StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0, 1, 40)).punches)
    }

    @Test
    fun eachOwnerHasItsOwnKeyPrefix() {
        // r3 R2: owner 1's prefix never matches owner 10's keys
        val a = StandbyKeep.ownerKey(StandbyKeep.ownerPrefix(1), k("A"))
        val b = StandbyKeep.ownerKey(StandbyKeep.ownerPrefix(10), k("A"))
        assertTrue(a.startsWith(StandbyKeep.ownerPrefix(1)))
        assertFalse(b.startsWith(StandbyKeep.ownerPrefix(1)))
        assertTrue(a != b)
        assertEquals(StandbyKeep.nativeKey(k("A")), a.removePrefix(StandbyKeep.ownerPrefix(1)))
    }"""),
])
