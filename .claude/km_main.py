import re, sys
root = sys.argv[1]
P = root + '/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'
raw = open(P, 'rb').read().decode('utf-8')
bom = raw.startswith('﻿')
s = raw.lstrip('﻿').replace('\r\n', '\n')
park = open(root + '/.claude/km_main_park.kt.txt', encoding='utf-8').read()
conn = open(root + '/.claude/km_main_connect.kt.txt', encoding='utf-8').read()

def sub(old, new, count=1):
    global s
    if s.count(old) != count:
        raise SystemExit('count %d != %d: %r' % (s.count(old), count, old[:90]))
    s = s.replace(old, new)

def block(start, end, text):
    global s
    a = s.index(start)
    b = s.index(end, a)
    s = s[:a] + text + s[b:]

block('    /** 연결 해제: the session ends for real', '    private fun startTargetsReadyWatch(', park)
block('    /**\n     * Address exchange, then punch', '    private fun setDirectoryBusy(', conn)

sub('import org.json.JSONException\n', 'import org.json.JSONArray\nimport org.json.JSONException\n')

# ---- fields
a = s.index('    // keepalive r1: the name shown for it, and the one parked connection (SessionPark).\n')
b = s.index('    // How long a tap took to a usable target list')
s = s[:a] + '''    // keepalive r1: the name shown for it.
    private var directoryHostName = ""
    // keepalive-multi r1 (SessionPark, native session table): the PCs this phone keeps -- handle -> what the screen
    // knows of it. The PC on screen is [activeHandle] with its [activeBinding] (nativeActivate); every screen action
    // carries the binding read at the moment it is taken, so after a switch nothing of B can reach A.
    private val keepPolicy = SessionPark.Policy()
    private val kept = LinkedHashMap<Long, SessionPark.Kept>()
    private var activeHandle = 0L
    private var activeBinding = 0L
    // the directory connect in progress (its entry is reserved, not yet up)
    private var connectingHandle = 0L
    private var nativeEntries: Map<Long, SessionPark.NativeEntry> = emptyMap()
    // r2 K4 / multi §3: the latest tap made while a park was still confirming, carried out when it settles.
    private var heldTap: SessionPark.TapFallback? = null
    // r2 §7: a resume whose clipboard baseline could not be read holds pushes until a real change.
    private var clipboardResumeHold = false
    // r2 K2 per PC: the current connect operation of each entry, numbered by that entry's native gate.
    private val entryOps = SessionPark.EntryOps { handle -> NativeSessionBridge.nativeConnectOpNext(handle) }
    // r4 S2-b: scheduled intents (a held tap); every stop ends them for good. Owned by the lifecycle epoch.
    private val connectIntents = SessionPark.ConnectIntents { NativeSessionBridge.nativeEpoch() }
    private var parkEpisodeCounter = 0L
    private var parkNetwork: Network? = null
    private var parkNetworkCallback: ConnectivityManager.NetworkCallback? = null
''' + s[b:]

# ---- binding-scoped calls: the binding read now
BINDING = ['nativeAbortVideoSwitch', 'nativeResetVideoStream', 'nativePrepareVideoSwitch', 'nativeGetReadySelectionGeneration',
           'nativeRequestWindowList', 'nativeSelectWindow', 'nativeSelectDesktopMode', 'nativeSelectMonitor', 'nativeSelectMonitorAt',
           'nativeIsHostScreenLocked', 'nativeRequestRuntimeConfig', 'nativeRequestDesktopCaptureBackend', 'nativeRequestStreamActive',
           'nativeQueueInputEvent', 'nativeQueueInputText', 'nativeQueueClipboardText', 'nativeTakeIncomingClipboardText',
           'nativeTakeClipboardPushRequest', 'nativeHostSupportsClipboard', 'nativeGetWindowPanelJson', 'nativeGetWindowThumbnail',
           'nativeMacroStartPlayback', 'nativeMacroPump', 'nativeGetSessionBytesReceived', 'nativeSeedClipboardBaseline']
for n in BINDING:
    s = re.sub(r'(NativeSessionBridge\.%s)\(\)' % n, r'\1(activeBinding)', s)
    s = re.sub(r'(NativeSessionBridge\.%s)\((?!activeBinding)(?=\S)' % n, r'\1(activeBinding, ', s)
    s = re.sub(r'(NativeSessionBridge\.%s)\(\n(\s+)(?!activeBinding)' % n, r'\1(\n\2activeBinding,\n\2', s)
ENTRY = ['nativeGetStatus', 'nativeGetLastError', 'nativeSessionCounters']
for n in ENTRY:
    s = re.sub(r'(NativeSessionBridge\.%s)\(\)' % n, r'\1(activeHandle)', s)

# ---- exit
sub('''        if (parked != null) endPark("exit", announce = false)
        connectOwner.invalidate()  // r3 R2: a connect on its way adopts nothing after the exit
        NativeSessionBridge.nativeDisconnect()
        connectFlowActive = false''', '''        // r3 R2 / multi §6: every PC ends, and a connect on its way adopts nothing after the exit
        endAllKept("exit")
        connectFlowActive = false''')

