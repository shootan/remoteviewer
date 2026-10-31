p = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')


def rep(a, b, count=1):
    global s
    assert s.count(a) == count, (a[:90], s.count(a))
    s = s.replace(a, b)


def cut(start, end_marker):
    """remove from `start` up to (not including) `end_marker`"""
    global s
    i = s.index(start)
    j = s.index(end_marker, i)
    s = s[:i] + s[j:]


# ---------------------------------------------------------------- fields
rep("""    // keepalive r2: the three PCs used most recently (per account, persisted), the PCs the user disconnected (not
    // prepared again this sign-in), and the planner that would prepare them. Preconnect.SEND_TO_REAL_HOSTS is off:
    // nothing is sent to a host automatically until the host side is confirmed (spike A).
    private val recentHosts: RecentHosts.Book by lazy { RecentHosts.load(this) }
    private val keepSuppression = RecentHosts.Suppression()
    private val preconnectPlanner = Preconnect.Planner { Math.random() }
    private var preconnectOffLogged = false
""", """    // keepalive r2: the three PCs used most recently (per account, persisted), the PCs the user disconnected (not
    // prepared again this sign-in), and the planner that would prepare them. Preconnect.AUTO_PREPARE (off in every
    // shipped build) gates only the app's OWN preparation of a recent PC; a user's "PC 목록" goes to standby anyway.
    private val recentHosts: RecentHosts.Book by lazy { RecentHosts.load(this) }
    private val keepSuppression = RecentHosts.Suppression()
    private val preconnectPlanner = Preconnect.Planner { Math.random() }
    private var preconnectOffLogged = false
    // keepalive-r2 ② e, kept model A (StandbyKeep): the standby leases of this sign-in (native StandbyRunner), their
    // last status, which session entry a lease activated, and the directory's standby support. The native calls that
    // may wait (start, observe, the standby connect, release) run on their own thread in order -- never on the UI
    // thread, never queued behind a tap's connect on directoryExecutor.
    private val standbyBook = StandbyKeep.Book()
    private var standbyLeases: Map<RecentHosts.Key, StandbyKeep.Lease> = emptyMap()
    private val standbyHandles = HashMap<Long, RecentHosts.Key>()
    private var standbySupport = DirectoryClient.StandbySupport.NONE
    private val standbyExecutor = java.util.concurrent.Executors.newSingleThreadExecutor()
    private var standbyInitDone = false
""")

# ---------------------------------------------------------------- disconnect: the lease goes too
rep("""        if (handle != 0L) {
            kept[handle]?.let { k -> recentKeyOf(k)?.let { keepSuppression.suppress(it) } }  // keepalive r2
""", """        if (handle != 0L) {
            kept[handle]?.let { k ->
                recentKeyOf(k)?.let {
                    keepSuppression.suppress(it)  // keepalive r2
                    releaseStandby(it, "user_disconnect")  // model A: its lease too -- not kept automatically again
                }
            }
""")

# ---------------------------------------------------------------- forget: the lease link, the network watch
rep("""    private fun forgetKept(handle: Long) {
        kept.remove(handle)
        entryOps.drop(handle)
        if (connectingHandle == handle) connectingHandle = 0L
        if (kept.values.none { it.parkedAtMs != 0L }) unwatchParkNetwork()
    }""", """    private fun forgetKept(handle: Long) {
        kept.remove(handle)
        entryOps.drop(handle)
        standbyHandles.remove(handle)
        if (connectingHandle == handle) connectingHandle = 0L
        if (kept.values.none { it.parkedAtMs != 0L } && standbyBook.keys().isEmpty()) unwatchParkNetwork()
    }""")

# ---------------------------------------------------------------- endAllKept: every lease too
rep("""        kept.clear()
        nativeEntries = emptyMap()
        connectingHandle = 0L
        pendingCommit = null
        unwatchParkNetwork()
""", """        kept.clear()
        nativeEntries = emptyMap()
        connectingHandle = 0L
        pendingCommit = null
        releaseAllStandby(reason)  // model A: the leases of this sign-in / this run go with it
        unwatchParkNetwork()
""")

