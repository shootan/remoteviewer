package com.remote60.androiddirect

/**
 * sh-filter r1 (S4): decoder handoff -> TextureView latch, per frame, for one diagnostic window.
 *
 * Each sample is one latched frame matched to the decoder's handoff of that same frame by its buffer
 * timestamp (the native ring, NativeSessionBridge.nativeHandoffNsForTimestamp) -- both ends read
 * CLOCK_MONOTONIC. The latch is what this app observes (onSurfaceTextureUpdated), not light leaving
 * the panel. Frames whose handoff fell out of the ring, or redraws, are not samples; `unmatched`
 * counts the former so a window with few matches is visible as such.
 *
 * Bounded: at most MAX_SAMPLES per window are kept (the rest are counted, not stored).
 */
class SharpenLatencyStats {
    data class Summary(val count: Int, val unmatched: Int, val p50Us: Long, val p95Us: Long, val maxUs: Long)

    private val samplesNs = LongArray(MAX_SAMPLES)
    private var size = 0
    private var overflow = 0
    var unmatched = 0

    fun add(deltaNs: Long) {
        if (deltaNs < 0 || deltaNs > 10_000_000_000L) { unmatched += 1; return }
        if (size < MAX_SAMPLES) samplesNs[size++] = deltaNs else overflow += 1
    }

    /** The window's summary; the window is then empty. Percentiles are nearest-rank. */
    fun drain(): Summary {
        val n = size
        val sorted = samplesNs.copyOf(n).also { it.sort() }
        fun rank(p: Double): Long = if (n == 0) 0L else sorted[((p * n).toInt()).coerceIn(0, n - 1)] / 1000
        val s = Summary(n + overflow, unmatched, rank(0.50), rank(0.95), if (n == 0) 0L else sorted[n - 1] / 1000)
        size = 0
        overflow = 0
        unmatched = 0
        return s
    }

    companion object { const val MAX_SAMPLES = 2048 }
}
