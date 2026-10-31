import sys
root = sys.argv[1]

def edit(p, pairs):
    p = root + '/' + p
    raw = open(p, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    s = raw.decode('utf-8-sig').replace('\r\n', '\n')
    for o, n in pairs:
        if s.count(o) != 1:
            raise SystemExit('count %d in %s: %r' % (s.count(o), p, o[:90]))
        s = s.replace(o, n)
    open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.replace('\n', '\r\n').encode('utf-8'))

M = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'

SUCCESS_OLD = '''                    connectingHandle = 0L
                    setDirectoryBusy(false)
                    directoryConnectingName = ""
                    // up: the eviction its reservation made (if the limit needed one) happens now
                    for (evicted in NativeSessionBridge.nativeCommit(handle)) {
                        val gone = kept[evicted]
                        forgetKept(evicted)
                        diagnosticsLog.log("keep_end", "handle=$evicted host=${gone?.hostName.orEmpty()} reason=lru")
                        if (gone != null) {
                            hostsStatusText.text = getString(R.string.hosts_keep_ended, gone.hostName,
                                SessionPark.endedReasonText("lru"))
                        }
                    }
                    if (!activateKept(handle)) {
                        // another PC still on screen / confirming: should not happen from the list; this one ends
                        diagnosticsLog.log("keep_activate_refused", "handle=$handle table=${NativeSessionBridge.nativeEntriesJson()}")
                        NativeSessionBridge.nativeEnd(handle, "activate_refused")
                        forgetKept(handle)
                        renderStatus()
                        return@runOnUiThread
                    }
                    connectionPathKind = pathKind
                    connectFlowActive = true
                    pendingRuntimeConfigSync = true
                    pendingDesktopBackendSync = true
                    desiredStreamActive = false
                    lastAppliedStreamActive = null
                    clipboardResumeHold = false
                    clearPendingSelection()
                    currentScene = UiScene.TARGETS
                    NativeSessionBridge.nativeRequestWindowList(activeBinding)
                    hostListAdapter.notifyDataSetChanged()
                    renderStatus()
                }'''
SUCCESS_NEW = '''                    // r2 M1: the worker started -- not yet a connection. Commit (and the eviction its reservation
                    // made) only once its Hello is answered and control is up; until then nothing kept is touched.
                    diagnosticsLog.log("directory_connect_started", "handle=$handle op=$op gen=$madeGeneration")
                    pendingCommit = PendingCommit(handle, op, madeGeneration, epoch, SystemClock.elapsedRealtime(),
                        pathKind, manual = false)
                    tryCommit()
                }'''

