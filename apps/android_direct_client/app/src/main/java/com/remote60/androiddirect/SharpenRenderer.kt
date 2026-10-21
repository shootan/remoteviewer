package com.remote60.androiddirect

import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.atomic.AtomicLong

/**
 * The sharpening pass (sh-filter r1, Codex contract 1/2/4/6/7). One per surface generation.
 *
 * Its own HandlerThread owns, alone: the EGL display/context, the window surface made on the
 * TextureView's SurfaceTexture (the OUTPUT -- the only producer on that BufferQueue while it lives), the
 * external OES texture and the INPUT SurfaceTexture/Surface the decoder writes into. Nothing here runs on
 * the UI thread except posting; release is posted, never joined.
 *
 * Per decoded frame: onFrameAvailable (input thread = this thread) -> at most ONE pending draw (later frames
 * fold into it and are counted as skipped) -> updateTexImage, the frame's own transform matrix, one
 * full-buffer draw of the 5-tap cross kernel, eglPresentationTimeANDROID(the input frame's timestamp),
 * eglSwapBuffers. The output buffer is the visible video size the TextureView already uses (S1): the
 * compositor still enlarges it with the existing transform, which this class never touches.
 *
 * Geometry (contract 1): the vertex shader passes the PRE-transform coordinate (0..1 over the visible
 * picture). The fragment shader steps one visible SOURCE pixel in that space -- 1/visibleW, 1/visibleH of
 * the frame being drawn, looked up by its timestamp in the decoder's handoff ring (r2 H5.3: not the output
 * buffer's size, which can lag behind a resolution change while the app defers the resize), clamps each tap
 * inside the visible picture, and only then applies the frame's SurfaceTexture transform -- which carries
 * the decoder's crop (e.g. 1088 -> 1080) and any flip/rotation. So no tap reads the coded padding, and the
 * transform is applied exactly once.
 *
 * Colour (contract 2): the OES sampler's YUV->RGB is the driver's, the kernel works on what it returns,
 * and the result is written to an RGBA8888 window surface with no gamma or colour-space step of our own.
 * Whether the driver's conversion matches the compositor's for the direct path is a MEASUREMENT (strength-0
 * seam run vs OFF), not an assumption.
 */
