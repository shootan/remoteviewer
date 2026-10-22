package com.remote60.androiddirect

import com.remote60.androiddirect.SharpenPathPolicy.Command
import com.remote60.androiddirect.SharpenPathPolicy.Path
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * sh-filter r1/r2 (Codex contract 3/4/7, r2 H1): who writes into the TextureView, and every change of that.
 * The invariants: the decoder is detached BEFORE another producer is started or bound; the direct Surface
 * is made only after the renderer CONFIRMED it let go of that surface; a surface whose renderer has not
 * confirmed stays blocked however often bind/select are called; a late release of an old surface's renderer
 * cleans up that surface only, once; stale generations do nothing; a failure falls back once per surface
 * episode and keeps the user's choice; weak<->strong is never a producer change.
 */
class SharpenPathPolicyTest {

    private fun policy(level: SharpnessLevel, surface: Long = 1L): SharpenPathPolicy =
        SharpenPathPolicy(level).also { it.surfaceAvailable(surface) }

    private fun start(level: SharpnessLevel): Pair<SharpenPathPolicy, Long> {
        val p = policy(level)
        val gen = (p.bind().single() as Command.StartRenderer).generation
        return p to gen
    }

    /** Producer-connecting commands: what must never happen on a surface that is not free. */
    private fun producers(c: List<Command>): List<Command> =
        c.filter { it is Command.BindDirect || it is Command.StartRenderer }

    @Test
    fun off_bindsTheDecoderDirectly_noRenderer() {
        val p = policy(SharpnessLevel.OFF)
        assertEquals(listOf(Command.BindDirect), p.bind())
        assertEquals(Path.DIRECT, p.path)
        assertEquals(SharpnessLevel.OFF, p.effective)
        assertEquals("binding again while bound does nothing", emptyList<Command>(), p.bind())
    }

