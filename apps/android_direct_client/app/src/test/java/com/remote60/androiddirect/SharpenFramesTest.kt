package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * sh-filter r2 (Codex H2/H3): a frame is told by its timestamp. Redraws are not frames however the consumer
 * coalesces them; the output gate is confirmed by a latch of THIS renderer/surface/codec whenever it comes;
 * latency is kept per segment and its percentiles are nearest-rank over the whole segment.
 */
class SharpenFramesTest {

    private fun info(handoffNs: Long, gen: Long = 2, epoch: Long = 5) = FrameInfo(handoffNs, gen, epoch, 1920, 1080)

    // ---- latch classification ----------------------------------------------------------------------

    @Test
    fun threeRedrawsCoalescedIntoOneLatch_countAsNoNewFrame_andTheNextFrameCounts() {
        val c = LatchClassifier()
        assertTrue(c.isNewFrame(1, 100))
        // three strength changes swapped the same frame; the view latched only once
        assertFalse(c.isNewFrame(1, 100))
        // the next real frame is not swallowed by the redraws that never got a latch of their own
        assertTrue(c.isNewFrame(1, 133))
    }

    @Test
    fun redrawsAndNewFramesInterleaved() {
        val c = LatchClassifier()
        val seq = listOf(100L, 100L, 133L, 166L, 166L, 166L, 200L)
        assertEquals(listOf(true, false, true, true, false, false, true), seq.map { c.isNewFrame(1, it) })
    }

    @Test
    fun theSameTimestampOnANewSurface_isANewLatch_thereNotARedraw() {
        val c = LatchClassifier()
        assertTrue(c.isNewFrame(1, 100))
        assertTrue(c.isNewFrame(2, 100))
        assertFalse(c.isNewFrame(2, 100))
    }

    @Test
    fun mutantOfR1_countingSwapsNotFrames_wouldDropTheRealFrame() {
        // r1: each redraw swap made the NEXT latch be skipped. Three redraw swaps folded into one latch left
        // two "pending", so the next two real frames were dropped. Shown here as the contrast case.
        var pending = 0
        val latches = listOf(100L to 3, 133L to 0, 166L to 0)  // (latched timestamp, redraw swaps before it)
        var r1Counted = 0
        var first = true
        for ((_, swaps) in latches) {
            pending += swaps
            if (first) { first = false; r1Counted += 1; continue }
            if (pending > 0) pending -= 1 else r1Counted += 1
        }
        val c = LatchClassifier()
        val now = latches.count { c.isNewFrame(1, it.first) }
        assertEquals("r1 rule loses real frames", 1, r1Counted)
        assertEquals("the timestamp rule counts all three", 3, now)
    }

    // ---- output gate -------------------------------------------------------------------------------

    @Test
    fun aFirstLatchThatCameBeforeAnyoneLooked_confirmsOnce() {
        val g = SharpenOutputGate()
        g.arm(rendererGeneration = 7, surfaceId = 1, attachNs = 1_000, codecEpochAtAttach = 4, nowMs = 0)
        assertTrue("the latch itself confirms (no baseline taken later)", g.onLatch(7, 1, true, info(1_500)))
        assertTrue(g.confirmed)
        assertFalse("only the first confirms", g.onLatch(7, 1, true, info(2_000)))
        assertFalse(g.timedOut(10_000, 3_000))
    }

    @Test
    fun latchesThatAreNotOfThisRendererSurfaceOrCodec_doNotConfirm() {
        val g = SharpenOutputGate()
        g.arm(7, 1, attachNs = 1_000, codecEpochAtAttach = 4, nowMs = 0)
        assertFalse("an older renderer generation", g.onLatch(6, 1, true, info(1_500)))
        assertFalse("another surface", g.onLatch(7, 2, true, info(1_500)))
        assertFalse("handed over before the attach (an old frame drawn late)", g.onLatch(7, 1, true, info(900)))
        assertFalse("from the codec that was running before the attach", g.onLatch(7, 1, true, info(1_500, epoch = 4)))
        assertFalse("a redraw", g.onLatch(7, 1, false, info(1_500)))
        assertFalse("no handoff known", g.onLatch(7, 1, true, null))
        assertFalse(g.confirmed)
        assertTrue("so it times out", g.timedOut(3_001, 3_000))
    }

    @Test
    fun aSelectionIsConfirmedOnlyByALatchOfItsOwnStreamGeneration() {
        val g = SharpenOutputGate()
        g.arm(7, 1, 1_000, 4, 0)
        g.onLatch(7, 1, true, info(1_500, gen = 2))
        assertTrue(g.confirmedFor(2))
        assertFalse("the new target is not shown yet", g.confirmedFor(3))
        g.onLatch(7, 1, true, info(2_500, gen = 3))
        assertTrue(g.confirmedFor(3))
    }

    @Test
    fun aFirstLatchWithoutItsEntry_isCountedBeforeTheConfirmingOne() {
        // r3 R2: the handoff entry published AFTER the buffer (r2's order) -> the first latch finds none.
        val g = SharpenOutputGate()
        g.arm(7, 1, 1_000, 4, 0)
        assertFalse(g.onLatch(7, 1, true, null))
        assertTrue(g.onLatch(7, 1, true, info(1_600)))
        assertEquals(1, g.latchesBeforeConfirm)
        val h = SharpenOutputGate()
        h.arm(7, 1, 1_000, 4, 0)
        assertTrue("entry first: the first latch confirms", h.onLatch(7, 1, true, info(1_500)))
        assertEquals(0, h.latchesBeforeConfirm)
    }

