import sys, re
root = sys.argv[1]
P = root + '/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt'
s = open(P, 'rb').read().decode('utf-8').replace('\r\n', '\n')

def sub(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('count %d: %r' % (s.count(old), old[:80]))
    s = s.replace(old, new)

sub('''    /** r4 S1: the manual connect, adopted through the connect gate as operation [op]; the generation it made (0 = none). */
    external fun nativeConnect(host: String, videoPort: Int, controlPort: Int, op: Long): Long

    /** r4 S2-a: a new connect operation, numbered by the native gate for the whole process (and current there). */
    external fun nativeConnectOpNext(): Long

    /** r4 S1: closes the session only if [op] adopted generation [gen] and it is still the one running (one step). */
    external fun nativeCloseIfOwn(op: Long, gen: Long): Boolean
    external fun nativeDisconnect()

    // keepalive r1 (SessionPark.kt): park a connected session while the PC list shows, resume it.
    /** Stops input / clipboard / previews, releases what the host holds, then pauses and fences. */
    external fun nativeBeginPark(): Boolean
    /** Only from Parked: the session takes requests again. */
    external fun nativeResumePark(): Boolean
    /** Drops a park without resuming (a disconnect follows). */
    external fun nativeEndPark()
    /** SessionPark.NativePhase codes. */
    external fun nativeParkPhase(): Int
    external fun nativeParkReleasedCount(): Int
    external fun nativeParkDroppedActionCount(): Int
    /** r2 K5: the host stops sending when paused (else "PC 목록" is not offered: 연결 해제 only). */
    external fun nativeHostHonorsStreamPause(): Boolean
    /** Bumps on every session Connect: the identity of the authenticated connection. */
    external fun nativeConnectionGeneration(): Long
    /** "gen= connects= hellos= observes= punches=" for the log. */
    external fun nativeSessionCounters(): String
    /** The phone's clipboard when a parked session resumes: counted as already sent. */
    external fun nativeSeedClipboardBaseline(text: String)
''', '''    // --- keepalive-multi r1: the kept PCs (native session table, session_table.hpp) ----------------
    //
    // Every PC connection is an entry with a process-unique [handle]. Calls about one connection take its handle;
    // calls that act on the picture / input / clipboard / targets take the [binding] the app got from
    // nativeActivate when that PC was put on screen -- captured when the action was taken, so an action of B can
    // never reach A after a switch (a stale binding does nothing). The decoder and Surface stay single.

    /** [kind (0 reuse, 1 new, 2 refused), handle, victim]: a tap on [key] (origin|account|hostId) in lifecycle [epoch]. */
    external fun nativeReserve(key: String, manual: Boolean, epoch: Long): LongArray
    /** The new connection is up: the handles its reservation evicted (LRU), closed now. */
    external fun nativeCommit(handle: Long): LongArray
    /** The connect failed or went stale: that entry only. */
    external fun nativeFail(handle: Long, reason: String)
    /** On screen: its binding (0 = refused -- another PC on screen / still confirming, or its resume failed: ended). */
    external fun nativeActivate(handle: Long): Long
    /** One PC ends; the others stay. */
    external fun nativeEnd(handle: Long, reason: String): Boolean
    /** Everything ends (sign-out, account / directory, network, disconnect all): the new lifecycle epoch. */
    external fun nativeEndAll(reason: String): Long
    external fun nativeEpoch(): Long
    external fun nativeSetKeepalivePolicy(maxHosts: Int, parkTtlMs: Long)
    /** {"epoch","maxHosts","parkTtlMs","entries":[...],"ended":[...],"closing","binding"} -- no token, no address. */
    external fun nativeEntriesJson(): String

    /** r4 S1: the manual connect of entry [handle], adopted through its gate as operation [op]; the generation (0 = none). */
    external fun nativeConnect(handle: Long, host: String, videoPort: Int, controlPort: Int, op: Long): Long

    /** r4 S2-a: a new connect operation for entry [handle] (numbers process-unique, the cancel that entry's own). */
    external fun nativeConnectOpNext(handle: Long): Long

    /** r4 S1: closes that entry's session only if [op] adopted generation [gen] and it is still the one running. */
    external fun nativeCloseIfOwn(handle: Long, op: Long, gen: Long): Boolean

    // keepalive r1 (SessionPark.kt): park the PC on screen while the PC list shows; nativeActivate resumes it.
    /** Its binding goes (no frame of it reaches the decoder), then inhibit / release / pause / fence; false = not parkable. */
    external fun nativeBeginPark(handle: Long): Boolean
    /** Drops a park without resuming (an end follows). */
    external fun nativeEndPark(handle: Long)
    /** SessionPark.NativePhase codes. */
    external fun nativeParkPhase(handle: Long): Int
    external fun nativeParkReleasedCount(handle: Long): Int
    external fun nativeParkDroppedActionCount(handle: Long): Int
    /** r2 K5: the host stops sending when paused (else "PC 목록" is not offered: 연결 해제 only). */
    external fun nativeHostHonorsStreamPause(handle: Long): Boolean
    /** Bumps on every session Connect: the identity of the authenticated connection. */
    external fun nativeConnectionGeneration(handle: Long): Long
    /** "handle= gen= connects= hellos= observes= punches=" for the log. */
    external fun nativeSessionCounters(handle: Long): String
    /** The phone's clipboard when a parked session resumes: counted as already sent. */
    external fun nativeSeedClipboardBaseline(binding: Long, text: String)
''')

sub('''        observeToken: String,
        op: Long,
    ): String''', '''        observeToken: String,
        op: Long,
    ): String''')
sub('''    external fun nativeDirectoryObserve(
        directoryHost: String,''', '''    external fun nativeDirectoryObserve(
        handle: Long,
        directoryHost: String,''')
sub('''    external fun nativeDirectoryConnectAny(
        candidates: Array<String>,''', '''    external fun nativeDirectoryConnectAny(
        handle: Long,
        candidates: Array<String>,''')
sub('external fun nativeDirectoryChosenCandidate(): String', 'external fun nativeDirectoryChosenCandidate(handle: Long): String')
sub('external fun nativeDirectoryLastError(): String', 'external fun nativeDirectoryLastError(handle: Long): String')
sub('external fun nativeGetStatus(): String', 'external fun nativeGetStatus(handle: Long): String')
sub('external fun nativeGetLastError(): String', 'external fun nativeGetLastError(handle: Long): String')
sub('external fun nativePrepareVideoSwitch(selectionGeneration: Long)', 'external fun nativePrepareVideoSwitch(binding: Long, selectionGeneration: Long): Boolean')
sub('external fun nativeAbortVideoSwitch()', 'external fun nativeAbortVideoSwitch(binding: Long)')
sub('external fun nativeGetReadySelectionGeneration(): Long', 'external fun nativeGetReadySelectionGeneration(binding: Long): Long')
for n in ['nativeRequestWindowList', 'nativeSelectDesktopMode', 'nativeIsHostScreenLocked', 'nativeTakeClipboardPushRequest', 'nativeHostSupportsClipboard']:
    sub('external fun %s(): Boolean' % n, 'external fun %s(binding: Long): Boolean' % n)
sub('external fun nativeSelectWindow(windowId: Long): Boolean', 'external fun nativeSelectWindow(binding: Long, windowId: Long): Boolean')
sub('external fun nativeSelectMonitor(monitorId: Int): Boolean', 'external fun nativeSelectMonitor(binding: Long, monitorId: Int): Boolean')
sub('external fun nativeSelectMonitorAt(monitorId: Int, listRevision: Long): Boolean', 'external fun nativeSelectMonitorAt(binding: Long, monitorId: Int, listRevision: Long): Boolean')
sub('external fun nativeRequestRuntimeConfig(bitrateBps: Int, fps: Int): Boolean', 'external fun nativeRequestRuntimeConfig(binding: Long, bitrateBps: Int, fps: Int): Boolean')
sub('external fun nativeRequestDesktopCaptureBackend(backend: Int): Boolean', 'external fun nativeRequestDesktopCaptureBackend(binding: Long, backend: Int): Boolean')
sub('external fun nativeRequestStreamActive(active: Boolean): Boolean', 'external fun nativeRequestStreamActive(binding: Long, active: Boolean): Boolean')
sub('''    external fun nativeQueueInputEvent(
        kind: Int,''', '''    external fun nativeQueueInputEvent(
        binding: Long,
        kind: Int,''')
sub('external fun nativeQueueInputText(text: String): Boolean', 'external fun nativeQueueInputText(binding: Long, text: String): Boolean')
sub('external fun nativeQueueClipboardText(text: String): Boolean', 'external fun nativeQueueClipboardText(binding: Long, text: String): Boolean')
sub('external fun nativeTakeIncomingClipboardText(): String?', 'external fun nativeTakeIncomingClipboardText(binding: Long): String?')
sub('external fun nativeGetWindowPanelJson(): String', 'external fun nativeGetWindowPanelJson(binding: Long): String')
sub('external fun nativeGetWindowThumbnail(windowId: Long): ByteArray?', 'external fun nativeGetWindowThumbnail(binding: Long, windowId: Long): ByteArray?')
sub('''    external fun nativeMacroStartPlayback(
        timingJitterMs: Int,''', '''    external fun nativeMacroStartPlayback(
        binding: Long,
        timingJitterMs: Int,''')
sub('external fun nativeMacroPump(): Int', 'external fun nativeMacroPump(binding: Long): Int')
sub('external fun nativeGetSessionBytesReceived(): Long', 'external fun nativeGetSessionBytesReceived(binding: Long): Long')
sub('external fun nativeResetVideoStream()', 'external fun nativeResetVideoStream(binding: Long)')
sub('    /** Whether the connected host advertised clipboard support. */', '    /** Whether the host on screen advertised clipboard support. */')
sub('    external fun nativeSetClipboardSyncEnabled(enabled: Boolean)\n', '    /** The user\'s switch: every kept connection and each new one. */\n    external fun nativeSetClipboardSyncEnabled(enabled: Boolean)\n')
open(P, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
