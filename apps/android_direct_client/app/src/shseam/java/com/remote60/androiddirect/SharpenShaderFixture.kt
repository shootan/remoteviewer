package com.remote60.androiddirect

import android.content.Context
import android.graphics.PixelFormat
import android.graphics.Rect
import android.media.ImageReader
import android.media.ImageWriter
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * sh-filter r3 (Codex R3) -- TEST BUILDS ONLY. The PRODUCTION SharpenRenderer (its shader, its clamp, its
 * use of the SurfaceTexture transform) driven with a frame whose BufferQueue metadata is set by hand:
 *
 *   input  = the renderer's own input Surface, fed by an ImageWriter -- a 64x48 RGBA buffer whose last 8 rows
 *            are PADDING (sentinel magenta) and whose crop rect is 64x40 (what a decoder's 1088 -> 1080 crop
 *            is to the consumer), optionally with a buffer transform (horizontal mirror, quarter turn) set
 *            through ANativeWindow_setBuffersTransform on that window;
 *   output = an ImageReader of the VISIBLE size (as the app sizes the TextureView buffer), read back as RGBA.
 *
 * Each case's output is written raw to files/sh_fixture_<case>.rgba with a JSON summary; the harness pulls
 * them and checks them against the kernel applied to the visible picture (orientation, every edge, and that
 * no padding pixel reached any output pixel). This is not the SM-S948N's Codec2 -- the crop and transform
 * here come from the producer side of the same BufferQueue, as a decoder's would.
 */
object SharpenShaderFixture {
    const val CODED_W = 64
    const val CODED_H = 48
    const val VIS_W = 64
    const val VIS_H = 40

    /** `control`: a negative control -- a deliberately broken renderer the checks must FAIL. */
    data class Case(val name: String, val transform: Int, val strength: Float,
                    val faults: SharpenRenderer.SharpenFaults = SharpenRenderer.SharpenFaults.NONE,
                    val control: Boolean = false,
                    /** "linear": smooth gradients (crop cases: SurfaceTexture shrinks a crop by a fraction
                     *  of a texel, which a 1-px alternation would turn into large but legitimate differences);
                     *  "alt": gradients + 1-px alternation (no crop: the result depends on 1-px taps). */
                    val pattern: String = "linear",
                    /** sh-strength r2: the output is this many times the visible size (B: the view's pixels). */
                    val outScale: Float = 1f,
                    /** sh-strength r2 B: draw through the display placement (the whole output) -- else A. */
                    val display: Boolean = false,
                    /** Feed the frame with the CPU producer (no crop) even without a transform. */
                    val cpu: Boolean = false)

    // ANativeWindow transform bits (android/native_window.h)
    private const val MIRROR_H = 1
    private const val ROT_90 = 4

    /** sh-strength r2 (Codex §4/§4B): ONE input picture ("text": hard 2-px bars, a 1-px line, a low-contrast
     *  checker), the same frame for every case -- A at off(GL 0)/0.20/0.35/0.60, and B (display-size, one pass) at
     *  x1.33 and x2 with the same strengths; one negative control (B stepping an OUTPUT pixel). */
    val CASES_AB by lazy {
        val strengths = listOf(0f, 0.20f, 0.35f, 0.60f)
        strengths.map { st -> Case("textA_s%02d".format((st * 100).toInt()), 0, st, ring(), pattern = "text", cpu = true) } +
            strengths.map { st -> Case("textB133_s%02d".format((st * 100).toInt()), 0, st, ring(), pattern = "text", cpu = true,
                outScale = 4f / 3f, display = true) } +
            listOf(0.35f, 0.60f).map { st -> Case("textB2_s%02d".format((st * 100).toInt()), 0, st, ring(), pattern = "text", cpu = true,
                outScale = 2f, display = true) } +
            listOf(Case("control_textB133_outtaps", 0, 0.35f, ring(SharpenRenderer.SharpenFaults(outputPixelTaps = true)),
                pattern = "text", cpu = true, outScale = 4f / 3f, display = true, control = true),
                // B with no source size: must not draw (no output-size fallback in B) -- the case expects NO frame
                Case("textB133_noring", 0, 0.35f, pattern = "text", cpu = true, outScale = 4f / 3f, display = true),
                // r3 (Codex §4.3): B with the decoder's crop (64x48 buffer, 64x40 visible, magenta padding) and with a
                // quarter turn, enlarged x1.33 -- taps one SOURCE pixel, the transform once, no padding read
                Case("cropB133_s35", 0, 0.35f, ring(), outScale = 4f / 3f, display = true),
                Case("rot90B133_s35", ROT_90, 0.35f, ring(), pattern = "alt", outScale = 4f / 3f, display = true))
    }