# ---------------------------------------------------------------- the old park: noteKeepUnsupported .. parkToHostList
cut("    // it, only 연결 해제. Said once per connection, as soon as its first list tells (the capability arrives with it).\n",
    "    /** Every status tick: what the native table says about each kept PC; a held tap once nothing is confirming. */")
# the comment line before the cut start belongs to it as well: find and remove the dangling header if any
rep("""    /** Every status tick: what the native table says about each kept PC; a held tap once nothing is confirming. */""",
    """    /** Whether "PC 목록" is offered for the PC on screen: a directory PC (a raw-IP connection has 연결 해제 only). */
    private fun listReturnOffered(): Boolean = activeHandle != 0L && kept[activeHandle]?.manual == false

    /**
     * keepalive-r2 ② e, kept model A (codex_keepalive_r2_kept_model.md, codex_ka_e_switch_decision.md): "PC 목록".
     * The screen's fences first (input released, selection stopped, the decoder's stream reset), then the session
     * ENDS -- its Disconnect, then (a lease's) SessionOver on the table's closer thread, so the lease pings again on the
     * same socket. Then: back to that lease's standby; the first transition of an ordinary directory session (a new
     * lease -- its directory requests are new and logged as such); or, for a PC that cannot stand by (raw IP, an older
     * host, a pre-login worker, a directory without standby), "상시 유지 미지원" -- the session is closed all the same.
     */
    private fun parkToHostList(origin: String) {
        val handle = activeHandle
        if (handle == 0L) return
        val k = kept[handle]
        val rk = k?.let { recentKeyOf(it) }
        val fromStandby = standbyHandles[handle] != null
        val prelogin = (NativeSessionBridge.nativeHostCaptureFlags(activeBinding) and RemoteRebootPolicy.FLAG_PRELOGIN_NOW) != 0
        val decision = StandbyKeep.onListReturn(
            fromStandby = fromStandby, directory = rk != null, supported = standbySupport.supported, prelogin = prelogin,
            knownUnsupported = rk != null && standbyBook.isUnsupported(rk), enabled = true)
        diagnosticsLog.log("list_return",
            "origin=$origin host=$directoryHostName handle=$handle decision=$decision fromStandby=$fromStandby " +
                "supported=${standbySupport.supported} prelogin=$prelogin " + NativeSessionBridge.nativeSessionCounters(handle))
        dismissViewerLogDialog()
        closeViewerSheets("list_return")
        releaseViewerModifiers()
        cancelActiveViewerTouch("list_return")
        hideViewerKeyboard("list_return")
        viewerKeyPanel?.hide()
        if (selectionStage != SelectionStage.IDLE) {
            NativeSessionBridge.nativeAbortVideoSwitch(activeBinding)
        } else {
            NativeSessionBridge.nativeResetVideoStream(activeBinding)
        }
        clearPendingSelection()
        resetViewerObservability()
        resetViewerOrientationState()
        desiredStreamActive = false
        pendingRuntimeConfigSync = false
        lastAppliedStreamActive = null
        entryOps.invalidate(handle)
        NativeSessionBridge.nativeEnd(handle,
            if (decision == StandbyKeep.ListReturn.UNSUPPORTED_END) "keep_unsupported" else "to_standby")
        forgetKept(handle)
        clearActive()
        connectFlowActive = false
        when (decision) {
            StandbyKeep.ListReturn.BACK_TO_STANDBY -> diagnosticsLog.log("standby_return", "host=${rk?.hostId.orEmpty()}")
            StandbyKeep.ListReturn.FIRST_TRANSITION -> {
                diagnosticsLog.log("standby_first_transition", "host=${rk?.hostId.orEmpty()} new_directory_requests=1")
                rk?.let { startStandby(it, "first_transition") }
            }
            StandbyKeep.ListReturn.UNSUPPORTED_END -> {
                diagnosticsLog.log("standby_unsupported_end", "host=${k?.hostName.orEmpty()}")
                Toast.makeText(this, getString(R.string.hosts_standby_unsupported_end, k?.hostName.orEmpty()),
                    Toast.LENGTH_LONG).show()
            }
        }
        hostsStatusText.text = ""
        currentScene = UiScene.HOSTS
        hostListAdapter.notifyDataSetChanged()
        renderStatus()
    }

    // ---------------------------------------------------------------- keepalive-r2 ② e: the standby leases

    private fun ensureStandbyInit(): Boolean {
        if (!standbyInitDone) standbyInitDone = NativeSessionBridge.nativeStandbyInit(filesDir.absolutePath)
        return standbyInitDone
    }

    /** A lease for [key] (none yet): started on the standby thread; its capability is fetched when it asks. */
    private fun startStandby(key: RecentHosts.Key, reason: String) {
        if (standbyBook.has(key) || standbyBook.isUnsupported(key) || !ensureStandbyInit()) return
        val inc = standbyBook.begin(key)
        val nk = StandbyKeep.nativeKey(key)
        diagnosticsLog.log("standby_start", "host=${key.hostId} inc=$inc reason=$reason leases=${standbyBook.keys().size}")
        standbyExecutor.execute {
            if (!standbyBook.isCurrent(key, inc)) return@execute
            if (!NativeSessionBridge.nativeStandbyStart(nk)) {
                diagnosticsLog.log("standby_start_failed", "host=${key.hostId} inc=$inc")
                runOnUiThread { if (standbyBook.isCurrent(key, inc)) standbyBook.end(key) }
            }
        }
        watchParkNetwork()
    }

    private fun releaseStandby(key: RecentHosts.Key, reason: String) {
        if (!standbyBook.has(key)) return
        standbyBook.end(key)
        standbyLeases = standbyLeases - key
        diagnosticsLog.log("standby_release", "host=${key.hostId} reason=$reason")
        val nk = StandbyKeep.nativeKey(key)
        standbyExecutor.execute { NativeSessionBridge.nativeStandbyRelease(nk) }
    }

    private fun releaseAllStandby(reason: String) {
        val keys = standbyBook.endAll()
        standbyLeases = emptyMap()
        standbyHandles.clear()
        if (!standbyInitDone) return
        diagnosticsLog.log("standby_release_all", "reason=$reason leases=${keys.size}")
        standbyExecutor.execute { NativeSessionBridge.nativeStandbyReleaseAll() }
    }

    /**
     * Every status tick: each lease's native status. One that found an older host / a pre-login worker goes (and is
     * not tried again this sign-in); one that asks for a capability gets one (staging <= 1, its own back-off); a lease
     * whose PC left the recent three goes (never the one on screen).
     */
    private fun standbyTick() {
        val keys = standbyBook.keys()
        if (keys.isEmpty()) {
            if (standbyLeases.isNotEmpty()) standbyLeases = emptyMap()
            return
        }
        val now = SystemClock.elapsedRealtime()
        val next = HashMap<RecentHosts.Key, StandbyKeep.Lease>()
        for (key in keys) {
            val lease = StandbyKeep.Lease.of(NativeSessionBridge.nativeStandbyStatus(StandbyKeep.nativeKey(key)))
            val before = standbyLeases[key]
            if (before == null || before.state != lease.state || before.handedOff != lease.handedOff || before.lost != lease.lost) {
                diagnosticsLog.log("standby_state",
                    "host=${key.hostId} exists=${lease.exists} state=${lease.state} handedOff=${lease.handedOff} " +
                        "lost=${lease.lost} needs=${lease.needsCapability}")
            }
            if (lease.exists && lease.state == StandbyKeep.LeaseState.UNSUPPORTED) {
                standbyBook.markUnsupported(key)
                releaseStandby(key, "unsupported")
                hostsStatusText.text = getString(R.string.hosts_standby_unsupported_host,
                    directoryHosts.firstOrNull { it.hostId == key.hostId }?.hostName ?: key.hostId)
                continue
            }
            next[key] = lease
            if (lease.exists && lease.needsCapability && !lease.handedOff) requestStandbyCapability(key, now)
        }
        standbyLeases = next
        val origin = directoryOrigin()
        val account = DirectoryClient.sessionAccount()
        val recent = recentHosts.recent(origin, account).map { RecentHosts.Key(origin, account, it) }
        for (gone in StandbyKeep.evictions(recent, keys.filter { standbyBook.has(it) }, standbyHandles[activeHandle])) {
            releaseStandby(gone, "lru")
        }
        if (standbyBook.keys().isEmpty() && kept.values.none { it.parkedAtMs != 0L }) unwatchParkNetwork()
    }

    /**
     * The capability a lease asked for: an observation from THAT lease's socket, the directory's standby connect, and
     * the aim (the relay's socket when reserved, else the host). Provided only while that lease is still the current one
     * for its PC (not released, not a new one under the same key, not another sign-in). Nothing secret is logged.
     */
    private fun requestStandbyCapability(key: RecentHosts.Key, nowMs: Long) {
        val inc = standbyBook.incarnation(key)
        if (!standbyBook.startStaging(key, inc, nowMs)) return
        val url = DirectoryClient.directoryUrl
        val token = DirectoryClient.session()
        val endpoint = directoryObserveEndpoint
        val nk = StandbyKeep.nativeKey(key)
        diagnosticsLog.log("standby_capability_begin", "host=${key.hostId} inc=$inc")
        standbyExecutor.execute {
            var ok = false
            var what = ""
            try {
                if (!standbyBook.isCurrent(key, inc) || url.isEmpty() || token.isEmpty()) {
                    what = "stale"
                } else {
                    val target = DirectoryClient.resolveObserveTarget(url, endpoint)
                    if (target is DirectoryClient.ObserveTarget.Refused) throw DirectoryClient.DirectoryException(target.message)
                    val ready = target as DirectoryClient.ObserveTarget.Ready
                    val observeToken = DirectoryClient.newObserveToken()
                    val observe = {
                        if (!NativeSessionBridge.nativeStandbyObserve(nk, ready.host, ready.port, observeToken)) {
                            throw DirectoryClient.DirectoryException("standby observe failed")
                        }
                    }
                    observe()
                    val t = try {
                        DirectoryClient.connect(url, token, key.hostId, observeToken, standby = true)
                    } catch (e: DirectoryClient.DirectoryException) {
                        if (e.status != 409 || !e.serverError.startsWith("observation")) throw e
                        observe()
                        DirectoryClient.connect(url, token, key.hostId, observeToken, standby = true)
                    }
                    val aim = DirectoryClient.standbyAim(t) ?: throw DirectoryClient.DirectoryException("no standby target")
                    what = "relay=${t.standbyRelay.ifEmpty { "-" }} aim=${if (t.standbyRelay == "reserved") "relay" else "host"}"
                    if (!standbyBook.isCurrent(key, inc)) {
                        what += " stale"  // released / replaced meanwhile: this capability goes nowhere
                    } else {
                        ok = NativeSessionBridge.nativeStandbyProvide(nk, t.punchToken, aim.first, aim.second)
                        if (!ok) what += " provide_refused"
                    }
                }
            } catch (e: Exception) {
                what = "error=${e.message.orEmpty()}"
            } finally {
                val done = ok
                diagnosticsLog.log("standby_capability", "host=${key.hostId} inc=$inc ok=$done $what")
                runOnUiThread { standbyBook.endStaging(key, inc, done, SystemClock.elapsedRealtime(), Math.random()) }
            }
        }
    }

    /**
     * Model A: a NEW session entry for this PC, connected by its ready lease's activation (nativeConnectStandby) -- no
     * directory, observe, punch or ordinary Hello. Committed like any connect once its session is established. A lease
     * that cannot be used goes, and the tap becomes the ordinary connect, once.
     */
    private fun activateStandby(host: DirectoryClient.Host, rk: RecentHosts.Key) {
        val key = SessionPark.entryKey(rk.directoryOrigin, rk.accountId, host.hostId)
        val epoch = NativeSessionBridge.nativeEpoch()
        val reservation = NativeSessionBridge.nativeReserve(key, false, epoch, false)
        if (reservation.size < 3 || reservation[0] != 1L) {
            diagnosticsLog.log("keep_reserve_refused",
                "host=${host.hostId} kind=${reservation.getOrNull(0)} handle=${reservation.getOrNull(1)} standby=1")
            hostsStatusText.text = getString(R.string.hosts_keep_refused)
            return
        }
        val handle = reservation[1]
        kept[handle] = SessionPark.Kept(handle, host.hostId, host.hostName, rk.accountId, rk.directoryOrigin, manual = false)
        connectingHandle = handle
        standbyHandles[handle] = rk
        setDirectoryBusy(true)
        directoryConnectingName = host.hostName
        hostsStatusText.text = getString(R.string.hosts_connecting, host.hostName)
        val t0 = SystemClock.elapsedRealtime()
        startTargetsReadyWatch("standby", t0)
        val op = entryOps.begin(handle)
        val nk = StandbyKeep.nativeKey(rk)
        diagnosticsLog.log("standby_activate",
            "host=${host.hostName} id=${host.hostId} handle=$handle op=$op " + NativeSessionBridge.nativeSessionCounters(handle))
        Thread {
            val gen = NativeSessionBridge.nativeConnectStandby(handle, nk, op)
            val why = if (gen == 0L) NativeSessionBridge.nativeDirectoryLastError(handle) else ""
            runOnUiThread {
                val mine = entryOps.isCurrent(handle, op) && NativeSessionBridge.nativeEpoch() == epoch
                if (gen != 0L && mine) {
                    diagnosticsLog.log("directory_connect_started", "handle=$handle op=$op gen=$gen standby=1")
                    pendingCommit = PendingCommit(handle, op, gen, epoch, t0, "standby", manual = false)
                    tryCommit()
                    return@runOnUiThread
                }
                diagnosticsLog.log("standby_activate_failed", "handle=$handle op=$op gen=$gen why=$why mine=$mine")
                // its entry ends (the closer: Disconnect, then SessionOver of this activation if it ever started)
                NativeSessionBridge.nativeFail(handle, if (mine) "standby_activate_failed" else "stale_operation")
                if (kept[handle] != null && activeHandle != handle) forgetKept(handle)
                if (connectingHandle == handle) connectingHandle = 0L
                if (!mine) return@runOnUiThread
                setDirectoryBusy(false)
                directoryConnectingName = ""
                releaseStandby(rk, "activate_failed")
                connectToDirectoryHost(host)
            }
        }.start()
    }

    /** Every status tick: what the native table says about each kept PC; a held tap once nothing is confirming. */""")

