p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/shseam/java/com/remote60/androiddirect/SharpenShaderFixture.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''                    /** Feed the frame with the CPU producer (no crop) even without a transform. */
                    val cpu: Boolean = false)''', '''                    /** Feed the frame with the CPU producer (no crop) even without a transform. */
                    val cpu: Boolean = false,
                    /** sh-strength r4: "frame" (one frame, its output), "layout_fail" (the output never the view's size:
                     *  wait for the renderer's failure, no video drawn), "pan_redraw" (one frame shown, then a moved
                     *  placement with no new frame, the size check lying `layout:3@2`: the latched frame redrawn there). */
                    val flow: String = "frame")''')

rep('''                Case("rot90B133_s35", ROT_90, 0.35f, ring(), pattern = "alt", outScale = 4f / 3f, display = true))''',
    '''                Case("rot90B133_s35", ROT_90, 0.35f, ring(), pattern = "alt", outScale = 4f / 3f, display = true),
                // r4 (Codex codex_sh_strength_r3.md N1 a): the output never becomes the view's size -- no video, ever
                Case("layoutB133_never", 0, 0.35f, ring(SharpenRenderer.SharpenFaults(layoutMismatchQueries = -1)),
                    pattern = "text", cpu = true, outScale = 4f / 3f, display = true, flow = "layout_fail"),
                // r4 (N1 b): no new decoder frame; a pan placement whose first 3 size checks lie -> the latched frame
                // redrawn ONCE at the new place, its timestamp kept
                Case("panB133_redraw", 0, 0.35f, ring(SharpenRenderer.SharpenFaults(layoutMismatchQueries = 3,
                    layoutMismatchFromPlacement = 2)), pattern = "text", cpu = true, outScale = 4f / 3f, display = true,
                    flow = "pan_redraw"))''')

rep('''        val got = CountDownLatch(1)
        val bytes = AtomicReference<ByteArray?>(null)''', '''        val got = CountDownLatch(1)
        val bytes = AtomicReference<ByteArray?>(null)
        val lastTs = java.util.concurrent.atomic.AtomicLong(0)
        val images = java.util.concurrent.atomic.AtomicInteger(0)''')
rep('''                bytes.set(all)
                got.countDown()''', '''                bytes.set(all)
                lastTs.set(img.timestamp)
                images.incrementAndGet()
                got.countDown()''')
rep('''                override fun onFailed(generation: Long, reason: String) { failure.set(reason); ready.countDown(); got.countDown() }''',
    '''                override fun onFailed(generation: Long, reason: String) { failure.set(reason); ready.countDown(); got.countDown(); failedLatch.countDown() }''')
rep('''        val released = CountDownLatch(1)
        val renderer''', '''        val released = CountDownLatch(1)
        val failedLatch = CountDownLatch(1)
        var firstTs = 0L
        var redrawsAfterPan = 0L
        var drawnAtPan = 0L
        val renderer''')
rep('''                    val rc = SharpenTestSeam.nativeDrawTransformed(surface, c.transform, rgba, VIS_W, VIS_H)
                    if (rc != 0) note = "draw_rc=$rc"
                    if (!got.await(5, TimeUnit.SECONDS)) note = "no_output"
                    if (failure.get() != null) note = "failed:" + failure.get()
                    return@run''', '''                    val rc = SharpenTestSeam.nativeDrawTransformed(surface, c.transform, rgba, VIS_W, VIS_H)
                    if (rc != 0) note = "draw_rc=$rc"
                    if (c.flow == "layout_fail") {
                        // r4 a: only cleared swaps reach the output; the renderer gives up within its bound
                        note = if (failedLatch.await(4, TimeUnit.SECONDS)) "failed:" + failure.get() else "no_failure"
                        return@run
                    }
                    if (!got.await(5, TimeUnit.SECONDS)) note = "no_output"
                    if (failure.get() != null) note = "failed:" + failure.get()
                    if (c.flow == "pan_redraw" && note == "ok") {
                        // r4 b: the frame shown; then a pan (no new frame) -- the size check lies 3 times
                        val until = System.currentTimeMillis() + 2000
                        while (renderer.framesDrawn.get() < 1 && System.currentTimeMillis() < until) Thread.sleep(10)
                        Thread.sleep(200)
                        firstTs = lastTs.get()
                        drawnAtPan = renderer.framesDrawn.get()
                        val r0 = renderer.redraws.get()
                        val dx = Math.round(outW / 4f).toFloat()
                        renderer.setDisplayPlacement(DisplayPlacement(-dx, 0f, outW.toFloat(), outH.toFloat(), outW.toFloat(), outH.toFloat()), 2L)
                        val until2 = System.currentTimeMillis() + 2000
                        while (renderer.redraws.get() <= r0 && failure.get() == null && System.currentTimeMillis() < until2) Thread.sleep(10)
                        Thread.sleep(300)
                        redrawsAfterPan = renderer.redraws.get() - r0
                        if (failure.get() != null) note = "failed:" + failure.get()
                    }
                    return@run''')
rep('''            "\\"heldNoSource\\":${renderer.framesHeldNoSource.get()},\\"drawn\\":${renderer.framesDrawn.get()}}"''',
    '''            "\\"heldNoSource\\":${renderer.framesHeldNoSource.get()},\\"drawn\\":${renderer.framesDrawn.get()}," +
            "\\"flow\\":\\"${c.flow}\\",\\"clearSwaps\\":${renderer.layoutClearSwaps.get()},\\"images\\":${images.get()}," +
            "\\"firstTs\\":$firstTs,\\"lastTs\\":${lastTs.get()},\\"redrawsAfterPan\\":$redrawsAfterPan,\\"drawnAtPan\\":$drawnAtPan," +
            "\\"shownGen\\":${renderer.swappedPlacementGen.get()}}"''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