    val CASES by lazy { listOf(
        Case("crop_s0", 0, 0f, ring()), Case("crop_s35", 0, 0.35f, ring()),
        Case("mirror_s35", MIRROR_H, 0.35f, ring(), pattern = "alt"),
        Case("rot90_s35", ROT_90, 0.35f, ring(), pattern = "alt"),
        // no ring entry: the step comes from the (display-oriented) output size, never swapped
        Case("rot90_s35_noring", ROT_90, 0.35f, pattern = "alt"),
        Case("control_crop_unclamped", 0, 0.35f, ring(SharpenRenderer.SharpenFaults(unclampedTaps = true)), control = true),
        Case("control_rot90_noswap", ROT_90, 0.35f, ring(SharpenRenderer.SharpenFaults(noQuarterSwap = true)), control = true,
            pattern = "alt"),
    ) }

    /** The visible picture: a gradient in both axes plus a one-pixel alternation in each (so the kernel's
     *  result depends on its taps being exactly one pixel apart); padding rows are magenta. */
    fun pixel(x: Int, y: Int, pattern: String): IntArray = when {
        y >= VIS_H -> intArrayOf(255, 0, 255)
        pattern == "alt" -> intArrayOf(40 + 2 * x + (x % 2) * 50, 40 + 3 * y + (y % 2) * 50, 170 - x - y + ((x + y) % 2) * 30)
        pattern == "text" -> {
            val v = when {
                x in 8..39 && y in 4..35 -> if ((x / 2) % 2 == 0) 228 else 28   // 2-px bars, white on black
                y == 20 -> 228                                                  // a 1-px line
                x >= 44 && y in 4..35 -> if ((x + y) % 2 == 0) 120 else 136     // low contrast, 1-px checker
                else -> 28
            }
            intArrayOf(v, v, (v * 3 + 255) / 4)  // a colour tint in blue so channels differ
        }
        else -> intArrayOf(32 + 3 * x, 40 + 4 * y, 200 - 2 * x - y)
    }

    /** As the decoder ring would report: the visible size, in the BUFFER's orientation. */
    private fun ring(f: SharpenRenderer.SharpenFaults = SharpenRenderer.SharpenFaults.NONE) =
        f.copy(sourceOverrideW = VIS_W, sourceOverrideH = VIS_H)

    fun run(context: Context, set: String = "shader"): String {
        NativeSessionBridge.nativeCodecEpoch()  // the native library is loaded
        val out = StringBuilder("{\"cases\":[")
        (if (set == "strengthab") CASES_AB else CASES).forEachIndexed { i, c ->
            if (i > 0) out.append(',')
            out.append(runCase(context, c))
        }
        out.append("]}")
        File(context.filesDir, "sh_fixture.json").writeText(out.toString())
        return out.toString()
    }