# keepTick: the leases first; the held tap waits for ITS PC's lease to stop handing off
rep("""    private fun keepTick() {
        tryCommit()
        if (kept.isEmpty() && heldTap == null) {""", """    private fun keepTick() {
        tryCommit()
        standbyTick()
        if (kept.isEmpty() && heldTap == null) {""")
rep("""        val held = heldTap
        if (held != null && !anyParkConfirming()) {""", """        val held = heldTap
        val heldLeaseReturning = held != null &&
            standbyLeases[RecentHosts.Key(held.origin, held.account, held.hostId)]?.handedOff == true
        if (held != null && !anyParkConfirming() && !heldLeaseReturning) {""")

# resumeKept goes (M1: the park_reuse path is removed with the park)
cut("    /** The same PC again: its target list on its own connection. false = the native side refused (it is ended). */\n",
    "    private fun readLocalClipboardText(): String? = try {")

# renderKeepBanner: count the leases
rep("""        val parkedCount = kept.keys.count { it != activeHandle }
        if (parkedCount == 0) {""", """        val parkedCount = standbyBook.keys().size
        if (parkedCount == 0) {""")

# window list: no old "keep unsupported" note (model A decides at "PC 목록")
rep("        noteKeepUnsupported(panelSnapshot)\n", "")
rep("        viewerMenuHostsButton.visibility = if (parkAllowedHere()) View.VISIBLE else View.GONE",
    "        viewerMenuHostsButton.visibility = if (listReturnOffered()) View.VISIBLE else View.GONE")