    @Test
    fun noSurface_noBind() {
        val p = SharpenPathPolicy(SharpnessLevel.WEAK)
        assertEquals(emptyList<Command>(), p.bind())
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
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen)), off)
        assertEquals(Path.RELEASING, p.path)
        assertEquals("a stale release does nothing", emptyList<Command>(), p.rendererReleased(gen - 1, true))
        assertEquals(listOf(Command.BindDirect), p.rendererReleased(gen, true))
        assertEquals(Path.DIRECT, p.path)
    }

    // ---- r2 H1 -------------------------------------------------------------------------------------

    @Test
    fun releaseTimeout_blocksTheSurface_noNewProducerWhateverIsAsked_untilTheLateReleaseArrives() {
        // r1 went to NONE here, and the next bind connected a producer to a surface the old EGL still held.
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        p.select(SharpnessLevel.OFF)
        assertEquals(emptyList<Command>(), p.rendererReleaseTimedOut(gen))
        assertEquals(Path.BLOCKED, p.path)
        assertEquals(SharpnessLevel.OFF, p.effective)
        val asked = mutableListOf<Command>()
        repeat(5) {
            asked += p.bind()                     // the periodic sync
            asked += p.select(SharpnessLevel.WEAK)
            asked += p.select(SharpnessLevel.OFF)
            asked += p.select(SharpnessLevel.STRONG)
            asked += p.unbind()
            asked += p.bind()
        }
        assertEquals("not one producer on the held surface", emptyList<Command>(), producers(asked))
        assertEquals(Path.BLOCKED, p.path)
        // The late release: only now is the last request (strong, bound) carried out -- once.
        val after = p.rendererReleased(gen, true)
        assertEquals(1, producers(after).size)
        assertTrue(after.single() is Command.StartRenderer)
        assertEquals(Path.STARTING, p.path)
    }

    @Test
    fun releaseNotConfirmed_theSurfaceStaysBlockedForGood_untilItGoes() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        p.select(SharpnessLevel.OFF)
        assertEquals(listOf(Command.ProducerStuck(gen)), p.rendererReleased(gen, false))
        assertEquals(Path.BLOCKED, p.path)
        assertEquals(emptyList<Command>(), producers(p.bind() + p.select(SharpnessLevel.WEAK) + p.bind()))
        val (_, androidReleases) = p.surfaceDestroyed()
        assertTrue("nothing will answer for it: Android releases it", androidReleases)
        p.surfaceAvailable(2L)
        assertTrue("a NEW surface may be used", p.bind().single() is Command.StartRenderer)
    }

    @Test
    fun surfaceA_destroyedWhileItsRendererHangs_surfaceB_worksAlone_andTheLateReleaseFreesOnlyA() {
        val (p, genA) = start(SharpnessLevel.WEAK)
        p.rendererReady(genA)
        p.select(SharpnessLevel.OFF)
        p.rendererReleaseTimedOut(genA)
        val (cA, androidA) = p.surfaceDestroyed()
        assertFalse("A is the app's to release, after its renderer answers", androidA)
        assertEquals(emptyList<Command>(), cA)
        // B comes and goes while A's renderer still hangs.
        p.surfaceAvailable(2L)
        p.select(SharpnessLevel.WEAK)
        val genB = (p.bind().single() as Command.StartRenderer).generation
        p.rendererReady(genB)
        val (cB, androidB) = p.surfaceDestroyed()
        assertFalse(androidB)
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(genB)), cB)
        // C is current. B's renderer answers first, then A's, late -- each frees its own surface, once.
        p.surfaceAvailable(3L)
        assertEquals(listOf(Command.ReleaseSurfaceTexture(2L)), p.rendererReleased(genB, true))
        assertEquals(listOf(Command.ReleaseSurfaceTexture(1L)), p.rendererReleased(genA, true))
        assertEquals("a duplicate answer does nothing", emptyList<Command>(), p.rendererReleased(genA, true))
        assertEquals(0, p.pendingReleases)
        assertEquals("C was never touched by them", Path.NONE, p.path)
        assertTrue(p.bind().single() is Command.StartRenderer)
    }

    @Test
    fun aRendererOfAGoneSurface_thatCouldNotDetach_stillHasItsSurfaceTextureReleased_once() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        p.surfaceDestroyed()
        p.surfaceAvailable(2L)
        assertEquals(listOf(Command.ReleaseSurfaceTexture(1L)), p.rendererReleased(gen, false))
        assertEquals(Path.NONE, p.path)
    }

    // ---- r1 behaviour kept -------------------------------------------------------------------------

    @Test
    fun offToWeak_detachesTheDecoderBeforeStartingTheRenderer() {
        val p = policy(SharpnessLevel.OFF)
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
        assertEquals(Command.ReleaseRenderer(gen), failed[1])
        assertEquals(Command.ShowFallback("shader: x"), failed[2])
        assertEquals(listOf(Command.BindDirect), p.rendererReleased(gen, true))
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
        p.rendererReleased(gen, true)
        val (_, appReleases) = p.surfaceDestroyed()
        assertTrue("the decoder was direct: Android releases the surface", appReleases)
        assertNull(p.fallbackReason)
        p.surfaceAvailable(2L)
        assertTrue("a new surface tries the filter again", p.bind().single() is Command.StartRenderer)

        val (q, g2) = start(SharpnessLevel.WEAK)
        q.rendererFailed(g2, "swap")
        q.rendererReleased(g2, true)
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
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen)), commands)
        assertEquals(listOf(Command.ReleaseSurfaceTexture(1L)), p.rendererReleased(gen, true))
        assertEquals(Path.NONE, p.path)
    }

    @Test
    fun rebindWhileReleasing_wiresAgainAfterTheRendererLetGo() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        p.unbind()  // scene change
        assertEquals(Path.RELEASING, p.path)
        assertEquals("nothing connects while the old renderer still holds the view", emptyList<Command>(), p.bind())
        val after = p.rendererReleased(gen, true)
        assertTrue("then a NEW renderer generation starts", (after.single() as Command.StartRenderer).generation > gen)
    }

    @Test
    fun choiceDuringStart_offReleases_strengthGoesWithReady() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        assertEquals(emptyList<Command>(), p.select(SharpnessLevel.STRONG))
        assertEquals(Command.SetStrength(gen, 0.35f), p.rendererReady(gen)[1])
        val (q, g2) = start(SharpnessLevel.WEAK)
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(g2)), q.select(SharpnessLevel.OFF))
        assertEquals(listOf(Command.BindDirect), q.rendererReleased(g2, true))
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
}
