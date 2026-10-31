R = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


K = 'main/java/com/remote60/androiddirect/'
edit(K + 'StandbyKeep.kt', [
    ('''    fun isReturn(preparedReturn: Boolean, pathKind: String): Boolean = preparedReturn || pathKind == "standby"
''', '''    fun isReturn(preparedReturn: Boolean, pathKind: String): Boolean = preparedReturn || pathKind == "standby"

    /**
     * What the native lease's activation handshake says when the host answers its StandbyActivate "unknown entry"
     * (standby_entry_client.hpp Lose -> standby_lease.hpp: "standby activation: " + reason). The session's lastError.
     */
    const val ACTIVATION_UNKNOWN_ENTRY = "activation: entry unknown to the host"

    /**
     * Codex codex_avd_guard_src_r1.md N3: a lease's activation can fail AFTER nativeConnectStandby returned its
     * generation (the controller starts its worker first; the handshake ends later). When the host said the entry is
     * unknown -- the lease could not be used -- the tap becomes the ordinary connect, once, as a gen-0 failure does.
     * Only for an activation (pathKind "standby"), only before it was established, not after the establish limit ran
     * out, and only once (the fallback is an ordinary connect: it never falls back again). Superseded, cancelled,
     * another device's takeover, a stale tap, an established session's peer_lost: none of them. The caller has already
     * checked the tap is still current (its op and epoch).
     */
    fun fallbackAfterEstablishFailure(pathKind: String, establishTimedOut: Boolean, lastError: String): Boolean =
        pathKind == "standby" && !establishTimedOut && lastError.contains(ACTIVATION_UNKNOWN_ENTRY)
'''),
])

edit(K + 'MainActivity.kt', [
    ('''    private data class PendingCommit(
        val handle: Long, val op: Long, val gen: Long, val epoch: Long, val startedAtMs: Long,
        val pathKind: String, val manual: Boolean, val preparedReturn: Boolean = false,
    )''', '''    private data class PendingCommit(
        val handle: Long, val op: Long, val gen: Long, val epoch: Long, val startedAtMs: Long,
        val pathKind: String, val manual: Boolean, val preparedReturn: Boolean = false,
        // N3 (codex_avd_guard_src_r1): an activation's PC and lease -- the same tap's ordinary fallback if the
        // activation fails before it is established (StandbyKeep.fallbackAfterEstablishFailure)
        val fallbackHost: DirectoryClient.Host? = null, val fallbackKey: RecentHosts.Key? = null,
    )'''),
    ('''                    pendingCommit = PendingCommit(handle, op, gen, epoch, t0, "standby", manual = false, preparedReturn = true)''',
     '''                    pendingCommit = PendingCommit(handle, op, gen, epoch, t0, "standby", manual = false, preparedReturn = true,
                        fallbackHost = host, fallbackKey = rk)'''),
    ('''            forgetKept(p.handle)
            setDirectoryBusy(false)
            directoryConnectingName = ""
            if (p.manual) {
                diagnosticsLog.log("connect_failed", why)''', '''            forgetKept(p.handle)
            setDirectoryBusy(false)
            directoryConnectingName = ""
            // N3: the activation failed after its generation came back -- the lease could not be used (the host does
            // not know the entry): this same, still current tap (checked above) becomes the ordinary connect, once
            val fh = p.fallbackHost
            val fk = p.fallbackKey
            if (fh != null && fk != null && StandbyKeep.fallbackAfterEstablishFailure(p.pathKind, status == 0L, lastError)) {
                diagnosticsLog.log("standby_activate_failed",
                    "handle=${p.handle} op=${p.op} gen=${p.gen} why=$why mine=true step=establish")
                if (connectingHandle == p.handle) connectingHandle = 0L
                releaseStandby(fk, "activate_failed")
                connectToDirectoryHost(fh, TapIntent.RETRY, preparedReturn = true)  // r3 R3: still the return
                return
            }
            if (p.manual) {
                diagnosticsLog.log("connect_failed", why)'''),
])

T = 'test/java/com/remote60/androiddirect/StandbyKeepTest.kt'
p = R + T
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')
i = s.rindex('}')  # the class's closing brace
add = '''
    // N3 (codex_avd_guard_src_r1): the activation's failure that comes after its generation -- the same tap's ordinary
    // connect, once, only for an unknown entry
    @Test
    fun anActivationThatFailsAfterItsGenerationFallsBackOnlyForAnUnknownEntry() {
        val unknown = "standby activation: " + StandbyKeep.ACTIVATION_UNKNOWN_ENTRY
        assertTrue(StandbyKeep.fallbackAfterEstablishFailure("standby", establishTimedOut = false, lastError = unknown))
        // not an activation: an ordinary / raw / relay connect (and so the fallback itself) never falls back again
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("relay", false, unknown))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("private", false, unknown))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("direct", false, unknown))
        // the establish limit ran out: not this failure
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", true, unknown))
        // superseded (a stale generation), cancelled, another device holding the PC, nothing said: no fallback
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false,
            "standby activation: activation refused (superseded)"))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, "standby activation cancelled"))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, "standby activation: host busy"))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, ""))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, "not_established"))
    }
'''
s = s[:i] + add + s[i:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok', T)