rep("        listHostsButton.visibility = if (parkAllowedHere()) View.VISIBLE else View.GONE",
    "        listHostsButton.visibility = if (listReturnOffered()) View.VISIBLE else View.GONE")

# onStop: a lease alone is something to end
rep("""            (activeHandle != 0L || kept.isNotEmpty() || connectingHandle != 0L || pendingCommit != null)) {""",
    """            (activeHandle != 0L || kept.isNotEmpty() || connectingHandle != 0L || pendingCommit != null ||
                standbyBook.keys().isNotEmpty())) {""")

# loadHosts: the standby support with the list (read-only)
rep("""                var list: List<DirectoryClient.Host>
                try {
                    list = DirectoryClient.hosts(url, token)""", """                var list: List<DirectoryClient.Host>
                var support: DirectoryClient.StandbySupport
                try {
                    val got = DirectoryClient.hostList(url, token)
                    list = got.hosts
                    support = got.standby""")
rep("""                    list = DirectoryClient.hosts(url, back.sessionToken)""", """                    val again = DirectoryClient.hostList(url, back.sessionToken)
                    list = again.hosts
                    support = again.standby""")
rep("""                    setDirectoryBusy(false)
                    directoryHosts = list
""", """                    setDirectoryBusy(false)
                    directoryHosts = list
                    if (standbySupport != support) diagnosticsLog.log("standby_support", "supported=${support.supported} relay=${support.relay}")
                    standbySupport = support
""")

