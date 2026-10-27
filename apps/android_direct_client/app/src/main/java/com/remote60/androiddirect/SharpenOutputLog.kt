package com.remote60.androiddirect

/**
 * sh-strength r5 (Codex codex_sh_strength_r4.md N1-c1): WHICH output buffer the view latched.
 *
 * Every swap of a SharpenRenderer carries its own presentation timestamp from here -- unique per swap, far above any
 * decoder timestamp ([BASE]) -- and, recorded BEFORE the swap (dropped again if the swap fails), what that buffer is:
 * a decoded frame drawn (VIDEO), the latched frame drawn again (REDRAW: a strength or layout change, no new decode) or
 * a cleared buffer (CLEAR: moving the output to a new size, no picture), with the layout generation it was drawn with
 * and the decoder frame it shows. The view's latch reads the timestamp of the buffer it actually latched
 * (SurfaceTexture.getTimestamp) and looks it up: never the renderer's latest swap, never "the next callback".
 *
 * Bounded (the oldest entries go); a timestamp not found -- evicted, another renderer's, a failed swap -- says nothing:
 * no frame counted, no layout taken as shown. Pure (no Android types): SharpenOutputLogTest drives it.
 *
 * r6 (Codex codex_sh_strength_r5.md N1-a): the timestamps come from ONE process-wide counter, never per log -- two
 * renderers (A released, B started on the same view) never hand out the same token, so A's late latch looked up in
 * B's log is not found (nothing), never read as B's buffer.
 */
class SharpenOutputLog(private val capacity: Int = 64) {
    enum class Kind { VIDEO, REDRAW, CLEAR }

    data class Output(val kind: Kind, val placementGen: Long, val decoderTs: Long)

    private val outputs = LinkedHashMap<Long, Output>()

    /** The timestamp for the next swap, recorded as `kind` of layout `placementGen` showing decoder frame `decoderTs`. */
    @Synchronized
    fun reserve(kind: Kind, placementGen: Long, decoderTs: Long): Long {
        val ts = BASE + NEXT.incrementAndGet()
        outputs[ts] = Output(kind, placementGen, decoderTs)
        while (outputs.size > capacity) outputs.remove(outputs.keys.first())
        return ts
    }

    /** The swap did not happen: its timestamp names nothing. */
    @Synchronized
    fun drop(ts: Long) {
        outputs.remove(ts)
    }

    @Synchronized
    fun lookup(ts: Long): Output? = outputs[ts]

    companion object {
        const val BASE = 1L shl 62
        /** r6: the process-wide token counter (every log, every renderer, the whole process lifetime). */
        private val NEXT = java.util.concurrent.atomic.AtomicLong(0L)
        fun isOutput(ts: Long) = ts >= BASE
    }
}

/**
 * sh-strength r5: what one latch of the view means, from the latched buffer's own timestamp.
 *  - a decoder timestamp (the direct path): a frame, the layout question does not arise (shownGen null);
 *  - a renderer output found in that renderer's log: VIDEO = a frame (its decoder timestamp is the frame's), REDRAW =
 *    a redraw (not a new frame), CLEAR = nothing (no picture, never a first output or a latency sample) -- VIDEO and
 *    REDRAW show their layout generation;
 *  - a renderer output NOT found (another renderer's, evicted, a failed swap): nothing at all.
 */
object SharpenLatch {
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
}
