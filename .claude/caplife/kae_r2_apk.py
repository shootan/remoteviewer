base = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/'


def patch(rel, reps):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (rel, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


# ======================================================================= StandbyKeep.kt
patch('main/java/com/remote60/androiddirect/StandbyKeep.kt', [
("""    /** nativeStandbyStatus: [exists, state, lost, handedOff, needsCapability]. */
    data class Lease(
        val exists: Boolean,
        val state: LeaseState,
        val lost: Long,
        val handedOff: Boolean,
        val needsCapability: Boolean,
    ) {
        companion object {
            val NONE = Lease(false, LeaseState.IDLE, 0L, false, false)
            fun of(v: LongArray?): Lease {
                if (v == null || v.size < 5 || v[0] == 0L) return NONE
                return Lease(true, LeaseState.of(v[1]), v[2], v[3] != 0L, v[4] != 0L)
            }
        }
    }

    enum class Shown { PREPARING, READY, ACTIVATING, ACTIVE, RETURNING, UNSUPPORTED, RETRY }""",
 """    /**
     * nativeStandbyStatus: [exists, state, lost, handedOff, needsCapability, tailActive, strays]. r2 D1 (Codex
     * codex_ka_e_src_r1.md): [tailActive] = the old session's datagrams still reach the lease (what the host still sends
     * it, observed), [strays] = how many so far (the tail's cost, counted apart).
     */
    data class Lease(
        val exists: Boolean,
        val state: LeaseState,
        val lost: Long,
        val handedOff: Boolean,
        val needsCapability: Boolean,
        val tailActive: Boolean = false,
        val strays: Long = 0L,
    ) {
        companion object {
            val NONE = Lease(false, LeaseState.IDLE, 0L, false, false)
            fun of(v: LongArray?): Lease {
                if (v == null || v.size < 5 || v[0] == 0L) return NONE
                return Lease(true, LeaseState.of(v[1]), v[2], v[3] != 0L, v[4] != 0L,
                    tailActive = v.size > 5 && v[5] != 0L, strays = if (v.size > 6) v[6] else 0L)
            }
        }
    }

    /**
     * r2 D1: what the list may say -- three facts kept apart: (1) the local hand-back of the socket (RETURNING while it
     * is not done), (2) the entry alive on its lease (READY / SETTLING), (3) the host's old grant and picture cleaned up
     * -- which the existing wire does not confirm: READY never claims it, and SETTLING says the old session's datagrams
     * are still arriving (observed). A tap on either is the user's explicit activation (a new generation: rollover).
     */
    enum class Shown { PREPARING, READY, SETTLING, ACTIVATING, ACTIVE, RETURNING, UNSUPPORTED, RETRY }"""),
("""        lease.handedOff -> Shown.RETURNING  // its session is ending (or never came up): not ready until the lease reads again
        lease.state == LeaseState.PARKED -> Shown.READY""",
 """        lease.handedOff -> Shown.RETURNING  // its session is ending (or never came up): not ready until the lease reads again
        lease.state == LeaseState.PARKED && lease.tailActive -> Shown.SETTLING
        lease.state == LeaseState.PARKED -> Shown.READY"""),
("""    /** The leases that go: the ones whose PC is not among the recent ones any more -- never the one on screen. */""",
 """    /**
     * r2 N4: active + standby together are at most [max]. [leasesByRecency] = the leases, most recent first (leases of PCs
     * no longer recent last); [otherActive] = sessions on screen / committed that are NOT a lease's (an ordinary or a raw-IP
     * connection: 0 or 1). The excess goes from the least recent lease -- never the one on screen. Called once a connection
     * is committed (a success): a fourth that fails evicts nothing.
     */
    fun overLimit(leasesByRecency: List<RecentHosts.Key>, otherActive: Int, onScreen: RecentHosts.Key?,
                  max: Int = RecentHosts.MAX): List<RecentHosts.Key> {
        val allowed = (max - otherActive).coerceAtLeast(0)
        if (leasesByRecency.size <= allowed) return emptyList()
        val out = ArrayList<RecentHosts.Key>()
        var excess = leasesByRecency.size - allowed
        for (k in leasesByRecency.asReversed()) {
            if (excess == 0) break
            if (k == onScreen) continue
            out.add(k)
            --excess
        }
        return out
    }

    /** r2 N4: whether one more lease may start now (the same count). */
    fun mayStart(leases: Int, otherActive: Int, max: Int = RecentHosts.MAX): Boolean = leases + otherActive < max

    /**
     * r2 N3 (clipboard r3 contract, kept): what a NEW session of a PC starts with. Back from standby (an activation):
     * what the phone holds now is not news to that PC -- it becomes the new binding's baseline and is NOT sent; if it
     * cannot be read, nothing is pushed until the clipboard really changes. An ordinary (cold) connect: as before.
     */
    sealed class ClipboardStart {
        data class Seed(val text: String) : ClipboardStart()
        object Hold : ClipboardStart()
        object Fresh : ClipboardStart()
    }

    fun clipboardOnStart(fromStandby: Boolean, phoneText: String?): ClipboardStart = when {
        !fromStandby -> ClipboardStart.Fresh
        phoneText.isNullOrEmpty() -> ClipboardStart.Hold
        else -> ClipboardStart.Seed(phoneText)
    }

    /** The leases that go: the ones whose PC is not among the recent ones any more -- never the one on screen."""),
("""        /** The job of [key]@[inc] ended: ok resets its back-off; a failure waits longer (1, 2, 4, 8, 16, 30 s + jitter). */
        @Synchronized fun endStaging(key: RecentHosts.Key, inc: Long, ok: Boolean, nowMs: Long, random: Double) {
            if (staging == key to inc) staging = null
            if (ok) {""",
 """        /**
         * The job of [key]@[inc] ended: ok resets its back-off; a failure waits longer (1, 2, 4, 8, 16, 30 s + jitter).
         * r2 N5: a late end of a job whose lease is gone / replaced changes nothing of the current one.
         */
        @Synchronized fun endStaging(key: RecentHosts.Key, inc: Long, ok: Boolean, nowMs: Long, random: Double) {
            if (staging == key to inc) staging = null
            if (!isCurrent(key, inc)) return
            if (ok) {"""),
])

# ======================================================================= native_bridge.cpp: the status array
patch('main/cpp/native_bridge.cpp', [
("""// [exists, state (StandbyEntryClient::State: 0 Idle, 1 Admitting, 2 Parked, 3 Activating, 4 Active, 5 Unsupported,
// 6 Waiting), lost count, handed off, needs a capability]""",
 """// [exists, state (StandbyEntryClient::State: 0 Idle, 1 Admitting, 2 Parked, 3 Activating, 4 Active, 5 Unsupported,
// 6 Waiting), lost count, handed off, needs a capability, tail active, strays] -- r2 D1: the last two are the old
// session's datagrams the lease still receives (observed) and how many so far"""),
("""  const jlong v[5] = {st.exists ? 1 : 0, static_cast<jlong>(st.state), static_cast<jlong>(st.lost), st.handedOff ? 1 : 0,
                      st.needsCapability ? 1 : 0};
  jlongArray out = env->NewLongArray(5);
  if (out) env->SetLongArrayRegion(out, 0, 5, v);""",
 """  const jlong v[7] = {st.exists ? 1 : 0, static_cast<jlong>(st.state), static_cast<jlong>(st.lost), st.handedOff ? 1 : 0,
                      st.needsCapability ? 1 : 0, st.tailActive ? 1 : 0, static_cast<jlong>(st.strays)};
  jlongArray out = env->NewLongArray(7);
  if (out) env->SetLongArrayRegion(out, 0, 7, v);"""),
])

# ======================================================================= MainActivity.kt
patch('main/java/com/remote60/androiddirect/MainActivity.kt', [
# ---- state
("""    private val standbyExecutor = java.util.concurrent.Executors.newSingleThreadExecutor()
    private var standbyInitDone = false
""", """    private val standbyExecutor = java.util.concurrent.Executors.newSingleThreadExecutor()
    private var standbyInitDone = false
    // r2 N1 (Codex codex_ka_e_src_r1.md): every user tap is the newest intent -- its number; a held tap carries the
    // number it was made under and runs only if no tap came since (a held / retried tap never re-approves itself).
    private var tapSeq = 0L
    private var heldTapSeq = 0L
    // r2 held input: "PC 목록" fences the session first (its held input released, its pause proven by a Ping) and ends it
    // when the fence settles or its budget runs out -- then the standby part (return / first transition / not supported).
    private data class ListReturnEnd(
        val handle: Long, val decision: StandbyKeep.ListReturn, val key: RecentHosts.Key?, val hostName: String,
        val deadlineMs: Long,
    )
    private val listReturnEnds = HashMap<Long, ListReturnEnd>()
    private val tailLogged = HashSet<RecentHosts.Key>()
"""),
# ---- N1: the tap
("""    private fun connectToDirectoryHost(host: DirectoryClient.Host) {
        val account = DirectoryClient.sessionAccount()
        val origin = directoryOrigin()
        val key = SessionPark.entryKey(origin, account, host.hostId)
        // keepalive r2: the user's explicit connect -- this PC may be kept again, and the user comes first.
        keepSuppression.release(RecentHosts.Key(origin, account, host.hostId))
        preconnectPlanner.onManualTap()""",
 """    /** r2 N1: who asks -- the user's own tap, a held tap carried out, or the one ordinary retry of a failed activation. */
    private enum class TapIntent { USER, HELD, RETRY }

    private fun connectToDirectoryHost(host: DirectoryClient.Host, intent: TapIntent = TapIntent.USER) {
        val account = DirectoryClient.sessionAccount()
        val origin = directoryOrigin()
        val key = SessionPark.entryKey(origin, account, host.hostId)
        if (intent == TapIntent.USER) {
            // the newest intent: any tap still held goes (r2 N1), the user's explicit connect lifts this PC's suppression
            ++tapSeq
            if (heldTap != null) diagnosticsLog.log("tap_held_superseded", "held=${heldTap?.hostId} by=${host.hostId}")
            heldTap = null
            // keepalive r2: the user's explicit connect -- this PC may be kept again, and the user comes first.
            keepSuppression.release(RecentHosts.Key(origin, account, host.hostId))
            preconnectPlanner.onManualTap()
        } else if (keepSuppression.isSuppressed(RecentHosts.Key(origin, account, host.hostId))) {
            // a held / retried tap never undoes the user's 끊기 made since (r2 N1)
            diagnosticsLog.log("tap_continuation_dropped", "host=${host.hostId} intent=$intent why=suppressed")
            return
        }"""),
("""            StandbyKeep.Tap.Wait -> {
                val previous = heldTap
                heldTap = connectIntents.scheduleFallback(account, origin, host.hostId)
                diagnosticsLog.log("standby_tap_held", "tapped=${host.hostId} previous=${previous?.hostId.orEmpty()}")
                return
            }""", """            StandbyKeep.Tap.Wait -> {
                val previous = heldTap
                heldTap = connectIntents.scheduleFallback(account, origin, host.hostId)
                heldTapSeq = tapSeq
                diagnosticsLog.log("standby_tap_held", "tapped=${host.hostId} previous=${previous?.hostId.orEmpty()} seq=$tapSeq")
                return
            }"""),
("""                val previous = heldTap
                heldTap = connectIntents.scheduleFallback(account, origin, host.hostId)
                diagnosticsLog.log(if (previous == null) "park_tap_held" else "park_tap_replaced",""",
 """                val previous = heldTap
                heldTap = connectIntents.scheduleFallback(account, origin, host.hostId)
                heldTapSeq = tapSeq
                diagnosticsLog.log(if (previous == null) "park_tap_held" else "park_tap_replaced","""),
("""        if (held != null && !anyParkConfirming() && !heldLeaseReturning) {
            heldTap = null
            val host = directoryHosts.firstOrNull { it.hostId == held.hostId }
            if (!connectIntents.fallbackRuns(held, DirectoryClient.sessionAccount(), directoryOrigin(), host != null)) {""",
 """        if (held != null && !anyParkConfirming() && !heldLeaseReturning) {
            heldTap = null
            val host = directoryHosts.firstOrNull { it.hostId == held.hostId }
            if (heldTapSeq != tapSeq) {
                // a newer tap came since (it clears the held one; this is the belt to that brace)
                diagnosticsLog.log("park_tap_cancelled", "host=${held.hostId} why=newer_tap seq=$heldTapSeq->$tapSeq")
            } else if (!connectIntents.fallbackRuns(held, DirectoryClient.sessionAccount(), directoryOrigin(), host != null)) {"""),
("""                diagnosticsLog.log("park_tap_resumed", "host=${held.hostId}")
                connectToDirectoryHost(host!!)""",
 """                diagnosticsLog.log("park_tap_resumed", "host=${held.hostId}")
                connectToDirectoryHost(host!!, TapIntent.HELD)"""),
("""                releaseStandby(rk, "activate_failed")
                connectToDirectoryHost(host)""", """                releaseStandby(rk, "activate_failed")
                connectToDirectoryHost(host, TapIntent.RETRY)"""),
# ---- N5: callbacks after destroy
("""            runOnUiThread {
                val mine = entryOps.isCurrent(handle, op) && NativeSessionBridge.nativeEpoch() == epoch""",
 """            runOnUiThread {
                if (isDestroyed) return@runOnUiThread  // r2 N5: an ended Activity's late answer changes nothing
                val mine = entryOps.isCurrent(handle, op) && NativeSessionBridge.nativeEpoch() == epoch"""),
("""                runOnUiThread { standbyBook.endStaging(key, inc, done, SystemClock.elapsedRealtime(), Math.random()) }""",
 """                runOnUiThread {
                    if (!isDestroyed) standbyBook.endStaging(key, inc, done, SystemClock.elapsedRealtime(), Math.random())
                }"""),
("""        saveCurrentEndpoint()
        directoryExecutor.shutdownNow()""", """        saveCurrentEndpoint()
        directoryExecutor.shutdownNow()
        // r2 N5: this Activity's leases go first (owners invalidated, ReleaseAll queued), then its standby thread ends
        // once what is queued has run -- a graceful shutdown, never shutdownNow (that would drop the ReleaseAll). Nothing
        // here waits on the UI thread; a capability job still in flight finds its lease gone and provides nothing.
        releaseAllStandby("destroy")
        standbyExecutor.shutdown()"""),
# ---- N4: the limit at commit, at start
("""    private fun startStandby(key: RecentHosts.Key, reason: String) {
        if (standbyBook.has(key) || !ensureStandbyInit()) return""",
 """    /** r2 N4: sessions on screen that are not a lease's (an ordinary / raw-IP connection): they count toward the three. */
    private fun otherActiveSessions(): Int = if (activeHandle != 0L && standbyHandles[activeHandle] == null) 1 else 0

    private fun startStandby(key: RecentHosts.Key, reason: String) {
        if (standbyBook.has(key) || !ensureStandbyInit()) return
        if (!StandbyKeep.mayStart(standbyBook.keys().size, otherActiveSessions())) {
            diagnosticsLog.log("standby_start_skipped",
                "host=${key.hostId} reason=$reason why=limit leases=${standbyBook.keys().size} other=${otherActiveSessions()}")
            return
        }"""),
("""        for (gone in StandbyKeep.evictions(recent, keys.filter { standbyBook.has(it) }, standbyHandles[activeHandle])) {
            releaseStandby(gone, "lru")
        }""", """        for (gone in StandbyKeep.evictions(recent, keys.filter { standbyBook.has(it) }, standbyHandles[activeHandle])) {
            releaseStandby(gone, "lru")
        }
        // r2 N4: active + standby together at most three -- a committed ordinary / raw-IP session counts too
        val byRecency = standbyBook.keys().sortedBy { k -> recent.indexOf(k).let { if (it < 0) Int.MAX_VALUE else it } }
        for (gone in StandbyKeep.overLimit(byRecency, otherActiveSessions(), standbyHandles[activeHandle])) {
            releaseStandby(gone, "limit")
        }"""),
# ---- D1: the tail logged once per episode (its cost apart)
("""            next[key] = lease
            if (lease.exists && lease.needsCapability && !lease.handedOff) requestStandbyCapability(key, now)""",
 """            next[key] = lease
            if (before?.tailActive == true && !lease.tailActive) {
                diagnosticsLog.log("standby_tail_quiet", "host=${key.hostId} strays=${lease.strays}")
            }
            if (lease.tailActive != (before?.tailActive == true)) cardsChanged = true
            if (lease.exists && lease.needsCapability && !lease.handedOff) requestStandbyCapability(key, now)"""),
# ---- N3: the clipboard at a commit
("""        connectionPathKind = p.pathKind
        connectFlowActive = true
        pendingRuntimeConfigSync = true
        resetSessionDesktopBackend("commit")  // ua2-apk: this connection's own choice, none yet (nothing re-sent)
        desiredStreamActive = false
        lastAppliedStreamActive = null
        clipboardResumeHold = false""",
 """        connectionPathKind = p.pathKind
        connectFlowActive = true
        pendingRuntimeConfigSync = true
        resetSessionDesktopBackend("commit")  // ua2-apk: this connection's own choice, none yet (nothing re-sent)
        desiredStreamActive = false
        lastAppliedStreamActive = null
        // r2 N3 (clipboard r3): back from standby, what the phone holds is this new binding's baseline -- not sent; if it
        // cannot be read, nothing is pushed until a real change. A cold connect starts as before.
        when (val c = StandbyKeep.clipboardOnStart(p.pathKind == "standby", if (p.pathKind == "standby") readLocalClipboardText() else null)) {
            is StandbyKeep.ClipboardStart.Seed -> {
                clipboardResumeHold = false
                NativeSessionBridge.nativeSeedClipboardBaseline(activeBinding, c.text)
                diagnosticsLog.log("clipboard_baseline", "start=seed units=${c.text.length} handle=${p.handle}")
            }
            StandbyKeep.ClipboardStart.Hold -> {
                clipboardResumeHold = true
                diagnosticsLog.log("clipboard_baseline", "start=hold handle=${p.handle}")
            }
            StandbyKeep.ClipboardStart.Fresh -> clipboardResumeHold = false
        }
        // r2 N4: a committed ordinary / raw-IP session counts toward the three with the leases (the least recent goes)
        standbyTick()"""),
# ---- N1: 끊기 / disconnect drop a held tap of that PC
("""        if (handle != 0L) {
            kept[handle]?.let { k ->
                recentKeyOf(k)?.let {
                    keepSuppression.suppress(it)  // keepalive r2""",
 """        if (handle != 0L) {
            kept[handle]?.let { k ->
                if (heldTap?.hostId == k.hostId) heldTap = null  // r2 N1: a held tap of the PC let go never runs
                recentKeyOf(k)?.let {
                    keepSuppression.suppress(it)  // keepalive r2"""),
("""                if (standbyBook.has(rk)) {
                    // this PC's standby goes, and it is not kept automatically again (until the user connects it)
                    diagnosticsLog.log("standby_disconnect_tap", "host=${host.hostName}")""",
 """                if (heldTap?.hostId == host.hostId) {
                    heldTap = null  // r2 N1: a held tap of this PC never runs after its 끊기
                    diagnosticsLog.log("tap_held_dropped", "host=${host.hostId} why=disconnect")
                }
                if (standbyBook.has(rk)) {
                    // this PC's standby goes, and it is not kept automatically again (until the user connects it)
                    diagnosticsLog.log("standby_disconnect_tap", "host=${host.hostName}")"""),
# ---- D1 + held input: "PC 목록" fences first, then ends
("""        entryOps.invalidate(handle)
        NativeSessionBridge.nativeEnd(handle,
            if (decision == StandbyKeep.ListReturn.UNSUPPORTED_END) "keep_unsupported" else "to_standby")
        forgetKept(handle)
        clearActive()
        connectFlowActive = false
        when (decision) {""", """        entryOps.invalidate(handle)
        clearActive()
        connectFlowActive = false
        val end = ListReturnEnd(handle, decision, rk, k?.hostName.orEmpty(),
            SystemClock.elapsedRealtime() + SessionPark.CONFIRM_BUDGET_MS)
        // r2: what the session holds is released NOW, not left to the host's end of it (Codex: held input and the
        // clipboard are never put off to the tail) -- the native fence: no new input / clipboard, the releases of what is
        // held sent, the pause proven by a Ping. It ends the session when it settles (keepTick) or at its budget. A
        // session that cannot fence (an older host, a raw-IP one) ends at once.
        if (NativeSessionBridge.nativeBeginPark(handle)) {
            listReturnEnds[handle] = end
            diagnosticsLog.log("list_return_fence", "handle=$handle begin")
        } else {
            diagnosticsLog.log("list_return_fence", "handle=$handle none (cannot fence: ends now)")
            finishListReturn(end, "no_fence")
        }
        hostsStatusText.text = ""
        currentScene = UiScene.HOSTS
        hostListAdapter.notifyDataSetChanged()
        renderStatus()
    }

    /** The fenced session ends; then the standby part of "PC 목록". */
    private fun finishListReturn(end: ListReturnEnd, how: String) {
        listReturnEnds.remove(end.handle)
        val decision = end.decision
        val rk = end.key
        diagnosticsLog.log("list_return_end",
            "handle=${end.handle} how=$how released=${NativeSessionBridge.nativeParkReleasedCount(end.handle)} " +
                "dropped=${NativeSessionBridge.nativeParkDroppedActionCount(end.handle)}")
        NativeSessionBridge.nativeEnd(end.handle,
            if (decision == StandbyKeep.ListReturn.UNSUPPORTED_END) "keep_unsupported" else "to_standby")
        forgetKept(end.handle)
        when (decision) {"""),
("""            StandbyKeep.ListReturn.UNSUPPORTED_END -> {
                diagnosticsLog.log("standby_unsupported_end", "host=${k?.hostName.orEmpty()}")
                Toast.makeText(this, getString(R.string.hosts_standby_unsupported_end, k?.hostName.orEmpty()),
                    Toast.LENGTH_LONG).show()
            }
        }
        hostsStatusText.text = ""
        currentScene = UiScene.HOSTS
        hostListAdapter.notifyDataSetChanged()
        renderStatus()
    }""", """            StandbyKeep.ListReturn.UNSUPPORTED_END -> {
                diagnosticsLog.log("standby_unsupported_end", "host=${end.hostName}")
                Toast.makeText(this, getString(R.string.hosts_standby_unsupported_end, end.hostName), Toast.LENGTH_LONG).show()
            }
        }
        hostListAdapter.notifyDataSetChanged()
    }

    /** Every status tick: a fenced "PC 목록" ends when its fence settled (or failed), or at its budget. */
    private fun listReturnTick() {
        if (listReturnEnds.isEmpty()) return
        val now = SystemClock.elapsedRealtime()
        for (end in listReturnEnds.values.toList()) {
            val phase = SessionPark.NativePhase.of(NativeSessionBridge.nativeParkPhase(end.handle))
            when {
                phase == SessionPark.NativePhase.PARKED -> finishListReturn(end, "fenced")
                phase == SessionPark.NativePhase.FAILED || phase == SessionPark.NativePhase.EXPIRED ->
                    finishListReturn(end, "fence_" + phase.name.lowercase())
                now >= end.deadlineMs -> finishListReturn(end, "fence_budget")
            }
        }
    }"""),
("""    private fun keepTick() {
        tryCommit()
        standbyTick()""", """    private fun keepTick() {
        tryCommit()
        listReturnTick()
        standbyTick()"""),
# ---- D1 card text
("""                StandbyKeep.Shown.READY -> getString(R.string.hosts_standby_ready_detail)""",
 """                StandbyKeep.Shown.READY -> getString(R.string.hosts_standby_ready_detail)
                StandbyKeep.Shown.SETTLING -> getString(R.string.hosts_standby_settling)"""),
("""                shown == StandbyKeep.Shown.READY -> getString(R.string.hosts_standby_state)""",
 """                shown == StandbyKeep.Shown.READY || shown == StandbyKeep.Shown.SETTLING -> getString(R.string.hosts_standby_state)"""),
])

patch('main/res/values/strings.xml', [
("""    <string name="hosts_standby_ready_detail">대기 연결 준비됨 · 누르면 바로 열립니다</string>""",
 """    <!-- r2 D1: "대기 중" says only that this phone's standby entry is alive on its lease -- not that the PC already cleaned
         up the previous connection (that is not confirmed by the PC; while its old picture still arrives: settling) -->
    <string name="hosts_standby_ready_detail">대기 연결 있음 · 누르면 엽니다</string>
    <string name="hosts_standby_settling">이전 연결 정리 중일 수 있음 · 누르면 다시 엽니다</string>"""),
])
print('ok')