# considerPreconnect: the automatic preparation (off in shipped builds)
i = s.index("    /**\n     * keepalive r2: whether to prepare one of the recent three now.")
j = s.index("    private fun flowLog(line: String)", i)
s = s[:i] + """    /**
     * keepalive-r2 ② e: the app's OWN preparation of the recent PCs (start / return / list refresh). Off in every shipped
     * build (Preconnect.AUTO_PREPARE, codex_ka_e_switch_decision.md) -- said once in the log; a user's "PC 목록" from a
     * supported PC still goes to standby. On (a debug test build): a lease for each recent PC that is online, not
     * suppressed, not found unsupported and not kept already (the capability jobs go one at a time, each with its back-off).
     */
    private fun considerPreconnect(reason: String) {
        val account = DirectoryClient.sessionAccount()
        val origin = directoryOrigin()
        val recent = recentHosts.recent(origin, account)
        if (!Preconnect.AUTO_PREPARE) {
            if (!preconnectOffLogged) {
                preconnectOffLogged = true
                diagnosticsLog.log("preconnect_off", "reason=$reason why=auto_prepare_off recent=${recent.size}")
            }
            return
        }
        if (!standbySupport.supported) {
            diagnosticsLog.log("preconnect_skip", "reason=$reason why=directory_without_standby")
            return
        }
        val listed = directoryHosts.associateBy { it.hostId }
        for (hostId in recent.take(RecentHosts.MAX)) {
            val key = RecentHosts.Key(origin, account, hostId)
            if (listed[hostId]?.online != true || keepSuppression.isSuppressed(key) || standbyBook.isUnsupported(key)) continue
            if (standbyBook.has(key) || kept.values.any { recentKeyOf(it) == key }) continue
            startStandby(key, "auto_$reason")
        }
    }

""" + s[j:]