edit(M, [
(SUCCESS_OLD, SUCCESS_NEW),
# --- fields
('''    private var connectingHandle = 0L
''', '''    private var connectingHandle = 0L
    // r2 M1: a connect whose worker started, waiting for its session to be established before it is committed
    private data class PendingCommit(
        val handle: Long, val op: Long, val gen: Long, val epoch: Long, val startedAtMs: Long,
        val pathKind: String, val manual: Boolean,
    )
    private var pendingCommit: PendingCommit? = null
'''),
# --- manual connect
('''        val op = entryOps.begin(handle)
        var ok = NativeSessionBridge.nativeConnect(handle, host, videoPort, controlPort, op) != 0L
        if (ok) {
            for (evicted in NativeSessionBridge.nativeCommit(handle)) {
                diagnosticsLog.log("keep_end", "handle=$evicted host=${kept[evicted]?.hostName.orEmpty()} reason=lru")
                forgetKept(evicted)
            }
            ok = activateKept(handle)
            if (!ok) NativeSessionBridge.nativeEnd(handle, "activate_refused")
        } else {
            diagnosticsLog.log("connect_failed", NativeSessionBridge.nativeGetLastError(handle))
            NativeSessionBridge.nativeFail(handle, "connect_failed")
        }
        if (!ok) forgetKept(handle)
        if (ok) {
            connectFlowActive = true
            pendingRuntimeConfigSync = true
            pendingDesktopBackendSync = true
            desiredStreamActive = false
            lastAppliedStreamActive = null
            clearPendingSelection()
            currentScene = UiScene.TARGETS
            NativeSessionBridge.nativeRequestWindowList(activeBinding)
        }
        renderStatus()
    }''', '''        if (connectingHandle != 0L) cancelConnecting("superseded")  // r2 M2: the newest intent wins
        val epoch = NativeSessionBridge.nativeEpoch()
        val op = entryOps.begin(handle)
        val gen = NativeSessionBridge.nativeConnect(handle, host, videoPort, controlPort, op)
        if (gen != 0L) {
            // r2 M1: committed (and shown) only once its session is established -- tryCommit, from the status ticks
            connectingHandle = handle
            pendingCommit = PendingCommit(handle, op, gen, epoch, SystemClock.elapsedRealtime(), "", manual = true)
            tryCommit()
        } else {
            diagnosticsLog.log("connect_failed", NativeSessionBridge.nativeGetLastError(handle))
            NativeSessionBridge.nativeFail(handle, "connect_failed")
            forgetKept(handle)
        }
        renderStatus()
    }

    /**
     * r2 M1: a connect whose worker started becomes this screen's PC only once its session is established
     * (authenticated Hello + control) -- the native table refuses an earlier commit, and only then does the eviction
     * its reservation made happen. Called on every status tick while one is pending; never waits.
     */
    private fun tryCommit() {
        val p = pendingCommit ?: return
        val now = SystemClock.elapsedRealtime()
        if (!entryOps.isCurrent(p.handle, p.op) || NativeSessionBridge.nativeEpoch() != p.epoch) {
            pendingCommit = null
            diagnosticsLog.log("directory_connect_stale", "handle=${p.handle} op=${p.op} step=commit")
            NativeSessionBridge.nativeFail(p.handle, "stale_operation")
            if (activeHandle != p.handle) forgetKept(p.handle)
            return
        }
        val lastError = NativeSessionBridge.nativeGetLastError(p.handle)
        val r = NativeSessionBridge.nativeCommit(p.handle, p.gen)
        val status = r.getOrNull(0) ?: -1L
        if (status == 0L) {
            if (now - p.startedAtMs < ESTABLISH_LIMIT_MS) return
            NativeSessionBridge.nativeFail(p.handle, "establish_timeout")
        }
        pendingCommit = null
        if (status != 1L) {
            // it ended (or never answered) before it was established: given back -- no kept PC was touched for it
            val why = if (status == 0L) "establish_timeout" else lastError.ifEmpty { "not_established" }
            diagnosticsLog.log("directory_connect_failed", "handle=${p.handle} op=${p.op} step=establish $why " +
                "evicted=0 kept=${kept.size - 1}")
            forgetKept(p.handle)
            setDirectoryBusy(false)
            directoryConnectingName = ""
            if (p.manual) {
                diagnosticsLog.log("connect_failed", why)
            } else {
                hostsStatusText.text = getString(R.string.hosts_connect_failed, why)
            }
            renderStatus()
            return
        }
        connectingHandle = 0L
        setDirectoryBusy(false)
        directoryConnectingName = ""
        diagnosticsLog.log("directory_connect_committed", "handle=${p.handle} ms=${now - p.startedAtMs} evicted=${r.size - 1}")
        // the eviction its reservation made (if the limit needed one) happens now
        for (i in 1 until r.size) {
            val evicted = r[i]
            val gone = kept[evicted]
            forgetKept(evicted)
            diagnosticsLog.log("keep_end", "handle=$evicted host=${gone?.hostName.orEmpty()} reason=lru")
            if (gone != null) {
                hostsStatusText.text = getString(R.string.hosts_keep_ended, gone.hostName,
                    SessionPark.endedReasonText("lru"))
            }
        }
        if (!activateKept(p.handle)) {
            // another PC still on screen / confirming: should not happen from the list; this one ends
            diagnosticsLog.log("keep_activate_refused", "handle=${p.handle} table=${NativeSessionBridge.nativeEntriesJson()}")
            NativeSessionBridge.nativeEnd(p.handle, "activate_refused")
            forgetKept(p.handle)
            renderStatus()
            return
        }
        connectionPathKind = p.pathKind
        connectFlowActive = true
        pendingRuntimeConfigSync = true
        pendingDesktopBackendSync = true
        desiredStreamActive = false
        lastAppliedStreamActive = null
        clipboardResumeHold = false
        clearPendingSelection()
        currentScene = UiScene.TARGETS
        NativeSessionBridge.nativeRequestWindowList(activeBinding)
        hostListAdapter.notifyDataSetChanged()
        renderStatus()
    }

    /**
     * r2 M2: the connect in progress gives way to a newer tap -- its own operation is made stale and its entry given
     * back at once (the native close stops its network step; nothing waits for its timeout). Other PCs untouched.
     */
    private fun cancelConnecting(reason: String) {
        val h = connectingHandle
        if (h == 0L) return
        entryOps.invalidate(h)
        if (pendingCommit?.handle == h) pendingCommit = null
        NativeSessionBridge.nativeFail(h, reason)
        diagnosticsLog.log("directory_connect_superseded", "handle=$h host=${kept[h]?.hostName.orEmpty()} reason=$reason")
        forgetKept(h)
        connectingHandle = 0L
        setDirectoryBusy(false)
        directoryConnectingName = ""
    }

    /**
     * r2 L1: everything ends -- the PC on screen, the kept ones and a connect in progress -- with a new lifecycle
     * epoch, and the screen goes back to where the app starts. Leaving the app, a confirmed network change and
     * 모두 끊기. (Rotation is not this: configuration changes are handled in place.)
     */
    private fun endEverything(reason: String) {
        dismissViewerLogDialog()
        closeViewerSheets(reason)
        releaseViewerModifiers()
        cancelActiveViewerTouch(reason)
        hideViewerKeyboard(reason)
        viewerKeyPanel?.hide()
        if (activeBinding != 0L) {
            if (selectionStage != SelectionStage.IDLE) {
                NativeSessionBridge.nativeAbortVideoSwitch(activeBinding)
            } else {
                NativeSessionBridge.nativeResetVideoStream(activeBinding)
            }
        }
        endAllKept(reason)
        setDirectoryBusy(false)
        directoryConnectingName = ""
        connectFlowActive = false
        clearPendingSelection()
        resetViewerObservability()
        currentScene = homeScene()
    }'''),
# --- endAllKept clears a pending commit
('''        kept.clear()
        nativeEntries = emptyMap()
        connectingHandle = 0L''', '''        kept.clear()
        nativeEntries = emptyMap()
        connectingHandle = 0L
        pendingCommit = null'''),
# --- keepTick drives the pending commit
('''    private fun keepTick() {
        if (kept.isEmpty() && heldTap == null) {''', '''    private fun keepTick() {
        tryCommit()
        if (kept.isEmpty() && heldTap == null) {'''),
# --- statusPoll
('''        if (heldTap != null || kept.values.any { it.parkedAtMs != 0L && !it.confirmLogged }) return 250L''',
 '''        if (heldTap != null || pendingCommit != null ||
            kept.values.any { it.parkedAtMs != 0L && !it.confirmLogged }) return 250L'''),
# --- status of the pending connect while nothing is on screen
('''        val statusValue = NativeSessionBridge.nativeGetStatus(activeHandle)''',
 '''        // a connect not committed yet shows as connecting (its session is not this screen's until it is)
        val statusHandle = if (activeHandle != 0L) activeHandle else pendingCommit?.handle ?: 0L
        val statusValue = NativeSessionBridge.nativeGetStatus(statusHandle)'''),
('''        } else if (onIdleScreen && connectFlowActive && activeHandle != 0L) {''',
 '''        } else if (onIdleScreen && connectFlowActive && activeHandle != 0L && pendingCommit == null) {'''),
# --- M2: taps while connecting
('''    private fun connectToDirectoryHost(host: DirectoryClient.Host) {
        if (directoryBusy) return
        val account = DirectoryClient.sessionAccount()
        val origin = directoryOrigin()
        val key = SessionPark.entryKey(origin, account, host.hostId)''', '''    private fun connectToDirectoryHost(host: DirectoryClient.Host) {
        val account = DirectoryClient.sessionAccount()
        val origin = directoryOrigin()
        val key = SessionPark.entryKey(origin, account, host.hostId)
        // r2 M2: a tap while another PC is still connecting is the user's newest intent -- that connect gives way
        // (its own operation only; it is not waited for), and this tap is carried out now. The same PC again is
        // the same intent.
        if (connectingHandle != 0L) {
            if (kept[connectingHandle]?.key == key) {
                diagnosticsLog.log("connect_tap_dup", "host=${host.hostId} handle=$connectingHandle")
                return
            }
            cancelConnecting("superseded")
        }'''),
# --- the list stays tappable while a connect runs
('''        hostsRefreshButton.isEnabled = !busy
        hostsListView.isEnabled = !busy
    }''', '''        hostsRefreshButton.isEnabled = !busy
        // r2 M2: the list stays tappable -- a tap while a PC connects is the newest intent (connectToDirectoryHost)
    }'''),
# --- L1 onStop
('''        // keepalive r1 / multi §6: a kept PC lives only while the app is in front. Backgrounded, locked or switched
        // away from, every parked connection is ended for real -- no foreground service keeps them. (The PC on
        // screen is left as 0.2.30 left it.)
        // r3 R2: a connect still on its way is not carried on in the background either -- it stops at its
        // next step (native included) and adopts nothing; the list is not left busy.
        endParkedKept("app_stopped")
        if (directoryBusy) {
            setDirectoryBusy(false)
            directoryConnectingName = ""
        }''', '''        // keepalive-multi r2 L1: keeping PCs is for while the app is in front. Backgrounded, locked or switched away
        // from, EVERY connection ends -- the PC on screen, the kept ones and a connect in progress -- with a new
        // lifecycle epoch (0.2.30 left the session on screen running in the background; that changes here). No
        // foreground service keeps anything. Rotation does not stop the Activity (configChanges), and a stop
        // during a configuration change is not leaving the app.
        // r3 R2: a connect still on its way is not carried on in the background either -- it stops at its
        // next step (native included) and adopts nothing; the list is not left busy.
        if (!isChangingConfigurations &&
            (activeHandle != 0L || kept.isNotEmpty() || connectingHandle != 0L || pendingCommit != null)) {
            endEverything("app_stopped")
        }
        if (directoryBusy) {
            setDirectoryBusy(false)
            directoryConnectingName = ""
        }'''),
# --- L1 network
('''                // multi §6: a confirmed network change ends the kept PCs; with none on screen it is the whole
                // lifecycle (a new epoch: nothing started before it is carried out).
                if (activeHandle == 0L) endAllKept(reason) else endParkedKept(reason)
                renderStatus()''', '''                // r2 L1: a confirmed network change ends everything -- the PC on screen too -- with a new epoch
                // (nothing started before it is carried out). Watched while at least one PC is kept parked; a
                // lone PC on screen is not watched (0.2.30's single session), its own session sees its path go.
                endEverything(reason)
                renderStatus()'''),
# --- L1 모두 끊기
('''            if (activeHandle == 0L) endAllKept("disconnect_all") else endParkedKept("disconnect_all")
            renderStatus()''', '''            endEverything("disconnect_all")  // r2 L1: what it says -- every PC, the one on screen included
            renderStatus()'''),
])

# the limit for an established session, next to the class constants
edit(M, [('''    companion object {''', '''    companion object {
        // r2 M1: how long a started connect may take to be established before it is given back (its own Hello /
        // control timeouts normally end it sooner)
        private const val ESTABLISH_LIMIT_MS = 20_000L''')])
print('ok')