# ---- manual connect
sub('''        // r4 S1: a new user intent -- any directory connect on its way is stale from here, and this one is
        // adopted through the same native gate (so no stale cleanup can close it).
        val op = connectOwner.begin()
        val ok = NativeSessionBridge.nativeConnect(host, videoPort, controlPort, op) != 0L
        if (ok) {''', '''        // r4 S1 / multi §2: the manual connect is an entry of the same native table (never kept on the list: a
        // manual connection is not parked), adopted through its own gate, so no stale cleanup can close it.
        val reservation = NativeSessionBridge.nativeReserve("manual|$host", true, NativeSessionBridge.nativeEpoch())
        if (reservation.size < 3 || reservation[0] != 1L) {
            diagnosticsLog.log("connect_failed", "reserve_refused kind=${reservation.getOrNull(0)}")
            renderStatus()
            return
        }
        val handle = reservation[1]
        kept[handle] = SessionPark.Kept(handle, "", host, "", "", manual = true)
        val op = entryOps.begin(handle)
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
        if (ok) {''')
sub('''            NativeSessionBridge.nativeRequestWindowList(activeBinding)
        } else {
            diagnosticsLog.log("connect_failed", NativeSessionBridge.nativeGetLastError(activeHandle))
        }
        renderStatus()''', '''            NativeSessionBridge.nativeRequestWindowList(activeBinding)
        }
        renderStatus()''')

# ---- onStop
sub('''        // keepalive r1: a park lives only while the list is in front. Backgrounded, locked or switched
        // away from, the connection is ended for real -- no foreground service keeps it.
        if (parked != null) endPark("app_stopped", announce = false)
        // r3 R2: a connect still on its way is not carried on in the background either -- it stops at its
        // next step (native included) and adopts nothing; the list is not left busy.
        if (directoryBusy) {
            connectOwner.invalidate()
            setDirectoryBusy(false)
            directoryConnectingName = ""
        }''', '''        // keepalive r1 / multi §6: a kept PC lives only while the app is in front. Backgrounded, locked or switched
        // away from, every parked connection is ended for real -- no foreground service keeps them. (The PC on
        // screen is left as 0.2.30 left it.)
        // r3 R2: a connect still on its way is not carried on in the background either -- it stops at its
        // next step (native included) and adopts nothing; the list is not left busy.
        endParkedKept("app_stopped")
        if (directoryBusy) {
            setDirectoryBusy(false)
            directoryConnectingName = ""
        }''')

# ---- sign-out
sub('''        if (parked != null) endPark("sign_out", announce = false)
        // r2 K2: a connect still on its way belonged to this sign-in: disowned, and the screen is not busy.
        connectOwner.invalidate()
        setDirectoryBusy(false)''', '''        // r2 K2 / multi §6: every PC of this sign-in ends and a connect still on its way is disowned (a new
        // lifecycle epoch); the screen is not busy.
        endAllKept("sign_out")
        setDirectoryBusy(false)''')

# ---- host list
sub('''                    // keepalive r1: a PC no longer in this account's list ends its park.
                    parked?.let { p -> if (list.none { it.hostId == p.hostId }) endPark("host_gone") }
''', '''                    // keepalive-multi r1 §6: a PC missing from one list refresh is not an ending -- its kept
                    // connection is authenticated on its own and ends only by its own reasons (0.2.30 ended it here).
''')
sub('''                        // r2 K2: the session is refused -- the park on it ends now, before the screen moves.
                        if (parked != null) endPark("session_refused", announce = false)
                        connectOwner.invalidate()
                        ++signInEpoch''', '''                        // r2 K2: the session is refused -- every PC kept on it ends now, before the screen moves.
                        endAllKept("session_refused")
                        ++signInEpoch''')

# ---- status
sub('        if (parked != null && !parkConfirmLogged) return 250L  // keepalive r1: watching a park confirm\n',
    '        // keepalive r1: watching a park confirm (and a tap held for it)\n'
    '        if (heldTap != null || kept.values.any { it.parkedAtMs != 0L && !it.confirmLogged }) return 250L\n')
sub('        parkTick()  // keepalive r1: confirm / end the parked connection before anything reads the status\n',
    '        keepTick()  // keepalive r1: confirm / end the kept connections before anything reads the status\n')
sub('        if (parked == null && NativeSessionBridge.nativeTakeClipboardPushRequest(activeBinding)) {',
    '        if (activeBinding != 0L && NativeSessionBridge.nativeTakeClipboardPushRequest(activeBinding)) {')
