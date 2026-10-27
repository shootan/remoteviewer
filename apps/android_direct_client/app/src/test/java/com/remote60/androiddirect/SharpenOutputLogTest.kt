package com.remote60.androiddirect

import com.remote60.androiddirect.SharpenOutputLog.Kind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** sh-strength r5/r6 (Codex codex_sh_strength_r4.md N1-c1, codex_sh_strength_r5.md N1-a/b): the latched buffer, by its
 *  own timestamp, and what that latch means to the Activity (SharpenLatch.qualify -- the call it makes). */
class SharpenOutputLogTest {

    // The order Codex named: layout A's buffer latched, its callback late; B swapped meanwhile (not latched yet).
    // The late callback reads A's buffer timestamp -> A is shown, never B.
    @Test fun aLateCallbackForAsBufferNeverShowsB() {
        val log = SharpenOutputLog()
        val a = log.reserve(Kind.VIDEO, 1L, 100L)
        val b = log.reserve(Kind.VIDEO, 2L, 101L)   // swapped after A's latch
        val late = SharpenLatch.decide(a, log)
        assertEquals(1L, late.shownGen)
        assertTrue(late.picture)
        assertEquals(100L, late.frameTs)
        assertEquals(2L, SharpenLatch.decide(b, log).shownGen)
    }

    // r6 N1-a: renderer A (released) and renderer B (current) BOTH hold outputs at the same position of their logs.
    // A's late latch looked up in B's log must not be read as B's buffer -- whatever the kinds.
    @Test fun twoRenderersNeverShareAToken() {
        val logA = SharpenOutputLog()
        val logB = SharpenOutputLog()
        val a1 = logA.reserve(Kind.VIDEO, 1L, 10L)
        val b1 = logB.reserve(Kind.CLEAR, 7L, 20L)
        val a2 = logA.reserve(Kind.CLEAR, 1L, 10L)
        val b2 = logB.reserve(Kind.VIDEO, 7L, 21L)
        assertNotEquals(a1, b1)
        assertNotEquals(a2, b2)
        for (old in listOf(a1, a2)) {
            val d = SharpenLatch.decide(old, logB)   // the Activity looks up in the CURRENT renderer's log
            assertFalse(d.picture)
            assertEquals(SharpenLatch.NONE_SHOWN, d.shownGen)
        }
        // B's own buffers are B's
        assertEquals(7L, SharpenLatch.decide(b2, logB).shownGen)
        assertFalse(SharpenLatch.decide(b1, logB).picture)
    }

    @Test fun tokensAreUniqueAcrossConcurrentRenderers() {
        val logs = List(4) { SharpenOutputLog(capacity = 10_000) }
        val seen = java.util.concurrent.ConcurrentHashMap.newKeySet<Long>()
        val threads = logs.map { log -> Thread { repeat(2_000) { seen.add(log.reserve(Kind.VIDEO, 1L, it.toLong())) } } }
        threads.forEach { it.start() }
        threads.forEach { it.join() }
        assertEquals(8_000, seen.size)
    }

    // A cleared buffer carrying the same decoder frame as a video buffer not latched yet: no picture, no first output,
    // and NO layout shown (r6: the previous proof is not kept for a buffer with no picture).
    @Test fun aClearWithTheSameDecoderFrameIsNoPicture() {
        val log = SharpenOutputLog()
        val video = log.reserve(Kind.VIDEO, 2L, 200L)
        val clear = log.reserve(Kind.CLEAR, 2L, 200L)
        val c = SharpenLatch.decide(clear, log)
        assertFalse(c.picture)
        assertEquals(SharpenLatch.NONE_SHOWN, c.shownGen)
        assertTrue(SharpenLatch.decide(video, log).picture)
    }

    // r6 N1-b, through the qualification the Activity uses: frame F's VIDEO never latched, F's REDRAW latched first ->
    // F's first show (counts once); then F's VIDEO and another REDRAW of F -> redraws (count nothing new); a CLEAR -> nothing.
    @Test fun aRedrawThatFirstShowsAFrameCountsOnce() {
        val log = SharpenOutputLog()
        val classifier = LatchClassifier()
        val surface = 5L
        val fVideo = log.reserve(Kind.VIDEO, 1L, 300L)       // swapped, never latched
        val fRedraw = log.reserve(Kind.REDRAW, 2L, 300L)     // the first buffer of F the view latches
        val first = SharpenLatch.qualify(fRedraw, log, classifier, surface)
        assertTrue(first.firstShow)
        assertFalse(first.redraw)
        assertEquals(300L, first.frameTs)
        assertEquals(2L, first.shownGen)
        // the gate the Activity feeds with it: confirmed by this latch (a frame of the armed renderer/surface/codec)
        val gate = SharpenOutputGate()
        gate.arm(9L, surface, 0L, 0L, 0L)
        assertTrue(gate.onLatch(9L, surface, first.firstShow, FrameInfo(10L, 4L, 1L, 64, 40)))
        // F again, either buffer: not a new show
        val againVideo = SharpenLatch.qualify(fVideo, log, classifier, surface)
        assertFalse(againVideo.firstShow)
        assertTrue(againVideo.redraw)
        val again = SharpenLatch.qualify(log.reserve(Kind.REDRAW, 2L, 300L), log, classifier, surface)
        assertFalse(again.firstShow)
        assertTrue(again.redraw)
        // a cleared buffer: nothing at all, no layout shown
        val clear = SharpenLatch.qualify(log.reserve(Kind.CLEAR, 2L, 300L), log, classifier, surface)
        assertFalse(clear.firstShow || clear.redraw)
        assertEquals(SharpenLatch.NONE_SHOWN, clear.shownGen)
        // the next decoded frame G shows for the first time
        assertTrue(SharpenLatch.qualify(log.reserve(Kind.VIDEO, 2L, 333L), log, classifier, surface).firstShow)
    }

    @Test fun unknownOutputsShowNoPicture() {
        val log = SharpenOutputLog(capacity = 4)
        val first = log.reserve(Kind.VIDEO, 1L, 1L)
        repeat(4) { log.reserve(Kind.VIDEO, 1L, 10L + it) }
        val evicted = SharpenLatch.decide(first, log)
        assertFalse(evicted.picture)
        assertEquals(SharpenLatch.NONE_SHOWN, evicted.shownGen)
        val failed = log.reserve(Kind.VIDEO, 3L, 50L)
        log.drop(failed)
        assertFalse(SharpenLatch.decide(failed, log).picture)
        assertFalse(SharpenLatch.decide(failed, null).picture)
        assertFalse(SharpenLatch.decide(SharpenOutputLog.BASE + Long.MAX_VALUE / 8, log).picture)
    }

    @Test fun aDecoderTimestampIsTheDirectPathsFrame() {
        val d = SharpenLatch.decide(123_456L, SharpenOutputLog())
        assertTrue(d.picture)
        assertEquals(123_456L, d.frameTs)
        assertNull(d.shownGen)   // not a renderer output: the layout question does not arise
    }

    @Test fun outputTimestampsAreAboveDecoderTime() {
        val log = SharpenOutputLog()
        val t1 = log.reserve(Kind.VIDEO, 1L, 5L)
        val t2 = log.reserve(Kind.REDRAW, 1L, 5L)
        assertTrue(t1 != t2 && SharpenOutputLog.isOutput(t1) && SharpenOutputLog.isOutput(t2))
        assertFalse(SharpenOutputLog.isOutput(Long.MAX_VALUE / 4))
    }
}
