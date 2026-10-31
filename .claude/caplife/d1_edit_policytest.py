P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/SharpenPathPolicyTest.kt'
CR = chr(13)
s = open(P, encoding='utf-8', newline='').read()
crlf = (CR + '\n') in s
if crlf:
    s = s.replace(CR + '\n', '\n')
anchor = '''    @Test
    fun levels_defaultIsWeak_explicitOffKept_unknownIsDefault() {'''
assert s.count(anchor) == 1
block = '''    // ---- apk-decoder-liveness r3 (Codex D1): a decoder detach that misses its deadline ----

    @Test
    fun d1_direct_detachTimesOut_nothingConnects_lateDoneDoesNotBind() {
        val p = policy(SharpnessLevel.OFF)
        p.bind()  // DIRECT
        val c = p.select(SharpnessLevel.WEAK)  // DetachDecoder, StartRenderer
        assertEquals(Command.DetachDecoder, c[0])
        // the detach did not finish: the rest of the batch (the renderer start) is not carried out
        assertEquals(emptyList<Command>(), p.decoderDetachTimedOut(1L, c.subList(1, c.size)))
        assertEquals(Path.BLOCKED, p.path)
        assertTrue(p.decoderHolds(1L))
        assertEquals("blocked: binding asks again only remembers", emptyList<Command>(), producers(p.bind()))
        assertEquals("blocked: another choice connects nothing", emptyList<Command>(), producers(p.select(SharpnessLevel.OFF)))
        assertEquals("the late completion only cleans up -- no bind, still blocked", emptyList<Command>(), p.decoderDetachCompleted())
        assertEquals(Path.BLOCKED, p.path)
        assertEquals(emptyList<Command>(), producers(p.bind()))
        // a NEW surface is a new episode
        p.surfaceAvailable(2L)
        assertEquals(listOf(Command.BindDirect), p.bind())
    }

    @Test
    fun d1_renderer_detachTimesOut_rendererReleaseDeferred_lateReleaseDoesNotUnblock() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        p.rendererReady(gen)
        val c = p.select(SharpnessLevel.OFF)  // DetachDecoder (from the renderer's input), ReleaseRenderer(gen)
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen)), c)
        assertEquals(emptyList<Command>(), p.decoderDetachTimedOut(null, c.subList(1, c.size)))
        assertEquals(Path.BLOCKED, p.path)
        assertFalse("the decoder was on the renderer's input, not on the view", p.decoderHolds(1L))
        assertEquals("the renderer is released only once the decoder let go of its input",
            listOf(Command.ReleaseRenderer(gen)), p.decoderDetachCompleted())
        assertEquals("the renderer's release does not unblock the surface", emptyList<Command>(), p.rendererReleased(gen, true))
        assertEquals(Path.BLOCKED, p.path)
        assertEquals(emptyList<Command>(), producers(p.bind()))
    }

    @Test
    fun d1_surfaceDestroyedWhileTheDecoderHoldsIt_releasedOnceWhenTheDetachIsDone() {
        val p = policy(SharpnessLevel.OFF)
        p.bind()
        val (c, androidReleases) = p.surfaceDestroyed()  // DIRECT: DetachDecoder, Android would release
        assertEquals(listOf(Command.DetachDecoder), c)
        assertTrue(androidReleases)
        p.decoderDetachTimedOut(1L, emptyList())
        assertTrue("the app (not Android) must release it now", p.decoderHolds(1L))
        p.decoderHoldTakesSurfaceTexture()
        assertEquals(listOf(Command.ReleaseSurfaceTexture(1L)), p.decoderDetachCompleted())
        assertEquals("and only once", emptyList<Command>(), p.decoderDetachCompleted())
    }

    @Test
    fun d1_newSurfaceWhileADetachIsPending_waitsForTheNextBind() {
        val p = policy(SharpnessLevel.OFF)
        p.bind()
        p.surfaceDestroyed()
        p.decoderDetachTimedOut(1L, emptyList())
        p.surfaceAvailable(2L)
        assertEquals("no producer while the old detach is pending", emptyList<Command>(), p.bind())
        assertEquals("its late completion connects nothing", emptyList<Command>(), producers(p.decoderDetachCompleted()))
        assertEquals("the next bind does", listOf(Command.BindDirect), p.bind())
    }

    @Test
    fun d1_attachRefused_nothingConnected() {
        val p = policy(SharpnessLevel.OFF)
        p.bind()
        assertEquals(emptyList<Command>(), p.decoderAttachRefused())
        assertEquals(Path.NONE, p.path)
        val (q, gen) = start(SharpnessLevel.WEAK)
        q.rendererReady(gen)
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen)), q.decoderAttachRefused())
    }

'''
s = s.replace(anchor, block + anchor)
s = s.replace('\n', CR + '\n') if crlf else s
open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok policy test')
