p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SharpenRenderer.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''/** sh-strength r2 B: the video quad in output pixels (left/top from the output's top-left). */
data class DisplayPlacement(val left: Float, val top: Float, val width: Float, val height: Float)''',
    '''/**
 * sh-strength r2 B: the video quad in VIEW pixels (left/top from the view's top-left) and the view size it was placed
 * in. The renderer draws it as fractions of that view (r3, Codex codex_sh_strength_r2.md N1), so the picture lands
 * where the viewport placed it whatever the output buffer's size is at that moment -- and it draws video only into a
 * buffer of that view's size (see draw()).
 */
data class DisplayPlacement(val left: Float, val top: Float, val width: Float, val height: Float,
                            val viewWidth: Float, val viewHeight: Float)''')

rep('''    /** sh-strength r2 B: frames not drawn because their source size was unknown (no fallback in B). */
    val framesHeldNoSource = AtomicLong(0)''', '''    /** sh-strength r2 B: frames not drawn because their source size was unknown (no fallback in B). */
    val framesHeldNoSource = AtomicLong(0)
    /** r3 (N1): swaps of a cleared buffer, made only to move the output to the view's size before video is drawn. */
    val layoutClearSwaps = AtomicLong(0)
    /** r3 (N2): placement updates that changed nothing (dropped without any work). */
    val placementsUnchanged = AtomicLong(0)''')

rep('''    private var placement: DisplayPlacement? = null
    private var placementRedrawPending = false''', '''    private var placement: DisplayPlacement? = null
    // r3 (N2): the latest placement asked for and whether a task to take it is posted -- a burst of UI updates is one
    // task that takes the newest; a placement equal to the one in use does nothing.
    @Volatile private var placementWanted: DisplayPlacement? = null
    private val placementPosted = java.util.concurrent.atomic.AtomicBoolean(false)
    // r3 (N1): a new frame not yet latched because the output was not the placement's view size; clear swaps so far.
    private var frameWaitsForLayout = false
    private var clearSwapsForLayout = 0''')

rep('''    fun setDisplayPlacement(value: DisplayPlacement?) {
        handler.post {
            if (released || failed || stopping) return@post
            placement = value
            if (!haveFrame || placementRedrawPending) return@post
            placementRedrawPending = true
            handler.post {
                placementRedrawPending = false
                if (!released && !failed && !stopping && haveFrame) draw(newFrame = false)
            }
        }
    }''', '''    fun setDisplayPlacement(value: DisplayPlacement?) {
        placementWanted = value
        if (!placementPosted.compareAndSet(false, true)) return  // the posted task will take the newest
        handler.post {
            placementPosted.set(false)
            if (released || failed || stopping) return@post
            val next = placementWanted
            if (next == placement) {
                placementsUnchanged.incrementAndGet()
                return@post
            }
            placement = next
            clearSwapsForLayout = 0
            if (frameWaitsForLayout) draw(newFrame = true)       // the frame that waited for this layout
            else if (haveFrame) draw(newFrame = false)           // one redraw of the latched frame, new layout
        }
    }''')

rep('''    private fun draw(newFrame: Boolean) {
        if (faults.neverDraw) return
        val t0 = SystemClock.elapsedRealtimeNanos()
        try {
            val st = input ?: return
            if (newFrame) {''', '''    private fun draw(newFrame: Boolean) {
        if (faults.neverDraw) return
        val t0 = SystemClock.elapsedRealtimeNanos()
        try {
            val st = input ?: return
            val place0 = placement
            if (place0 != null) {
                // r3 (N1): video goes only into an output of the placement's view size. A buffer not there yet (the
                // default size changed, the next dequeued buffer has it) is moved on by swapping a CLEARED frame --
                // not latched, not counted as a frame -- at most twice per layout; the frame waits, not dropped.
                val ow = IntArray(1)
                val oh = IntArray(1)
                EGL14.eglQuerySurface(display, windowSurface, EGL14.EGL_WIDTH, ow, 0)
                EGL14.eglQuerySurface(display, windowSurface, EGL14.EGL_HEIGHT, oh, 0)
                val matches = ow[0] == place0.viewWidth.toInt() && oh[0] == place0.viewHeight.toInt()
                if (!matches && clearSwapsForLayout < 2) {
                    if (newFrame) frameWaitsForLayout = true
                    clearSwapsForLayout += 1
                    layoutClearSwaps.incrementAndGet()
                    GLES20.glViewport(0, 0, ow[0].coerceAtLeast(1), oh[0].coerceAtLeast(1))
                    GLES20.glClearColor(0f, 0f, 0f, 1f)
                    GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                    EGL14.eglSwapBuffers(display, windowSurface)
                    handler.post { if (!released && !failed && !stopping && frameWaitsForLayout) draw(newFrame = true) }
                    return
                }
            }
            frameWaitsForLayout = false
            if (newFrame) {''')

rep('''            if (faults.outputPixelTaps && place != null) {
                // Negative control only: one OUTPUT pixel, expressed in the pre-transform (source) coordinate.
                stepW = place.width.toInt().coerceAtLeast(1)
                stepH = place.height.toInt().coerceAtLeast(1)
            }''', '''            if (faults.outputPixelTaps && place != null) {
                // Negative control only: one OUTPUT pixel, expressed in the pre-transform (source) coordinate.
                stepW = (place.width / place.viewWidth * w[0]).toInt().coerceAtLeast(1)
                stepH = (place.height / place.viewHeight * h[0]).toInt().coerceAtLeast(1)
            }''')

rep('''                val sx = place.width / w[0]
                val sy = place.height / h[0]
                // output y grows downward in the view, NDC y upward
                val cx = (place.left + place.width / 2f) / w[0] * 2f - 1f
                val cy = 1f - (place.top + place.height / 2f) / h[0] * 2f''', '''                // r3 (N1): fractions of the view the placement was made in (the buffer is stretched over that view)
                val sx = place.width / place.viewWidth
                val sy = place.height / place.viewHeight
                // output y grows downward in the view, NDC y upward
                val cx = (place.left + place.width / 2f) / place.viewWidth * 2f - 1f
                val cy = 1f - (place.top + place.height / 2f) / place.viewHeight * 2f''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

# fixture: the placement covers the whole output (view = output)
f = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/shseam/java/com/remote60/androiddirect/SharpenShaderFixture.kt'
t = open(f, 'rb').read().decode('utf-8')
o = '''renderer.setDisplayPlacement(DisplayPlacement(0f, 0f, outW.toFloat(), outH.toFloat()))'''
assert t.count(o) == 1
t = t.replace(o, '''renderer.setDisplayPlacement(DisplayPlacement(0f, 0f, outW.toFloat(), outH.toFloat(), outW.toFloat(), outH.toFloat()))''')
open(f, 'wb').write(t.encode('utf-8'))
print('ok')
