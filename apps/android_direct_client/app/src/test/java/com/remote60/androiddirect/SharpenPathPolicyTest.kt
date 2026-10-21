package com.remote60.androiddirect

import com.remote60.androiddirect.SharpenPathPolicy.Command
import com.remote60.androiddirect.SharpenPathPolicy.Path
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * sh-filter r1 (Codex contract 3/4/7): who writes into the TextureView, and every change of that.
 * The invariants: the decoder is detached BEFORE another producer is started or bound; the direct
 * Surface is made only after the renderer has let go; stale generations do nothing; a failure falls
 * back once per surface episode and keeps the user's choice; weak<->strong is never a producer change.
 */
class SharpenPathPolicyTest {

    private fun start(level: SharpnessLevel): Pair<SharpenPathPolicy, Long> {
        val p = SharpenPathPolicy(level)
        val c = p.bind()
        val gen = (c.single() as Command.StartRenderer).generation
        return p to gen
    }

    @Test
    fun off_bindsTheDecoderDirectly_noRenderer() {
        val p = SharpenPathPolicy(SharpnessLevel.OFF)
        assertEquals(listOf(Command.BindDirect), p.bind())
        assertEquals(Path.DIRECT, p.path)
        assertEquals(SharpnessLevel.OFF, p.effective)
        assertEquals("binding again while bound does nothing", emptyList<Command>(), p.bind())
    }

