p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/shseam/java/com/remote60/androiddirect/SharpenShaderFixture.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''                    val pattern: String = "linear")''', '''                    val pattern: String = "linear",
                    /** sh-strength r2: the output is this many times the visible size (B: the view's pixels). */
                    val outScale: Float = 1f,
                    /** sh-strength r2 B: draw through the display placement (the whole output) -- else A. */
                    val display: Boolean = false,
                    /** Feed the frame with the CPU producer (no crop) even without a transform. */
                    val cpu: Boolean = false)''')

rep('''    val CASES by lazy { listOf(''', '''    /** sh-strength r2 (Codex §4/§4B): ONE input picture ("text": hard 2-px bars, a 1-px line, a low-contrast
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
                Case("textB133_noring", 0, 0.35f, pattern = "text", cpu = true, outScale = 4f / 3f, display = true))
    }

    val CASES by lazy { listOf(''')

rep('''        pattern == "alt" -> intArrayOf(40 + 2 * x + (x % 2) * 50, 40 + 3 * y + (y % 2) * 50, 170 - x - y + ((x + y) % 2) * 30)''',
    '''        pattern == "alt" -> intArrayOf(40 + 2 * x + (x % 2) * 50, 40 + 3 * y + (y % 2) * 50, 170 - x - y + ((x + y) % 2) * 30)
        pattern == "text" -> {
            val v = when {
                x in 8..39 && y in 4..35 -> if ((x / 2) % 2 == 0) 228 else 28   // 2-px bars, white on black
                y == 20 -> 228                                                  // a 1-px line
                x >= 44 && y in 4..35 -> if ((x + y) % 2 == 0) 120 else 136     // low contrast, 1-px checker
                else -> 28
            }
            intArrayOf(v, v, (v * 3 + 255) / 4)  // a colour tint in blue so channels differ
        }''')

rep('''    fun run(context: Context): String {
        NativeSessionBridge.nativeCodecEpoch()  // the native library is loaded
        val out = StringBuilder("{\\"cases\\":[")
        CASES.forEachIndexed { i, c ->''', '''    fun run(context: Context, set: String = "shader"): String {
        NativeSessionBridge.nativeCodecEpoch()  // the native library is loaded
        val out = StringBuilder("{\\"cases\\":[")
        (if (set == "strengthab") CASES_AB else CASES).forEachIndexed { i, c ->''')

rep('''        val quarter = c.transform and ROT_90 != 0
        val outW = if (quarter) VIS_H else VIS_W
        val outH = if (quarter) VIS_W else VIS_H''', '''        val quarter = c.transform and ROT_90 != 0
        val baseW = if (quarter) VIS_H else VIS_W
        val baseH = if (quarter) VIS_W else VIS_H
        val outW = Math.round(baseW * c.outScale)
        val outH = Math.round(baseH * c.outScale)''')

rep('''            }, c.faults)
        var note = "ok"''', '''            }, c.faults)
        if (c.display) renderer.setDisplayPlacement(DisplayPlacement(0f, 0f, outW.toFloat(), outH.toFloat()))
        var note = "ok"''')

rep('''                if (c.transform != 0) {''', '''                if (c.transform != 0 || c.cpu) {''')

rep('''            "\\"src\\":\\"${renderer.lastSourceSize}\\"}"''', '''            "\\"src\\":\\"${renderer.lastSourceSize}\\",\\"outScale\\":${c.outScale},\\"display\\":${c.display}," +
            "\\"heldNoSource\\":${renderer.framesHeldNoSource.get()},\\"drawn\\":${renderer.framesDrawn.get()}}"''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

q = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/shseam/java/com/remote60/androiddirect/SharpenTestSeam.kt'
t = open(q, 'rb').read().decode('utf-8')
crlf = '\r\n' in t
t = t.replace('\r\n', '\n')
old = '''        if (intent?.getStringExtra("shTestFixture") != "shader") return
        Thread {
            val json = try { SharpenShaderFixture.run(context.applicationContext) } catch (t: Throwable) {'''
new = '''        val set = intent?.getStringExtra("shTestFixture")
        if (set != "shader" && set != "strengthab") return  // sh-strength r2: "strengthab" = SharpenShaderFixture.CASES_AB
        Thread {
            val json = try { SharpenShaderFixture.run(context.applicationContext, set) } catch (t: Throwable) {'''
assert t.count(old) == 1
t = t.replace(old, new)
if crlf:
    t = t.replace('\n', '\r\n')
open(q, 'wb').write(t.encode('utf-8'))
print('ok')
