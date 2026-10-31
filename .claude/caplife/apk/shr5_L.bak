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
 */
class SharpenOutputLog(private val capacity: Int = 64) {
    enum class Kind { VIDEO, REDRAW, CLEAR }

    data class Output(val kind: Kind, val placementGen: Long, val decoderTs: Long)

    private val outputs = LinkedHashMap<Long, Output>()
    private var seq = 0L

    /** The timestamp for the next swap, recorded as `kind` of layout `placementGen` showing decoder frame `decoderTs`. */
    @Synchronized
    fun reserve(kind: Kind, placementGen: Long, decoderTs: Long): Long {
        seq += 1
        val ts = BASE + seq
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
}
