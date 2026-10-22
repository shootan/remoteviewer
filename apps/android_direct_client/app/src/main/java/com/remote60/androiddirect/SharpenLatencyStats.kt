package com.remote60.androiddirect

/**
 * sh-filter S4 (r2 H3): decoder handoff -> TextureView latch, per NEW frame, kept per SEGMENT.
 *
 * A sample is one latched NEW frame (LatchClassifier: redraws are not frames) matched to the decoder's
 * handoff of that same frame by its buffer timestamp (the native ring) -- both ends read CLOCK_MONOTONIC.
 * The latch is what this app observes (onSurfaceTextureUpdated), not light leaving the panel.
 *
 * Segments: one per (effective level, path, renderer generation, surface). A producer or level change
 * closes the segment, so samples of different levels or paths are never mixed under one name. Inside a
 * segment, the first WARMUP_NS after it began are counted apart (the switch itself is not the steady
 * state), and a frame handed over BEFORE the segment began is `stale` (a frame of the previous producer
 * latched late) -- neither is a steady sample.
 *
 * Whole-segment distribution: the steady samples are kept exactly (up to CAP; beyond that they are counted
 * in `overflow` and NOT in the percentiles -- the report says so), so the segment's p95 is the p95 of all
 * its frames, not a median of per-window p95s. Percentiles are NEAREST-RANK: the value at rank ceil(q*n)
 * (1-based) of the sorted samples -- for 1..100 ms, p50 = 50 ms and p95 = 95 ms.
 *
 * Other counts: `unmatched` (no handoff found for the timestamp: fell out of the ring), `redraws` (latches
 * of the frame already shown).
 */
class SharpenLatencyStats(private val cap: Int = CAP) {
    data class Key(val level: String, val path: String, val rendererGeneration: Long, val surfaceId: Long) {
        override fun toString(): String = "level=$level path=$path gen=$rendererGeneration surface=$surfaceId"
    }

    data class Summary(
        val count: Int, val warmup: Int, val stale: Int, val unmatched: Int, val redraws: Int, val overflow: Int,
        val p50Us: Long, val p95Us: Long, val p99Us: Long, val maxUs: Long, val durationMs: Long,
    ) {
        fun fields(): String =
            "n=$count p50us=$p50Us p95us=$p95Us p99us=$p99Us maxus=$maxUs warmup=$warmup stale=$stale " +
                "unmatched=$unmatched redraws=$redraws overflow=$overflow ms=$durationMs"
    }

    var key: Key? = null
        private set
    var segmentId = 0L
        private set
    private var startNs = 0L
    private var samplesNs = LongArray(1024)
    private var size = 0
    private var windowFrom = 0
    private var warmup = 0
    private var stale = 0
    private var unmatched = 0
    private var redraws = 0
    private var overflow = 0
    private var windowUnmatched = 0
    private var windowRedraws = 0

    /** Starts a segment for `key` (if it is not already the current one); returns the CLOSED one's summary. */
    fun enter(key: Key, nowNs: Long): Pair<Key, Summary>? {
        if (key == this.key) return null
        val closed = this.key?.let { it to summarize(0, nowNs) }
        this.key = key
        segmentId += 1
        startNs = nowNs
        size = 0
        windowFrom = 0
        warmup = 0; stale = 0; unmatched = 0; redraws = 0; overflow = 0
        windowUnmatched = 0; windowRedraws = 0
        return closed
    }

    fun addMatched(handoffNs: Long, latchNs: Long) {
        if (key == null) return
        if (handoffNs < startNs) { stale += 1; return }
        if (latchNs - startNs < WARMUP_NS) { warmup += 1; return }
        val d = latchNs - handoffNs
        if (d < 0) { unmatched += 1; windowUnmatched += 1; return }
        if (size >= cap) { overflow += 1; return }
        if (size == samplesNs.size) samplesNs = samplesNs.copyOf((samplesNs.size * 2).coerceAtMost(cap))
        samplesNs[size++] = d
    }

    fun addUnmatched() { if (key != null) { unmatched += 1; windowUnmatched += 1 } }
    fun addRedraw() { if (key != null) { redraws += 1; windowRedraws += 1 } }

    /** The steady samples since the last window (a progress line); does not end the segment. */
    fun window(nowNs: Long): Summary {
        val s = summarize(windowFrom, nowNs, windowUnmatched, windowRedraws)
        windowFrom = size
        windowUnmatched = 0
        windowRedraws = 0
        return s
    }

    /** The whole current segment so far. */
    fun segment(nowNs: Long): Summary = summarize(0, nowNs)

    private fun summarize(from: Int, nowNs: Long, um: Int = unmatched, rd: Int = redraws): Summary {
        val n = size - from
        val sorted = samplesNs.copyOfRange(from, size).also { it.sort() }
        val whole = from == 0
        return Summary(
            n, if (whole) warmup else 0, if (whole) stale else 0, um, rd, if (whole) overflow else 0,
            nearestRankUs(sorted, 0.50), nearestRankUs(sorted, 0.95), nearestRankUs(sorted, 0.99),
            if (n == 0) 0L else sorted[n - 1] / 1000, (nowNs - startNs) / 1_000_000,
        )
    }

    companion object {
        const val CAP = 36_000             // ~20 min at 30 fps per segment, exact
        const val WARMUP_NS = 1_000_000_000L

        /** Nearest-rank: the value at 1-based rank ceil(q * n) of an ascending array, in µs (0 when empty). */
        fun nearestRankUs(sortedNs: LongArray, q: Double): Long {
            val n = sortedNs.size
            if (n == 0) return 0L
            val rank = kotlin.math.ceil(q * n - 1e-9).toInt().coerceIn(1, n)  // 1e-9: 0.95*n in binary
            return sortedNs[rank - 1] / 1000
        }
    }
}
