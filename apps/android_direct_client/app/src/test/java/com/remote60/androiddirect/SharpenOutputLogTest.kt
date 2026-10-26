package com.remote60.androiddirect

import com.remote60.androiddirect.SharpenOutputLog.Kind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** sh-strength r5 (Codex codex_sh_strength_r4.md N1-c1): the latched buffer, by its own timestamp. */
class SharpenOutputLogTest {

    // The order Codex named: layout A's buffer latched, its callback late; B swapped meanwhile (not latched yet).
    // The late callback reads A's buffer timestamp -> A is shown, never B.
    @Test fun aLateCallbackForAsBufferNeverShowsB() {
        val log = SharpenOutputLog()
        val a = log.reserve(Kind.VIDEO, 1L, 100L)
        val b = log.reserve(Kind.VIDEO, 2L, 101L)   // swapped after A's latch
        val late = SharpenLatch.decide(a, log)
        assertEquals(1L, late.shownGen)
        assertTrue(late.frame)
        assertEquals(100L, late.frameTs)
        // B's own latch, when it comes
        assertEquals(2L, SharpenLatch.decide(b, log).shownGen)
    }

    // A cleared buffer carrying the same decoder frame as a video buffer not latched yet: no frame, no first output,
    // no layout shown -- the output kind tells them apart, not the decoder timestamp.
    @Test fun aClearWithTheSameDecoderFrameIsNothing() {
        val log = SharpenOutputLog()
        val video = log.reserve(Kind.VIDEO, 2L, 200L)
        val clear = log.reserve(Kind.CLEAR, 2L, 200L)
        val c = SharpenLatch.decide(clear, log)
        assertFalse(c.frame)
        assertFalse(c.redraw)
        assertNull(c.shownGen)
        assertTrue(SharpenLatch.decide(video, log).frame)
    }

    @Test fun aRedrawIsNotANewFrameButShowsItsLayout() {
        val log = SharpenOutputLog()
        log.reserve(Kind.VIDEO, 1L, 300L)
        val r = SharpenLatch.decide(log.reserve(Kind.REDRAW, 2L, 300L), log)
        assertFalse(r.frame)
        assertTrue(r.redraw)
        assertEquals(300L, r.frameTs)
        assertEquals(2L, r.shownGen)
    }

    @Test fun unknownOutputsSayNothing() {
        val log = SharpenOutputLog(capacity = 4)
        val other = SharpenOutputLog()
        val first = log.reserve(Kind.VIDEO, 1L, 1L)
        // another renderer's log, an evicted entry, a swap that failed: no frame, no layout
        assertEquals(SharpenLatch.decide(first, other), SharpenLatch.decide(SharpenOutputLog.BASE + 999, log))
        assertNull(SharpenLatch.decide(first, other).shownGen)
        repeat(4) { log.reserve(Kind.VIDEO, 1L, 10L + it) }
        val evicted = SharpenLatch.decide(first, log)
        assertFalse(evicted.frame || evicted.redraw)
        assertNull(evicted.shownGen)
        val failed = log.reserve(Kind.VIDEO, 3L, 50L)
        log.drop(failed)
        assertNull(SharpenLatch.decide(failed, log).shownGen)
        assertFalse(SharpenLatch.decide(failed, null).frame)
    }

    @Test fun aDecoderTimestampIsTheDirectPathsFrame() {
        val d = SharpenLatch.decide(123_456L, SharpenOutputLog())
        assertTrue(d.frame)
        assertEquals(123_456L, d.frameTs)
        assertNull(d.shownGen)
    }

    @Test fun outputTimestampsAreUniqueAndAboveDecoderTime() {
        val log = SharpenOutputLog()
        val t1 = log.reserve(Kind.VIDEO, 1L, 5L)
        val t2 = log.reserve(Kind.REDRAW, 1L, 5L)
        assertTrue(t1 != t2 && SharpenOutputLog.isOutput(t1) && SharpenOutputLog.isOutput(t2))
        assertFalse(SharpenOutputLog.isOutput(Long.MAX_VALUE / 4))
    }
}