    @Test
    fun weak_startsTheRenderer_andAttachesTheDecoderOnlyWhenReady() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        assertEquals(Path.STARTING, p.path)
        assertEquals("no second renderer while one starts", emptyList<Command>(), p.bind())
        assertEquals("a stale ready is ignored", emptyList<Command>(), p.rendererReady(gen - 1))
        val ready = p.rendererReady(gen)
        assertEquals(Command.AttachDecoderToRenderer(gen), ready[0])
        assertEquals(Command.SetStrength(gen, 0.20f), ready[1])
        assertEquals(Path.LIVE, p.path)
        assertEquals(SharpnessLevel.WEAK, p.effective)
    }

    @Test
    fun weakToStrong_isAStrengthChange_notAProducerChange() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        assertEquals(listOf(Command.SetStrength(gen, 0.35f)), p.select(SharpnessLevel.STRONG))
        assertEquals(Path.LIVE, p.path)
    }

    @Test
    fun filterToOff_detachesFirst_releasesTheRenderer_bindsDirectOnlyAfterItLetGo() {
        val (p, gen) = start(SharpnessLevel.STRONG)
        p.rendererReady(gen)
        val off = p.select(SharpnessLevel.OFF)
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen, false)), off)
        assertEquals(Path.RELEASING, p.path)
        assertEquals("a stale release does nothing", emptyList<Command>(), p.rendererReleased(gen - 1))
        assertEquals(listOf(Command.BindDirect), p.rendererReleased(gen))
        assertEquals(Path.DIRECT, p.path)
    }

    @Test
    fun releaseTimeout_leavesTheViewUnconnected_neverTwoProducers() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        p.select(SharpnessLevel.OFF)
        assertEquals(emptyList<Command>(), p.rendererReleaseTimedOut(gen))
        assertEquals(Path.NONE, p.path)
        assertEquals("a late release after the timeout binds nothing", emptyList<Command>(), p.rendererReleased(gen))
    }

    @Test
    fun offToWeak_detachesTheDecoderBeforeStartingTheRenderer() {
        val p = SharpenPathPolicy(SharpnessLevel.OFF)
        p.bind()
        val c = p.select(SharpnessLevel.WEAK)
        assertEquals(Command.DetachDecoder, c[0])
        assertTrue(c[1] is Command.StartRenderer)
        assertEquals(Path.STARTING, p.path)
    }

    @Test
    fun failure_fallsBackToDirect_keepsTheChoice_andDoesNotRetryOnRebind() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        val failed = p.rendererFailed(gen, "shader: x")
        assertEquals(Command.DetachDecoder, failed[0])
        assertEquals(Command.ReleaseRenderer(gen, false), failed[1])
        assertEquals(Command.ShowFallback("shader: x"), failed[2])
        assertEquals(listOf(Command.BindDirect), p.rendererReleased(gen))
        assertEquals("requested stays weak", SharpnessLevel.WEAK, p.requested)
        assertEquals("effective is off", SharpnessLevel.OFF, p.effective)
        assertEquals("shader: x", p.fallbackReason)
        // A scene change and back in the same surface episode: still direct, no retry loop.
        assertEquals(listOf(Command.DetachDecoder), p.unbind())
        assertEquals(listOf(Command.BindDirect), p.bind())
        assertEquals("a duplicate failure report changes nothing", emptyList<Command>(), p.rendererFailed(gen, "again"))
    }

    @Test
    fun failureLatch_clearsWithANewSurface_orAnExplicitNewChoice() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererFailed(gen, "context")
        p.rendererReleased(gen)
        val (_, appReleases) = p.surfaceDestroyed()
        assertTrue("the decoder was direct: Android releases the surface", appReleases)
        assertNull(p.fallbackReason)
        assertTrue("a new surface tries the filter again", p.bind().single() is Command.StartRenderer)

        val (q, g2) = start(SharpnessLevel.WEAK)
        q.rendererFailed(g2, "swap")
        q.rendererReleased(g2)
        assertEquals("re-choosing the same level does not retry", emptyList<Command>(), q.select(SharpnessLevel.WEAK))
        val retry = q.select(SharpnessLevel.STRONG)
        assertEquals("a different filtered choice retries once", Command.DetachDecoder, retry[0])
        assertTrue(retry[1] is Command.StartRenderer)
    }

    @Test
    fun surfaceDestroyed_withTheRendererOn_theAppReleasesTheSurfaceAfterTheRendererLetGo() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        val (commands, androidReleases) = p.surfaceDestroyed()
        assertFalse("Android must not release it while EGL may use it", androidReleases)
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen, true)), commands)
        assertEquals(listOf(Command.ReleaseSurfaceTexture), p.rendererReleased(gen))
        assertEquals(Path.NONE, p.path)
    }

    @Test
    fun rebindWhileReleasing_wiresAgainAfterTheRendererLetGo() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        p.unbind()  // scene change
        assertEquals(Path.RELEASING, p.path)
        assertEquals("nothing connects while the old renderer still holds the view", emptyList<Command>(), p.bind())
        val after = p.rendererReleased(gen)
        assertTrue("then a NEW renderer generation starts", (after.single() as Command.StartRenderer).generation > gen)
    }

    @Test
    fun choiceDuringStart_offReleases_strengthGoesWithReady() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        assertEquals(emptyList<Command>(), p.select(SharpnessLevel.STRONG))
        assertEquals(Command.SetStrength(gen, 0.35f), p.rendererReady(gen)[1])
        val (q, g2) = start(SharpnessLevel.WEAK)
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(g2, false)), q.select(SharpnessLevel.OFF))
        assertEquals(listOf(Command.BindDirect), q.rendererReleased(g2))
    }

    @Test
    fun levels_defaultIsWeak_explicitOffKept_unknownIsDefault() {
        assertEquals(SharpnessLevel.WEAK, SharpnessLevel.fromStored(null))
        assertEquals(SharpnessLevel.OFF, SharpnessLevel.fromStored("off"))
        assertEquals(SharpnessLevel.STRONG, SharpnessLevel.fromStored("strong"))
        assertEquals(SharpnessLevel.WEAK, SharpnessLevel.fromStored("bogus"))
        assertEquals(0.20f, SharpnessLevel.WEAK.strength)
        assertEquals(0.35f, SharpnessLevel.STRONG.strength)
        assertFalse(SharpnessLevel.OFF.filtered)
    }

    @Test
    fun latencyStats_percentilesAndBounds() {
        val s = SharpenLatencyStats()
        for (i in 1..100) s.add(i * 1_000_000L)  // 1..100 ms
        s.add(-1)
        val sum = s.drain()
        assertEquals(100, sum.count)
        assertEquals(1, sum.unmatched)
        assertEquals(51_000L, sum.p50Us)
        assertEquals(96_000L, sum.p95Us)
        assertEquals(100_000L, sum.maxUs)
        assertEquals("drained", 0, s.drain().count)
    }
}