class SharpenRenderer(
    private val generation: Long,
    private val output: SurfaceTexture,
    initialStrength: Float,
    private val listener: Listener,
    private val faults: SharpenFaults = SharpenFaults.NONE,
) {
    interface Listener {
        /** On the renderer thread. The decoder may now be given `input` (this generation only). */
        fun onReady(generation: Long, input: Surface)
        /** On the renderer thread. GL could not start or broke; the renderer stops drawing. */
        fun onFailed(generation: Long, reason: String)
        /**
         * On the renderer thread, once the release is over. `detached`: the output EGL window surface was
         * destroyed (or never made) -- the TextureView surface has no producer from this renderer. False
         * when eglDestroySurface failed or threw: the policy then never treats that surface as free.
         */
        fun onReleased(generation: Long, detached: Boolean)
    }

    /** Test seam hooks (no-op in shipped builds, see SharpenTestSeam). */
    data class SharpenFaults(
        val failContext: Boolean = false,
        val failShader: Boolean = false,
        val failSwapAfterFrames: Int = -1,
        val neverDraw: Boolean = false,
        val forceStrength: Float? = null,
        /** r2 H1: hold the release this long before tearing down (past the app's 2 s release timeout). */
        val holdReleaseMs: Long = 0L,
    ) {
        companion object { val NONE = SharpenFaults() }
    }

    // Counters, read from any thread (diagnostics; never the decoder's own counts -- contract 6).
    val framesAvailable = AtomicLong(0)
    val framesDrawn = AtomicLong(0)
    val framesSkipped = AtomicLong(0)
    val redraws = AtomicLong(0)
    /** r2 H5.3: frames whose visible size was not in the handoff ring (the output size was used instead). */
    val stepFromOutputSize = AtomicLong(0)
    /** The visible source size the last frame was drawn with ("WxH"). */
    @Volatile var lastSourceSize: String = ""
        private set
    /** Monotonic ns of the last swap (frame or redraw); 0 before the first. */
    val lastSwapNs = AtomicLong(0)
    /** Draw cost (updateTexImage..swap), ns, last value and running max for the 1 s diagnostic window. */
    val lastDrawNs = AtomicLong(0)

    private val thread = HandlerThread("gnlink-sharpen-$generation")
    private val handler: Handler
    @Volatile private var strength = faults.forceStrength ?: initialStrength
    private var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var context: EGLContext = EGL14.EGL_NO_CONTEXT
    private var windowSurface: EGLSurface = EGL14.EGL_NO_SURFACE
    private var program = 0
    private var oesTexture = 0
    private var input: SurfaceTexture? = null
    private var inputSurface: Surface? = null
    private var drawPending = false
    private var haveFrame = false
    private var failed = false
    private var released = false
    /** Release asked for: no more draws, even while a (test-held) teardown waits. */
    private var stopping = false
    private val texMatrix = FloatArray(16)
    private var sourceW = 0
    private var sourceH = 0
    private var uTexMatrix = -1
    private var uStep = -1
    private var uStrength = -1
    private var aPosition = -1
    private var aUv = -1
    private val quad: FloatBuffer = ByteBuffer.allocateDirect(QUAD.size * 4).order(ByteOrder.nativeOrder())
        .asFloatBuffer().apply { put(QUAD); position(0) }

    init {
        thread.start()
        handler = Handler(thread.looper)
        handler.post { start() }
    }

    fun setStrength(value: Float) {
        handler.post {
            if (released || failed || stopping) return@post
            strength = faults.forceStrength ?: value
            // Contract 3: a strength change shows at once, even on a still picture -- one redraw of the
            // frame already latched, which is NOT a new decoded frame (contract 6).
            if (haveFrame) draw(newFrame = false)
        }
    }

    /** Posted; answers onReleased on this thread when the TextureView has no producer left. */
    fun release() {
        val finish = Runnable {
            val detached = if (!released) teardown() else true
            released = true
            listener.onReleased(generation, detached)
            thread.quitSafely()
        }
        if (faults.holdReleaseMs > 0L) {
            // Test seam only: a driver that takes long to let go. Drawing stops at once (as in a real
            // release, the decoder has already been detached); the EGL surface stays until the hold ends.
            handler.post { stopping = true }
            handler.postDelayed(finish, faults.holdReleaseMs)
        } else {
            handler.post(finish)
        }
    }

    private fun start() {
        try {
            if (faults.failContext) throw IllegalStateException("context create failed (test seam)")
            display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            if (display == EGL14.EGL_NO_DISPLAY) throw IllegalStateException("eglGetDisplay")
            val version = IntArray(2)
            if (!EGL14.eglInitialize(display, version, 0, version, 1)) throw IllegalStateException("eglInitialize ${eglError()}")
            val configs = arrayOfNulls<EGLConfig>(1)
            val count = IntArray(1)
            val attrs = intArrayOf(
                EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8,
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT,
                EGL14.EGL_NONE,
            )
            if (!EGL14.eglChooseConfig(display, attrs, 0, configs, 0, 1, count, 0) || count[0] == 0) {
                throw IllegalStateException("eglChooseConfig ${eglError()}")
            }
            val config = configs[0]!!
            context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT,
                intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
            if (context == EGL14.EGL_NO_CONTEXT) throw IllegalStateException("eglCreateContext ${eglError()}")
            // The output: the TextureView's SurfaceTexture. Fails if something else is still its producer.
            windowSurface = EGL14.eglCreateWindowSurface(display, config, output, intArrayOf(EGL14.EGL_NONE), 0)
            if (windowSurface == EGL14.EGL_NO_SURFACE) throw IllegalStateException("eglCreateWindowSurface ${eglError()}")
            if (!EGL14.eglMakeCurrent(display, windowSurface, windowSurface, context)) {
                throw IllegalStateException("eglMakeCurrent ${eglError()}")
            }
            program = buildProgram()
            uTexMatrix = GLES20.glGetUniformLocation(program, "uTexMatrix")
            uStep = GLES20.glGetUniformLocation(program, "uStep")
            uStrength = GLES20.glGetUniformLocation(program, "uStrength")
            aPosition = GLES20.glGetAttribLocation(program, "aPosition")
            aUv = GLES20.glGetAttribLocation(program, "aUv")
            val tex = IntArray(1)
            GLES20.glGenTextures(1, tex, 0)
            oesTexture = tex[0]
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTexture)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
            checkGl("texture")
            val st = SurfaceTexture(oesTexture)
            st.setOnFrameAvailableListener({ onFrameAvailable() }, handler)
            input = st
            val surface = Surface(st)
            inputSurface = surface
            listener.onReady(generation, surface)
        } catch (t: Throwable) {
            fail(t.message ?: t.javaClass.simpleName)
        }
    }

    private fun onFrameAvailable() {
        if (released || failed || stopping) return
        framesAvailable.incrementAndGet()
        if (drawPending) {
            framesSkipped.incrementAndGet()  // latest wins: the pending draw will latch the newest
            return
        }
        drawPending = true
        handler.post {
            drawPending = false
            if (!released && !failed && !stopping) draw(newFrame = true)
        }
    }

    private fun draw(newFrame: Boolean) {
        if (faults.neverDraw) return
        val t0 = SystemClock.elapsedRealtimeNanos()
        try {
            val st = input ?: return
            if (newFrame) {
                st.updateTexImage()
                st.getTransformMatrix(texMatrix)  // this frame's crop/flip/rotation
                haveFrame = true
                // This frame's own visible size (a redraw keeps the size of the frame it redraws).
                val info = FrameInfo.of(NativeSessionBridge.nativeHandoffInfo(st.timestamp))
                if (info != null && info.visibleWidth > 0 && info.visibleHeight > 0) {
                    sourceW = info.visibleWidth
                    sourceH = info.visibleHeight
                } else {
                    sourceW = 0
                    sourceH = 0
                }
            }
            val w = IntArray(1)
            val h = IntArray(1)
            EGL14.eglQuerySurface(display, windowSurface, EGL14.EGL_WIDTH, w, 0)
            EGL14.eglQuerySurface(display, windowSurface, EGL14.EGL_HEIGHT, h, 0)
            if (w[0] <= 0 || h[0] <= 0) return
            GLES20.glViewport(0, 0, w[0], h[0])
            GLES20.glUseProgram(program)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTexture)
            GLES20.glUniformMatrix4fv(uTexMatrix, 1, false, texMatrix, 0)
            // One VISIBLE SOURCE pixel of this frame, in the pre-transform coordinate. If the transform turns
            // the picture a quarter (x reads the source's y), the steps swap with it.
            var stepW = sourceW
            var stepH = sourceH
            if (stepW <= 0 || stepH <= 0) {
                stepW = w[0]; stepH = h[0]
                if (newFrame) stepFromOutputSize.incrementAndGet()
            }
            val quarterTurn = kotlin.math.abs(texMatrix[1]) > kotlin.math.abs(texMatrix[0])
            if (quarterTurn) { val t = stepW; stepW = stepH; stepH = t }
            GLES20.glUniform2f(uStep, 1f / stepW, 1f / stepH)
            if (newFrame) lastSourceSize = "${stepW}x${stepH}"
            GLES20.glUniform1f(uStrength, strength)
            quad.position(0)
            GLES20.glVertexAttribPointer(aPosition, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aPosition)
            quad.position(2)
            GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aUv)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
            checkGl("draw")
            // The frame keeps its decoder timestamp through this pass: the TextureView's latch can be
            // matched to the decoder's release by it (a key, not a clock -- S4).
            EGLExt.eglPresentationTimeANDROID(display, windowSurface, st.timestamp)
            if (faults.failSwapAfterFrames >= 0 && framesDrawn.get() >= faults.failSwapAfterFrames) {
                throw IllegalStateException("eglSwapBuffers failed (test seam)")
            }
            // A redraw swaps the SAME timestamp: the view's latch tells it apart by that (LatchClassifier).
            if (!EGL14.eglSwapBuffers(display, windowSurface)) throw IllegalStateException("eglSwapBuffers ${eglError()}")
            lastSwapNs.set(System.nanoTime())
            if (newFrame) framesDrawn.incrementAndGet() else redraws.incrementAndGet()
            lastDrawNs.set(SystemClock.elapsedRealtimeNanos() - t0)
        } catch (t: Throwable) {
            fail(t.message ?: t.javaClass.simpleName)
        }
    }

    private fun fail(reason: String) {
        if (failed || released) return
        failed = true
        // Stop drawing; the GL objects are let go only in release(), on this same thread.
        listener.onFailed(generation, reason)
    }

    /**
     * Returns whether the OUTPUT (the TextureView surface's producer) is confirmed gone. Each step is tried
     * on its own, so a failure in one does not skip the rest; only the output's result is reported up --
     * the other resources are this renderer's own and die with its thread either way.
     */
    private fun teardown(): Boolean {
        // Order: the decoder has already been detached from `input` by the caller (it detaches before it
        // releases us). The output surface goes first -- that is what frees the TextureView's BufferQueue.
        var detached = windowSurface == EGL14.EGL_NO_SURFACE
        val notes = mutableListOf<String>()
        try {
            if (display != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
                if (windowSurface != EGL14.EGL_NO_SURFACE) {
                    detached = EGL14.eglDestroySurface(display, windowSurface)
                    if (!detached) notes += "eglDestroySurface ${eglError()}"
                }
            }
        } catch (t: Throwable) {
            detached = false
            notes += "output: ${t.message ?: t.javaClass.simpleName}"
        }
        if (detached) windowSurface = EGL14.EGL_NO_SURFACE
        try {
            inputSurface?.release()
            input?.setOnFrameAvailableListener(null)
            input?.release()
        } catch (t: Throwable) {
            notes += "input: ${t.message ?: t.javaClass.simpleName}"
        }
        inputSurface = null
        input = null
        try {
            if (display != EGL14.EGL_NO_DISPLAY) {
                if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context)
                EGL14.eglReleaseThread()
                if (detached) EGL14.eglTerminate(display)  // never terminate under a surface that is still there
            }
        } catch (t: Throwable) {
            notes += "context: ${t.message ?: t.javaClass.simpleName}"
        }
        context = EGL14.EGL_NO_CONTEXT
        if (detached) display = EGL14.EGL_NO_DISPLAY
        teardownNotes = notes.joinToString("; ")
        return detached
    }

    /** What went wrong in the last teardown, for the app's log (empty: nothing). */
    @Volatile var teardownNotes: String = ""
        private set

    private fun buildProgram(): Int {
        val vs = compile(GLES20.GL_VERTEX_SHADER, VERTEX)
        val fs = compile(GLES20.GL_FRAGMENT_SHADER, if (faults.failShader) "not a shader" else FRAGMENT)
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, vs)
        GLES20.glAttachShader(p, fs)
        GLES20.glLinkProgram(p)
        val ok = IntArray(1)
        GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0)
        if (ok[0] == 0) throw IllegalStateException("link: ${GLES20.glGetProgramInfoLog(p)}")
        return p
    }

    private fun compile(type: Int, src: String): Int {
        val s = GLES20.glCreateShader(type)
        GLES20.glShaderSource(s, src)
        GLES20.glCompileShader(s)
        val ok = IntArray(1)
        GLES20.glGetShaderiv(s, GLES20.GL_COMPILE_STATUS, ok, 0)
        if (ok[0] == 0) throw IllegalStateException("shader: ${GLES20.glGetShaderInfoLog(s)}")
        return s
    }

    private fun checkGl(what: String) {
        val e = GLES20.glGetError()
        if (e != GLES20.GL_NO_ERROR) throw IllegalStateException("$what glError=0x${Integer.toHexString(e)}")
    }

    private fun eglError(): String = "0x" + Integer.toHexString(EGL14.eglGetError())

    companion object {
        /** x, y, u, v -- a full-buffer strip; u,v are the PRE-transform coordinate over the visible picture. */
        private val QUAD = floatArrayOf(
            -1f, -1f, 0f, 0f,
            1f, -1f, 1f, 0f,
            -1f, 1f, 0f, 1f,
            1f, 1f, 1f, 1f,
        )

        const val VERTEX = """
attribute vec4 aPosition;
attribute vec2 aUv;
varying vec2 vUv;
void main() {
  gl_Position = aPosition;
  vUv = aUv;
}
"""

        // out = (1 + 4s)·C − s·(L + R + T + B), RGB, alpha 1, clamped. Taps one visible pixel apart, each
        // clamped to the visible picture BEFORE the frame's transform (so padding is never read).
        const val FRAGMENT = """
#extension GL_OES_EGL_image_external : require
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform samplerExternalOES uTex;
uniform mat4 uTexMatrix;
uniform vec2 uStep;
uniform float uStrength;
varying vec2 vUv;
vec3 tap(vec2 p) {
  vec2 q = clamp(p, 0.5 * uStep, vec2(1.0) - 0.5 * uStep);
  return texture2D(uTex, (uTexMatrix * vec4(q, 0.0, 1.0)).xy).rgb;
}
void main() {
  vec3 c = tap(vUv);
  vec3 n = tap(vUv + vec2(uStep.x, 0.0)) + tap(vUv - vec2(uStep.x, 0.0))
         + tap(vUv + vec2(0.0, uStep.y)) + tap(vUv - vec2(0.0, uStep.y));
  gl_FragColor = vec4(clamp((1.0 + 4.0 * uStrength) * c - uStrength * n, 0.0, 1.0), 1.0);
}
"""
    }
}