# the tap: a lease decides first; the old park reuse is gone
rep("""        // a kept PC decides first -- before "offline", since the list's online flag lags the connection this
        // phone already holds.
        keepTick()""", """        // keepalive-r2 ② e, kept model A: a PC with a lease decides first -- ready: activate THAT lease; its last
        // session still handing the socket back: the tap waits (carried out when it settles); anything else: the
        // ordinary connect (a lease not ready is let go first: one thing per PC).
        val rk = RecentHosts.Key(origin, account, host.hostId)
        standbyTick()
        when (val st = StandbyKeep.onTap(if (standbyBook.has(rk)) standbyLeases[rk] else null)) {
            StandbyKeep.Tap.Activate -> {
                activateStandby(host, rk)
                return
            }
            StandbyKeep.Tap.Wait -> {
                val previous = heldTap
                heldTap = connectIntents.scheduleFallback(account, origin, host.hostId)
                diagnosticsLog.log("standby_tap_held", "tapped=${host.hostId} previous=${previous?.hostId.orEmpty()}")
                return
            }
            is StandbyKeep.Tap.Cold -> if (st.releaseFirst) releaseStandby(rk, "cold_tap")
        }
        // a PC still in the native table decides next -- before "offline", since the list's online flag lags the
        // connection this phone already holds.
        keepTick()""")
rep("""            is SessionPark.Tap.Reuse -> {
                if (resumeKept(tap.handle)) return
                // refused at the moment of adoption (its limit, its pause): ended -- this tap connects anew, once
            }""", """            is SessionPark.Tap.Reuse -> {
                // model A: no parked control session is reused (none is made any more) -- one left over ends, once
                NativeSessionBridge.nativeEnd(tap.handle, "no_park_reuse")
                forgetKept(tap.handle)
            }""")