    @Test
    fun cancelledGate_confirmsNothing() {
        val g = SharpenOutputGate()
        g.arm(7, 1, 1_000, 4, 0)
        g.cancel()
        assertFalse(g.onLatch(7, 1, true, info(1_500)))
        assertFalse(g.timedOut(10_000, 3_000))
    }

    @Test
    fun frameInfo_fromTheNativeArray() {
        assertNull(FrameInfo.of(null))
        assertEquals(FrameInfo(1, 2, 3, 1920, 1080), FrameInfo.of(longArrayOf(1, 2, 3, 1920, 1080)))
    }

    // ---- latency segments --------------------------------------------------------------------------

    private val ms = 1_000_000L

    @Test
    fun nearestRank_is_ceilQn() {
        val sorted = LongArray(100) { (it + 1) * ms }  // 1..100 ms
        assertEquals(50_000L, SharpenLatencyStats.nearestRankUs(sorted, 0.50))
        assertEquals(95_000L, SharpenLatencyStats.nearestRankUs(sorted, 0.95))
        assertEquals(99_000L, SharpenLatencyStats.nearestRankUs(sorted, 0.99))
        assertEquals(1_000L, SharpenLatencyStats.nearestRankUs(longArrayOf(1 * ms), 0.95))
        assertEquals(0L, SharpenLatencyStats.nearestRankUs(LongArray(0), 0.5))
        // 20 samples: rank ceil(0.95*20)=19
        assertEquals(19_000L, SharpenLatencyStats.nearestRankUs(LongArray(20) { (it + 1) * ms }, 0.95))
    }

    @Test
    fun segment_isTheWholeDistribution_notAMedianOfWindows() {
        val s = SharpenLatencyStats()
        val k = SharpenLatencyStats.Key("weak", "sharpen", 3, 1)
        val t0 = 10_000 * ms
        s.enter(k, t0)
        // warm-up second: not steady
        s.addMatched(t0 + 100 * ms, t0 + 150 * ms)
        // window 1: 95 frames of 10 ms and 5 of 200 ms; window 2: 100 frames of 10 ms
        var t = t0 + 2_000 * ms
        repeat(95) { s.addMatched(t, t + 10 * ms); t += 33 * ms }
        repeat(5) { s.addMatched(t, t + 200 * ms); t += 33 * ms }
        val w1 = s.window(t)
        repeat(100) { s.addMatched(t, t + 10 * ms); t += 33 * ms }
        val w2 = s.window(t)
        val seg = s.segment(t)
        assertEquals(1, seg.warmup)
        assertEquals(200, seg.count)
        assertEquals("window 1 alone: p95 = 10 ms (rank 95)", 10_000L, w1.p95Us)
        assertEquals(10_000L, w2.p95Us)
        // whole segment: 5 of 200 sorted last -> ranks 196..200; p99 rank 198 -> 200 ms; p95 rank 190 -> 10 ms
        assertEquals(10_000L, seg.p95Us)
        assertEquals(200_000L, seg.p99Us)
        assertEquals(200_000L, seg.maxUs)
    }

    @Test
    fun aNewLevelOrPath_closesTheSegment_samplesAreNotMixed() {
        val s = SharpenLatencyStats()
        val off = SharpenLatencyStats.Key("off", "direct", 0, 1)
        val weak = SharpenLatencyStats.Key("weak", "sharpen", 1, 1)
        val t0 = 0L
        s.enter(off, t0)
        var t = t0 + 2_000 * ms
        repeat(10) { s.addMatched(t, t + 60 * ms); t += 33 * ms }
        assertNull("the same key does not close anything", s.enter(off, t))
        val closed = s.enter(weak, t)!!
        assertEquals(off, closed.first)
        assertEquals(10, closed.second.count)
        assertEquals(60_000L, closed.second.p95Us)
        // a frame handed to the OLD producer, latched after the switch: stale, not a weak sample
        s.addMatched(t - 5 * ms, t + 2_000 * ms)
        t += 2_000 * ms
        repeat(10) { s.addMatched(t, t + 80 * ms); t += 33 * ms }
        val seg = s.segment(t)
        assertEquals(1, seg.stale)
        assertEquals(10, seg.count)
        assertEquals(80_000L, seg.p95Us)
    }

    @Test
    fun beyondTheCap_samplesAreCountedAsOverflow_andSaidSo() {
        val s = SharpenLatencyStats(cap = 50)
        s.enter(SharpenLatencyStats.Key("weak", "sharpen", 1, 1), 0)
        var t = 2_000 * ms
        repeat(60) { s.addMatched(t, t + 10 * ms); t += 33 * ms }
        s.addUnmatched()
        s.addRedraw()
        val seg = s.segment(t)
        assertEquals(50, seg.count)
        assertEquals(10, seg.overflow)
        assertEquals(1, seg.unmatched)
        assertEquals(1, seg.redraws)
    }
}