sub('''        val isConnecting = statusValue.startsWith("connecting")
        val isConnected = statusValue.startsWith("connected")
        connectFlowActive = isConnecting || isConnected
''', '''        val isConnecting = statusValue.startsWith("connecting")
        val isConnected = statusValue.startsWith("connected")
        if (activeHandle != 0L && !isConnecting && !isConnected) {
            // the PC on screen is gone (its peer, a runtime failure): that entry ends; the kept ones stay
            val gone = activeHandle
            NativeSessionBridge.nativeEnd(gone, "peer_lost")
            diagnosticsLog.log("keep_end", "handle=$gone host=$directoryHostName reason=peer_lost active=true")
            forgetKept(gone)
            clearActive()
        }
        connectFlowActive = isConnecting || isConnected
''')
sub('''        } else if (onIdleScreen && connectFlowActive && parked == null) {
            // keepalive r1: a parked connection is connected and still belongs on the list.''',
    '''        } else if (onIdleScreen && connectFlowActive && activeHandle != 0L) {
            // keepalive r1: a parked connection is connected and still belongs on the list.''')
sub('        if (parked == null) syncConnectedClientPreferences(isConnected)\n',
    '        if (activeBinding != 0L) syncConnectedClientPreferences(isConnected)\n')
sub('        if (currentScene == UiScene.HOSTS) renderParkBanner(nowMs)\n', '        if (currentScene == UiScene.HOSTS) renderKeepBanner()\n')

# ---- clipboard
sub('        if (parked != null) return  // keepalive r1: nothing travels from a parked session\n',
    '        if (activeBinding == 0L) return  // keepalive r1: nothing travels from a parked session\n')
sub('        if (parked != null) return  // keepalive r1\n', '        if (activeBinding == 0L) return  // keepalive r1\n')

# ---- banner button: all kept PCs end
sub('''        hostsParkDisconnectButton.setOnClickListener {
            diagnosticsLog.log("park_disconnect_tap", "scene=$currentScene")
            endPark("disconnect_tap", announce = false)
            renderStatus()
        }''', '''        hostsParkDisconnectButton.setOnClickListener {
            diagnosticsLog.log("park_disconnect_all_tap", "scene=$currentScene kept=${kept.size}")
            if (activeHandle == 0L) endAllKept("disconnect_all") else endParkedKept("disconnect_all")
            renderStatus()
        }''')

# ---- host card
a = s.index('            // keepalive r1: the parked PC says so on its own card.\n')
b = s.index('            return view\n', a)
s = s[:a] + '''            // keepalive-multi r1 §7: each kept PC says so on its own card -- its state, its time left -- with its
            // own 끊기 (the other kept PCs stay).
            val keptEntry = kept.values.firstOrNull {
                !it.manual && it.hostId == host.hostId && it.accountId == DirectoryClient.sessionAccount() &&
                    it.directoryOrigin == directoryOrigin()
            }
            val native = keptEntry?.let { nativeEntries[it.handle] }
            val keepState = SessionPark.listState(native, keptEntry != null && keptEntry.handle == activeHandle)
            val isKept = keepState == SessionPark.ListState.KEPT || keepState == SessionPark.ListState.CONFIRMING
            view.findViewById<TextView>(R.id.hostCardDetail).text = when (keepState) {
                SessionPark.ListState.KEPT -> getString(R.string.hosts_keep_detail,
                    SessionPark.formatRemaining(native?.remainingMs ?: 0L))
                SessionPark.ListState.CONFIRMING -> getString(R.string.hosts_keep_confirming)
                else -> if (host.online) getString(R.string.hosts_title) else getString(R.string.hosts_offline_detail)
            }
            val state = view.findViewById<TextView>(R.id.hostCardState)
            state.text = when {
                isKept -> getString(R.string.hosts_parked_state)
                host.online -> getString(R.string.hosts_online)
                else -> getString(R.string.hosts_offline)
            }
            state.setTextColor(getColor(if (host.online || isKept) R.color.ui_success else R.color.ui_text_secondary))
            view.alpha = if (host.online || isKept) 1.0f else 0.8f
            val end = view.findViewById<Button>(R.id.hostCardDisconnect)
            end.visibility = if (isKept) View.VISIBLE else View.GONE
            end.setOnClickListener {
                val h = keptEntry?.handle ?: return@setOnClickListener
                diagnosticsLog.log("park_disconnect_tap", "handle=$h host=${host.hostName}")
                entryOps.invalidate(h)
                NativeSessionBridge.nativeEnd(h, "user_disconnect")
                forgetKept(h)
                diagnosticsLog.log("keep_end", "handle=$h host=${host.hostName} reason=user_disconnect")
                hostsStatusText.text = getString(R.string.hosts_park_ended, host.hostName)
                notifyDataSetChanged()
                renderStatus()
            }
''' + s[b:]

left = re.findall(r'\bparked\b(?! connection| PC| session| one|\))|endPark\(|parkTick\(|connectOwner|nativeDisconnect|renderParkBanner|parkConfirmLogged|parkPendingTap', s)
out = s.replace('\n', '\r\n')
open(P, 'wb').write((('﻿' if bom else '') + out).encode('utf-8'))
print('leftovers', left)