rep("""                val h = reservation[1]
                if (kept[h] == null) kept[h] = SessionPark.Kept(h, host.hostId, host.hostName, account, origin, manual = false)
                if (!anyParkConfirming() && resumeKept(h)) return
""", """                // a live entry the screen did not know: model A reuses no session -- it ends; tap again
                val h = reservation[1]
                NativeSessionBridge.nativeEnd(h, "no_park_reuse")
                forgetKept(h)
""")

# the card: a lease says its state; its 끊기 lets it go
rep("""            val native = keptEntry?.let { nativeEntries[it.handle] }
            val keepState = SessionPark.listState(native, keptEntry != null && keptEntry.handle == activeHandle)
            val isKept = keepState == SessionPark.ListState.KEPT || keepState == SessionPark.ListState.CONFIRMING
            view.findViewById<TextView>(R.id.hostCardDetail).text = when (keepState) {""", """            val native = keptEntry?.let { nativeEntries[it.handle] }
            val keepState = SessionPark.listState(native, keptEntry != null && keptEntry.handle == activeHandle)
            // keepalive-r2 ② e: this PC's lease (model A) -- only a confirmed entry on a live lease reads as ready
            val rk = RecentHosts.Key(directoryOrigin(), DirectoryClient.sessionAccount(), host.hostId)
            val lease = if (standbyBook.has(rk)) standbyLeases[rk] ?: StandbyKeep.Lease.NONE else null
            val shown = lease?.let { StandbyKeep.shown(it, standbyHandles[activeHandle] == rk) }
                ?: if (lease != null) StandbyKeep.Shown.PREPARING else null
            val isKept = shown != null
            view.findViewById<TextView>(R.id.hostCardDetail).text = if (shown != null) when (shown) {
                StandbyKeep.Shown.READY -> getString(R.string.hosts_standby_ready_detail)
                StandbyKeep.Shown.PREPARING -> getString(R.string.hosts_standby_preparing)
                StandbyKeep.Shown.RETRY -> getString(R.string.hosts_standby_retry)
                StandbyKeep.Shown.RETURNING -> getString(R.string.hosts_standby_returning)
                StandbyKeep.Shown.UNSUPPORTED -> getString(R.string.hosts_standby_unsupported)
                StandbyKeep.Shown.ACTIVATING, StandbyKeep.Shown.ACTIVE -> getString(R.string.hosts_standby_activating)
            } else when (keepState) {""")
rep("""            state.text = when {
                isKept -> getString(R.string.hosts_parked_state)""", """            state.text = when {
                shown == StandbyKeep.Shown.READY -> getString(R.string.hosts_standby_state)
                isKept -> getString(R.string.hosts_standby_state_pending)""")
rep("""            end.setOnClickListener {
                val h = keptEntry?.handle ?: return@setOnClickListener
                diagnosticsLog.log("park_disconnect_tap", "handle=$h host=${host.hostName}")""", """            end.setOnClickListener {
                if (standbyBook.has(rk)) {
                    // this PC's standby goes, and it is not kept automatically again (until the user connects it)
                    diagnosticsLog.log("standby_disconnect_tap", "host=${host.hostName}")
                    keepSuppression.suppress(rk)
                    releaseStandby(rk, "user_disconnect")
                    hostsStatusText.text = getString(R.string.hosts_park_ended, host.hostName)
                    notifyDataSetChanged()
                    renderStatus()
                    return@setOnClickListener
                }
                val h = keptEntry?.handle ?: return@setOnClickListener
                diagnosticsLog.log("park_disconnect_tap", "handle=$h host=${host.hostName}")""")

# 모두 끊기: the leases' PCs are suppressed too
rep("""            for (k in kept.values) recentKeyOf(k)?.let { keepSuppression.suppress(it) }  // keepalive r2""",
    """            for (k in kept.values) recentKeyOf(k)?.let { keepSuppression.suppress(it) }  // keepalive r2
            for (key in standbyBook.keys()) keepSuppression.suppress(key)  // model A: the leases' PCs as well""")

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
