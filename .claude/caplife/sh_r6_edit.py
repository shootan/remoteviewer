base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/'

def edit(rel, pairs):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('SharpenOutputLog.kt', [
    (''' * Bounded (the oldest entries go); a timestamp not found -- evicted, another renderer's, a failed swap -- says nothing:
 * no frame counted, no layout taken as shown. Pure (no Android types): SharpenOutputLogTest drives it.
 */''', ''' * Bounded (the oldest entries go); a timestamp not found -- evicted, another renderer's, a failed swap -- says nothing:
 * no frame counted, no layout taken as shown. Pure (no Android types): SharpenOutputLogTest drives it.
 *
 * r6 (Codex codex_sh_strength_r5.md N1-a): the timestamps come from ONE process-wide counter, never per log -- two
 * renderers (A released, B started on the same view) never hand out the same token, so A's late latch looked up in
 * B's log is not found (nothing), never read as B's buffer.
 */'''),
    ('''    private val outputs = LinkedHashMap<Long, Output>()
    private var seq = 0L
''', '''    private val outputs = LinkedHashMap<Long, Output>()
'''),
    ('''        seq += 1
        val ts = BASE + seq
''', '''        val ts = BASE + NEXT.incrementAndGet()
'''),
    ('''    companion object {
        const val BASE = 1L shl 62
        fun isOutput(ts: Long) = ts >= BASE
    }
}''', '''    companion object {
        const val BASE = 1L shl 62
        /** r6: the process-wide token counter (every log, every renderer, the whole process lifetime). */
        private val NEXT = java.util.concurrent.atomic.AtomicLong(0L)
        fun isOutput(ts: Long) = ts >= BASE
    }
}'''),
    ('''object SharpenLatch {
    data class Decision(val frame: Boolean, val redraw: Boolean, val frameTs: Long, val shownGen: Long?)

    private val NOTHING = Decision(false, false, 0L, null)

    fun decide(timestamp: Long, log: SharpenOutputLog?): Decision {
        if (!SharpenOutputLog.isOutput(timestamp)) return Decision(true, false, timestamp, null)
        val o = log?.lookup(timestamp) ?: return NOTHING
        return when (o.kind) {
            SharpenOutputLog.Kind.CLEAR -> NOTHING
            SharpenOutputLog.Kind.REDRAW -> Decision(false, true, o.decoderTs, o.placementGen)
            SharpenOutputLog.Kind.VIDEO -> Decision(true, false, o.decoderTs, o.placementGen)
        }
    }
}''', '''object SharpenLatch {
    /** r6: no layout is shown -- the view latched a cleared or an unknown renderer buffer (no picture of any layout). */
    const val NONE_SHOWN = -1L

    /** `picture`: a buffer showing a decoded frame (VIDEO, or a REDRAW of it); `shownGen`: null = not a renderer output
     *  (keep what was shown), NONE_SHOWN = no layout on screen now, else the layout shown. */
    data class Decision(val picture: Boolean, val frameTs: Long, val shownGen: Long?)

    private val NO_PICTURE = Decision(false, 0L, NONE_SHOWN)

    fun decide(timestamp: Long, log: SharpenOutputLog?): Decision {
        if (!SharpenOutputLog.isOutput(timestamp)) return Decision(true, timestamp, null)
        val o = log?.lookup(timestamp) ?: return NO_PICTURE
        return when (o.kind) {
            SharpenOutputLog.Kind.CLEAR -> NO_PICTURE
            SharpenOutputLog.Kind.REDRAW, SharpenOutputLog.Kind.VIDEO -> Decision(true, o.decoderTs, o.placementGen)
        }
    }

    /**
     * r6 (N1-b): what one latch is, as the Activity uses it. A new DECODE and a frame's first time ON SCREEN are apart:
     * a decoder frame is shown for the first time by whichever buffer of it the view latches first -- its VIDEO, or a
     * REDRAW of it when the VIDEO was never latched -- and only then counts (first output, the selection's frame, its
     * handoff->latch delay); every later latch of the same frame (VIDEO or REDRAW) is a redraw; a CLEAR or an unknown
     * buffer is nothing. `firstShow` / `redraw` / neither.
     */
    data class Latched(val firstShow: Boolean, val redraw: Boolean, val frameTs: Long, val shownGen: Long?)

    fun qualify(timestamp: Long, log: SharpenOutputLog?, classifier: LatchClassifier, surfaceId: Long): Latched {
        val d = decide(timestamp, log)
        if (!d.picture) return Latched(false, false, 0L, d.shownGen)
        val first = classifier.isNewFrame(surfaceId, d.frameTs)
        return Latched(first, !first, d.frameTs, d.shownGen)
    }
}'''),
])

edit('MainActivity.kt', [
    ('''        val latched = SharpenLatch.decide(timestamp, sharpenRenderer?.outputs)
        latched.shownGen?.let { sharpenShownPlacementGen = it }
        if (!latched.frame && !latched.redraw) return
        val frameTs = latched.frameTs  // the decoder frame this buffer shows
        // sh-filter r2 (H3): the decoder timestamp names the frame. A strength change re-draws the frame already
        // shown: that latch is not a new displayed frame.
        val newFrame = latched.frame && latchClassifier.isNewFrame(surfaceId, frameTs)
        enterSharpenLatencySegment(latchNs)
        if (!newFrame) {
            sharpenStats.addRedraw()
            return
        }''', '''        // r6 (N1-b): a decoder frame's FIRST time on screen (its VIDEO, or a REDRAW of it when the VIDEO was never
        // latched) counts once; a cleared or unknown buffer shows no layout (the fence holds) and counts as nothing.
        val latched = SharpenLatch.qualify(timestamp, sharpenRenderer?.outputs, latchClassifier, surfaceId)
        latched.shownGen?.let { sharpenShownPlacementGen = it }
        if (!latched.firstShow && !latched.redraw) return
        val frameTs = latched.frameTs  // the decoder frame this buffer shows
        enterSharpenLatencySegment(latchNs)
        if (!latched.firstShow) {
            sharpenStats.addRedraw()
            return
        }'''),
])
print('ok')
