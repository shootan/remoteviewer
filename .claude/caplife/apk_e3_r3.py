A = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/'
K = A + 'main/java/com/remote60/androiddirect/'


def edit(p, reps):
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (p, a[:80], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', p.rsplit('/', 1)[-1])


edit(K + 'StandbyKeep.kt', [
    ("""    fun clipboardOnStart(fromStandby: Boolean, phoneText: String?): ClipboardStart = when {""",
     """    /**
     * r3 R3 (Codex codex_ka_e_src_r2.md): a session is "back from standby" when the user went back to a PC on standby
     * -- whatever path the connection took (its activation, or that same tap's ordinary fallback after a failed one).
     * A separate cold connect is not.
     */
    fun isReturn(preparedReturn: Boolean, pathKind: String): Boolean = preparedReturn || pathKind == "standby"

    fun clipboardOnStart(fromStandby: Boolean, phoneText: String?): ClipboardStart = when {"""),
])
edit(K + 'MainActivity.kt', [
    ("""        val returning = p.preparedReturn || p.pathKind == "standby\"""",
     """        val returning = StandbyKeep.isReturn(p.preparedReturn, p.pathKind)"""),
])
edit(A + 'test/java/com/remote60/androiddirect/StandbyKeepTest.kt', [
    ("""    @Test
    fun punchesAreReadApartFromStrays() {""", """    @Test
    fun aReturnsFallbackKeepsItsClipboardBaseline() {
        // r3 R3: the activation failed and the same tap connected the ordinary way (relay / private): still a return --
        // the phone's text is this binding's baseline (not sent), unreadable -> held; a separate cold connect is fresh
        assertTrue(StandbyKeep.isReturn(preparedReturn = true, pathKind = "relay"))
        assertTrue(StandbyKeep.isReturn(preparedReturn = false, pathKind = "standby"))
        assertFalse(StandbyKeep.isReturn(preparedReturn = false, pathKind = "relay"))
        val fallback = StandbyKeep.isReturn(true, "private")
        assertEquals(StandbyKeep.ClipboardStart.Seed("copied on B"), StandbyKeep.clipboardOnStart(fallback, "copied on B"))
        assertEquals(StandbyKeep.ClipboardStart.Hold, StandbyKeep.clipboardOnStart(fallback, null))
        assertEquals(StandbyKeep.ClipboardStart.Fresh,
            StandbyKeep.clipboardOnStart(StandbyKeep.isReturn(false, "private"), "copied on B"))
    }

    @Test
    fun punchesAreReadApartFromStrays() {"""),
])
