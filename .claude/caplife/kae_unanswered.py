base = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/'


def patch(rel, reps):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (rel, a[:70], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


patch('main/java/com/remote60/androiddirect/StandbyKeep.kt', [
("""    /**
     * "PC 목록" with a PC on screen. [fromStandby] = its session came from a lease's activation; [directory] = a
     * directory-brokered connection (not raw IP); [supported] = the directory said standby (read-only /api/hosts);
     * [prelogin] = the PC is at its logon screen (a pre-login worker); [knownUnsupported] = this PC's lease already
     * found an older host; [enabled] = model A is on in this build.
     */""", """    /**
     * "PC 목록" with a PC on screen. [fromStandby] = its session came from a lease's activation; [directory] = a
     * directory-brokered connection (not raw IP); [supported] = the directory said standby (read-only /api/hosts);
     * [prelogin] = the PC is at its logon screen (a pre-login worker); [knownUnsupported] = known for certain not to
     * stand by (the app passes false: a lease whose Hellos went unanswered may have met an older host OR a path that
     * failed for now -- Codex: an unconfirmed / passing failure is never taken for an older version, so "PC 목록" tries
     * again); [enabled] = model A is on in this build.
     */"""),
("""        /** An older host / a pre-login worker answered no standby: not tried again this sign-in. */
        @Synchronized fun markUnsupported(key: RecentHosts.Key) { unsupported.add(key) }
        @Synchronized fun isUnsupported(key: RecentHosts.Key): Boolean = key in unsupported
""", """        /**
         * A lease's Hellos went unanswered (an older host -- or a path that failed for now: not told apart). Only the
         * app's OWN preparation holds off for [UNANSWERED_HOLD_MS]; a user's "PC 목록" tries again.
         */
        @Synchronized fun markUnsupported(key: RecentHosts.Key, nowMs: Long) { unsupported[key] = nowMs + UNANSWERED_HOLD_MS }
        @Synchronized fun isUnsupported(key: RecentHosts.Key, nowMs: Long): Boolean = nowMs < (unsupported[key] ?: 0L)
"""),
("        private val unsupported = HashSet<RecentHosts.Key>()\n",
 "        private val unsupported = HashMap<RecentHosts.Key, Long>()  // until when the app's own preparation holds off\n"),
("""    /** The native runner's key of a PC. */""", """    /** How long the app's own preparation leaves a PC whose lease went unanswered (a user's PC 목록 never waits). */
    const val UNANSWERED_HOLD_MS = 10L * 60L * 1000L

    /** The native runner's key of a PC. */"""),
])
patch('main/java/com/remote60/androiddirect/MainActivity.kt', [
("            knownUnsupported = rk != null && standbyBook.isUnsupported(rk), enabled = true)",
 "            knownUnsupported = false, enabled = true)  // an unanswered lease is never taken for an older host"),
("        if (standbyBook.has(key) || standbyBook.isUnsupported(key) || !ensureStandbyInit()) return",
 "        if (standbyBook.has(key) || !ensureStandbyInit()) return"),
("""                standbyBook.markUnsupported(key)
                releaseStandby(key, "unsupported")
                hostsStatusText.text = getString(R.string.hosts_standby_unsupported_host,""",
 """                standbyBook.markUnsupported(key, now)
                releaseStandby(key, "unanswered")
                hostsStatusText.text = getString(R.string.hosts_standby_unanswered,"""),
("            if (listed[hostId]?.online != true || keepSuppression.isSuppressed(key) || standbyBook.isUnsupported(key)) continue",
 "            if (listed[hostId]?.online != true || keepSuppression.isSuppressed(key) ||\n                standbyBook.isUnsupported(key, SystemClock.elapsedRealtime())) continue"),
])
patch('main/res/values/strings.xml', [
("""    <string name="hosts_standby_unsupported_host">%1$s 은(는) 상시 유지를 지원하지 않습니다 — 다시 누르면 새로 연결합니다</string>""",
 """    <string name="hosts_standby_unanswered">%1$s 에서 대기 연결 응답이 없습니다 (상시 유지를 지원하지 않는 PC 이거나 연결 경로 문제) — 다시 누르면 새로 연결합니다</string>"""),
])
patch('test/java/com/remote60/androiddirect/StandbyKeepTest.kt', [
("""        b.markUnsupported(k("C"))
        assertEquals(setOf(k("A"), k("B")), b.endAll().toSet())
        assertFalse(b.isCurrent(k("A"), a))
        assertTrue(b.keys().isEmpty())
        assertFalse(b.isUnsupported(k("C")))""", """        b.markUnsupported(k("C"), 1_000L)
        assertEquals(setOf(k("A"), k("B")), b.endAll().toSet())
        assertFalse(b.isCurrent(k("A"), a))
        assertTrue(b.keys().isEmpty())
        assertFalse(b.isUnsupported(k("C"), 1_000L))"""),
("""    @Test
    fun releasingTheStagingLeaseFreesTheSlot() {""", """    @Test
    fun anUnansweredLeaseHoldsOffOnlyTheAppsOwnPreparationForAWhile() {
        // Codex: an unanswered lease may be an older host or a path that failed for now -- not taken for an older version
        val b = StandbyKeep.Book()
        b.markUnsupported(k("A"), 1_000L)
        assertTrue(b.isUnsupported(k("A"), 1_000L + StandbyKeep.UNANSWERED_HOLD_MS - 1))
        assertFalse(b.isUnsupported(k("A"), 1_000L + StandbyKeep.UNANSWERED_HOLD_MS))
        assertFalse(b.isUnsupported(k("B"), 1_000L))
    }

    @Test
    fun releasingTheStagingLeaseFreesTheSlot() {"""),
])
print('ok')
