base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/'

def edit(rel, pairs):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

M = 'main/java/com/remote60/androiddirect/MainActivity.kt'
edit(M, [
    ('''        // sh-strength r4 (N1 c): the layout the view now shows -- the one the renderer's last video swap was drawn with
        // (a cleared swap names none). One buffer of slack: a swap that lands between this latch and the read is
        // credited a frame early (stated limit).
        sharpenRenderer?.let { sharpenShownPlacementGen = it.swappedPlacementGen.get() }
        if (timestamp == SharpenRenderer.CLEAR_SWAP_TIMESTAMP) return  // r4: a cleared swap before any frame
        // sh-filter r2 (H3): the latched timestamp names the frame. A strength change re-swaps the frame
        // already shown with the same timestamp: that latch is not a new displayed frame.
        val newFrame = latchClassifier.isNewFrame(surfaceId, timestamp)
        enterSharpenLatencySegment(latchNs)
        if (!newFrame) {
            sharpenStats.addRedraw()
            return
        }''', '''        // sh-strength r5 (Codex codex_sh_strength_r4.md N1-c1): the buffer the view LATCHED, by its own timestamp -- a
        // renderer output is looked up in that renderer's log (what it is, its layout, its decoder frame); never the
        // renderer's latest swap. A cleared buffer or an unknown one counts as nothing and shows no layout.
        val latched = SharpenLatch.decide(timestamp, sharpenRenderer?.outputs)
        latched.shownGen?.let { sharpenShownPlacementGen = it }
        if (!latched.frame && !latched.redraw) return
        val frameTs = latched.frameTs  // the decoder frame this buffer shows
        // sh-filter r2 (H3): the decoder timestamp names the frame. A strength change re-draws the frame already
        // shown: that latch is not a new displayed frame.
        val newFrame = latched.frame && latchClassifier.isNewFrame(surfaceId, frameTs)
        enterSharpenLatencySegment(latchNs)
        if (!newFrame) {
            sharpenStats.addRedraw()
            return
        }'''),
    ('''        val info = FrameInfo.of(NativeSessionBridge.nativeHandoffInfo(timestamp))
        // S4: the decoder's handoff of THIS frame -> this latch.''', '''        val info = FrameInfo.of(NativeSessionBridge.nativeHandoffInfo(frameTs))
        // S4: the decoder's handoff of THIS frame -> this latch.'''),
    ('''                    "ts=$timestamp latchesBefore=${sharpenGate.latchesBeforeConfirm} " +''',
     '''                    "ts=$frameTs latchesBefore=${sharpenGate.latchesBeforeConfirm} " +'''),
    # c2: the router decides the whole stream at its DOWN
    ('''        when (viewerTouchRouter.route(event.actionMasked, viewerPanMode && viewerViewport.isLarge)) {''',
     '''        // sh-strength r5 (N1-c2): a stream that starts while B's new layout is not shown yet is swallowed WHOLE (second
        // finger, moves, partial ups) until its final up; the next new DOWN is judged again.
        val layoutShown = !sharpenLayoutNotShown()
        if (event.actionMasked == MotionEvent.ACTION_DOWN && !layoutShown && !(viewerPanMode && viewerViewport.isLarge) &&
            sharpenInputHeldGen != sharpenPlacementGenSent) {
            sharpenInputHeldGen = sharpenPlacementGenSent
            diagnosticsLog.log("input_held_layout", "sent=$sharpenPlacementGenSent shown=$sharpenShownPlacementGen")
        }
        when (viewerTouchRouter.route(event.actionMasked, viewerPanMode && viewerViewport.isLarge, layoutShown)) {'''),
    ('''            // sh-strength r4 (N1 c): the picture on screen is still the old layout while the mapping is the new one --
            // this touch is not sent (no coordinate of either layout); the next one, once shown, is.
            if (sharpenLayoutNotShown()) {
                if (sharpenInputHeldGen != sharpenPlacementGenSent) {
                    sharpenInputHeldGen = sharpenPlacementGenSent
                    diagnosticsLog.log("input_held_layout", "sent=$sharpenPlacementGenSent shown=$sharpenShownPlacementGen")
                }
                return true
            }
''', ''),
    # a layout change during a PC stream: released once, the rest swallowed
    ('''                sharpenPlacementGenSent += 1  // r4 (N1 c): named, so its display can be told
                sharpenRenderer?.setDisplayPlacement(place, sharpenPlacementGenSent)''',
     '''                sharpenPlacementGenSent += 1  // r4 (N1 c): named, so its display can be told
                sharpenRenderer?.setDisplayPlacement(place, sharpenPlacementGenSent)
                // r5 (N1-c2): a PC stream under way was mapped with the old layout -- released once, the rest swallowed
                interruptViewerTouch("layout_$sharpenPlacementGenSent")'''),
])

R = 'main/java/com/remote60/androiddirect/ViewerTouchRouter.kt'
edit(R, [
    (''' * The next ACTION_DOWN starts the next stream in whatever mode is current then.''',
     ''' * The next ACTION_DOWN starts the next stream in whatever mode is current then.
 * sh-strength r5 (N1-c2): a PC stream whose ACTION_DOWN comes while the picture's new layout is not shown yet
 * ([layoutShown] false) is SWALLOW from its start to its final up -- a second finger, a move, a partial up never
 * begins sending; the layout becoming shown mid-stream does not resume it. A LOCAL stream (화면 이동) is unaffected.'''),
    ('''    /** The route for this event. [localNow] = 크게보기 with 화면 이동 on, read only at ACTION_DOWN. */
    fun route(actionMasked: Int, localNow: Boolean): Owner {
        if (actionMasked == ACTION_DOWN) {
            owner = if (localNow) Owner.LOCAL else Owner.PC''', '''    /** The route for this event. [localNow] = 크게보기 with 화면 이동 on, [layoutShown] = the picture on screen is the
     *  layout input maps with; both read only at ACTION_DOWN. */
    fun route(actionMasked: Int, localNow: Boolean, layoutShown: Boolean = true): Owner {
        if (actionMasked == ACTION_DOWN) {
            owner = if (localNow) Owner.LOCAL else if (layoutShown) Owner.PC else Owner.SWALLOW'''),
])

F = 'shseam/java/com/remote60/androiddirect/SharpenShaderFixture.kt'
edit(F, [
    ('''                        firstTs = lastTs.get()''', '''                        firstTs = renderer.outputs.lookup(lastTs.get())?.decoderTs ?: -1L  // r5: the decoder frame shown'''),
    ('''                        redrawsAfterPan = renderer.redraws.get() - r0''', '''                        redrawsAfterPan = renderer.redraws.get() - r0
                        val last = renderer.outputs.lookup(lastTs.get())
                        lastKind = last?.kind?.name ?: "unknown"
                        lastDecoderTs = last?.decoderTs ?: -1L
                        lastGen = last?.placementGen ?: -1L'''),
    ('''        var drawnAtPan = 0L''', '''        var drawnAtPan = 0L
        var lastKind = ""
        var lastDecoderTs = 0L
        var lastGen = 0L'''),
    ('''"\\"firstTs\\":$firstTs,\\"lastTs\\":${lastTs.get()},''', '''"\\"firstTs\\":$firstTs,\\"lastTs\\":$lastDecoderTs,\\"lastKind\\":\\"$lastKind\\",\\"lastGen\\":$lastGen,\\"outTs\\":${lastTs.get()},'''),
    ('''            "\\"shownGen\\":${renderer.swappedPlacementGen.get()}}"''', '''            "\\"end\\":0}"'''),
])
print('ok')