    private fun runCase(context: Context, c: Case): String {
        val quarter = c.transform and ROT_90 != 0
        val baseW = if (quarter) VIS_H else VIS_W
        val baseH = if (quarter) VIS_W else VIS_H
        val outW = Math.round(baseW * c.outScale)
        val outH = Math.round(baseH * c.outScale)
        val thread = HandlerThread("sh-fixture-${c.name}").apply { start() }
        val handler = Handler(thread.looper)
        val reader = ImageReader.newInstance(outW, outH, PixelFormat.RGBA_8888, 2)
        val got = CountDownLatch(1)
        val bytes = AtomicReference<ByteArray?>(null)
        reader.setOnImageAvailableListener({ r ->
            val img = r.acquireLatestImage() ?: return@setOnImageAvailableListener
            try {
                val plane = img.planes[0]
                val buf = plane.buffer
                val row = ByteArray(outW * 4)
                val all = ByteArray(outW * outH * 4)
                for (y in 0 until outH) {
                    buf.position(y * plane.rowStride)
                    buf.get(row, 0, outW * 4)
                    System.arraycopy(row, 0, all, y * outW * 4, outW * 4)
                }
                bytes.set(all)
                got.countDown()
            } finally {
                img.close()
            }
        }, handler)
        val ready = CountDownLatch(1)
        val input = AtomicReference<Surface?>(null)
        val failure = AtomicReference<String?>(null)
        val released = CountDownLatch(1)
        val renderer = SharpenRenderer(9000L + c.name.hashCode() % 1000, reader.surface, c.strength,
            object : SharpenRenderer.Listener {
                override fun onReady(generation: Long, input2: Surface) { input.set(input2); ready.countDown() }
                override fun onFailed(generation: Long, reason: String) { failure.set(reason); ready.countDown(); got.countDown() }
                override fun onReleased(generation: Long, detached: Boolean) { released.countDown() }
            }, c.faults)
        if (c.display) renderer.setDisplayPlacement(DisplayPlacement(0f, 0f, outW.toFloat(), outH.toFloat(), outW.toFloat(), outH.toFloat()))
        var note = "ok"
        try {
            if (!ready.await(5, TimeUnit.SECONDS) || input.get() == null) {
                note = "not_ready:" + (failure.get() ?: "timeout")
            } else run {
                val surface = input.get()!!
                if (c.transform != 0 || c.cpu) {
                    // ImageWriter does not carry a window transform: the transform cases use a CPU producer on
                    // the same input window (no crop on this path -- the crop cases above cover crop).
                    val rgba = ByteArray(VIS_W * VIS_H * 4)
                    for (y in 0 until VIS_H) for (x in 0 until VIS_W) {
                        val p = pixel(x, y, c.pattern); val o = (y * VIS_W + x) * 4
                        rgba[o] = p[0].toByte(); rgba[o + 1] = p[1].toByte(); rgba[o + 2] = p[2].toByte(); rgba[o + 3] = 255.toByte()
                    }
                    val rc = SharpenTestSeam.nativeDrawTransformed(surface, c.transform, rgba, VIS_W, VIS_H)
                    if (rc != 0) note = "draw_rc=$rc"
                    if (!got.await(5, TimeUnit.SECONDS)) note = "no_output"
                    if (failure.get() != null) note = "failed:" + failure.get()
                    return@run
                }
                val rc = 0
                val writer = ImageWriter.Builder(surface).setWidthAndHeight(CODED_W, CODED_H)
                    .setImageFormat(PixelFormat.RGBA_8888).setMaxImages(1).build()
                val img = writer.dequeueInputImage()
                val plane = img.planes[0]
                val buf = plane.buffer
                for (y in 0 until CODED_H) {
                    buf.position(y * plane.rowStride)
                    for (x in 0 until CODED_W) {
                        val p = pixel(x, y, c.pattern)
                        buf.put(p[0].toByte()); buf.put(p[1].toByte()); buf.put(p[2].toByte()); buf.put(255.toByte())
                    }
                }
                img.cropRect = Rect(0, 0, VIS_W, VIS_H)
                img.timestamp = 1_000_000_000L
                writer.queueInputImage(img)
                if (!got.await(5, TimeUnit.SECONDS)) note = "no_output"
                if (failure.get() != null) note = "failed:" + failure.get()
                if (rc != 0) note += ";transform_rc=$rc"
                writer.close()
            }
        } catch (t: Throwable) {
            note = "error:" + (t.message ?: t.javaClass.simpleName)
        }
        renderer.release()
        released.await(3, TimeUnit.SECONDS)
        reader.close()
        thread.quitSafely()
        val data = bytes.get()
        if (data != null) File(context.filesDir, "sh_fixture_${c.name}.rgba").writeBytes(data)
        return "{\"name\":\"${c.name}\",\"transform\":${c.transform},\"strength\":${c.strength},\"control\":${c.control},\"pattern\":\"${c.pattern}\"," +
            "\"outW\":$outW,\"outH\":$outH,\"bytes\":${data?.size ?: 0},\"note\":\"$note\"," +
            "\"src\":\"${renderer.lastSourceSize}\",\"outScale\":${c.outScale},\"display\":${c.display}," +
            "\"heldNoSource\":${renderer.framesHeldNoSource.get()},\"drawn\":${renderer.framesDrawn.get()}}"
    }

}
