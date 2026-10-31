p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SharpenRenderer.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep(''' * Colour (contract 2): the OES sampler's YUV->RGB is the driver's, the kernel works on what it returns,''',
    ''' * sh-strength r2 B (Codex codex_field_0162_0231.md §4B -- an A/B EXPERIMENT, not the product path): with a
 * placement set (setDisplayPlacement), the output buffer is the VIEW's pixel size and this pass draws the video quad
 * where ViewerViewport placed it -- enlargement and sharpening in one draw, one final clamp -- over black; the
 * TextureView then adds no transform of its own (the caller's side of the contract). The taps stay one visible
 * SOURCE pixel apart (never an output pixel), the frame's SurfaceTexture transform is still applied once, and a frame
 * whose source size is not known is NOT drawn (the output-size fallback would be the wrong step here). Null = A.
 *
 * Colour (contract 2): the OES sampler's YUV->RGB is the driver's, the kernel works on what it returns,''')

rep('''        /** r3 R3: the shader fixture has no decoder ring; this stands for the ring's (buffer-oriented)
         *  visible size of its frames. 0 = none. */
        val sourceOverrideW: Int = 0,
        val sourceOverrideH: Int = 0,
    ) {''', '''        /** r3 R3: the shader fixture has no decoder ring; this stands for the ring's (buffer-oriented)
         *  visible size of its frames. 0 = none. */
        val sourceOverrideW: Int = 0,
        val sourceOverrideH: Int = 0,
        /** sh-strength r2 B negative control: step one OUTPUT pixel instead of one source pixel. Must fail. */
        val outputPixelTaps: Boolean = false,
    ) {''')

rep('''    /** The visible source size the last frame was drawn with ("WxH"). */''',
    '''    /** sh-strength r2 B: frames not drawn because their source size was unknown (no fallback in B). */
    val framesHeldNoSource = AtomicLong(0)
    /** The visible source size the last frame was drawn with ("WxH"). */''')

rep('''    @Volatile private var strength = faults.forceStrength ?: initialStrength''',
    '''    @Volatile private var strength = faults.forceStrength ?: initialStrength
    /** sh-strength r2 B: where the video quad goes in the (view-sized) output; null = A (full buffer). Renderer
     *  thread only (set through setDisplayPlacement). */
    private var placement: DisplayPlacement? = null
    private var placementRedrawPending = false''')

rep('''    /** Posted; answers onReleased on this thread when the TextureView has no producer left. */''',
    '''    /**
     * sh-strength r2 B: the video quad's rectangle in output pixels (the ViewerViewport placement, view-sized
     * output), or null for A. A still picture is redrawn with it -- the latest one only: placements that arrive
     * while a redraw is pending fold into it.
     */
    fun setDisplayPlacement(value: DisplayPlacement?) {
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
    }

    /** Posted; answers onReleased on this thread when the TextureView has no producer left. */''')

rep('''            aPosition = GLES20.glGetAttribLocation(program, "aPosition")''',
    '''            aPosition = GLES20.glGetAttribLocation(program, "aPosition")
            uPlace = GLES20.glGetUniformLocation(program, "uPlace")''')
rep('''    private var aPosition = -1''', '''    private var aPosition = -1
    private var uPlace = -1''')

rep('''            if (sourceW > 0 && sourceH > 0) {
                stepW = if (quarterTurn) sourceH else sourceW
                stepH = if (quarterTurn) sourceW else sourceH
            } else {
                stepW = w[0]; stepH = h[0]
                if (newFrame) stepFromOutputSize.incrementAndGet()
            }
            GLES20.glUniform2f(uStep, 1f / stepW, 1f / stepH)''',
    '''            val place = placement
            if (sourceW > 0 && sourceH > 0) {
                stepW = if (quarterTurn) sourceH else sourceW
                stepH = if (quarterTurn) sourceW else sourceH
            } else if (place != null) {
                // B: no source size, no draw -- the output (view) size is not a source pixel. Held, counted.
                if (newFrame) framesHeldNoSource.incrementAndGet()
                return
            } else {
                stepW = w[0]; stepH = h[0]
                if (newFrame) stepFromOutputSize.incrementAndGet()
            }
            if (faults.outputPixelTaps && place != null) {
                // Negative control only: one OUTPUT pixel, expressed in the pre-transform (source) coordinate.
                stepW = place.width.toInt().coerceAtLeast(1)
                stepH = place.height.toInt().coerceAtLeast(1)
            }
            GLES20.glUniform2f(uStep, 1f / stepW, 1f / stepH)
            // The quad: the whole buffer (A), or the placement rectangle in NDC of the view-sized output (B).
            if (place != null) {
                GLES20.glClearColor(0f, 0f, 0f, 1f)
                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                val sx = place.width / w[0]
                val sy = place.height / h[0]
                // output y grows downward in the view, NDC y upward
                val cx = (place.left + place.width / 2f) / w[0] * 2f - 1f
                val cy = 1f - (place.top + place.height / 2f) / h[0] * 2f
                GLES20.glUniform4f(uPlace, sx, sy, cx, cy)
            } else {
                GLES20.glUniform4f(uPlace, 1f, 1f, 0f, 0f)
            }''')

rep('''            if (newFrame) {
                lastSourceSize = "${stepW}x${stepH} out=${w[0]}x${h[0]} turn=${if (quarterTurn) 1 else 0} " +
                    "m=%.3f,%.3f,%.3f,%.3f".format(texMatrix[0], texMatrix[1], texMatrix[4], texMatrix[5])
            }''', '''            if (newFrame) {
                lastSourceSize = "${stepW}x${stepH} out=${w[0]}x${h[0]} turn=${if (quarterTurn) 1 else 0} " +
                    "m=%.3f,%.3f,%.3f,%.3f".format(texMatrix[0], texMatrix[1], texMatrix[4], texMatrix[5]) +
                    (if (place != null) " place=%.1f,%.1f,%.1fx%.1f".format(place.left, place.top, place.width, place.height) else "")
            }''')

rep('''        const val VERTEX = """
attribute vec4 aPosition;
attribute vec2 aUv;
varying vec2 vUv;
void main() {
  gl_Position = aPosition;
  vUv = aUv;
}
"""''', '''        // uPlace = (scaleX, scaleY, centreX, centreY) in NDC: (1, 1, 0, 0) is the whole buffer (A).
        const val VERTEX = """
attribute vec4 aPosition;
attribute vec2 aUv;
uniform vec4 uPlace;
varying vec2 vUv;
void main() {
  gl_Position = vec4(aPosition.xy * uPlace.xy + uPlace.zw, 0.0, 1.0);
  vUv = aUv;
}
"""''')

rep('''class SharpenRenderer(''', '''/** sh-strength r2 B: the video quad in output pixels (left/top from the output's top-left). */
data class DisplayPlacement(val left: Float, val top: Float, val width: Float, val height: Float)

class SharpenRenderer(''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
