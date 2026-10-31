p = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'
CR = chr(13)
s = open(p, encoding='utf-8', newline='').read()
crlf = (CR + '\n') in s
if crlf:
    s = s.replace(CR + '\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''        diagnosticsLog = SessionDiagnosticsLog(this)
        SharpenTestSeam.runFixture(this, intent)''', '''        diagnosticsLog = SessionDiagnosticsLog(this)
        if (BuildConfig.LIVENESS_TRACE) startLivenessTraceLogger()  // apk-decoder-liveness r2: test builds only
        SharpenTestSeam.runFixture(this, intent)''')
rep('''    override fun onDestroy() {
''', '''    // apk-decoder-liveness r2 (Codex design r2 (2)) -- TEST builds only (-Pgnlink.livenessTrace=true): the session's
    // receive / control progress and the decoder worker's, logged as `liveness_trace` twice a second from a thread of
    // its own -- not the UI thread, which is the one that can be held up. The native side formats it from atomics.
    private var livenessTraceThread: Thread? = null

    private fun startLivenessTraceLogger() {
        if (livenessTraceThread != null) return
        val thread = Thread({
            while (!Thread.currentThread().isInterrupted) {
                try {
                    Thread.sleep(500)
                } catch (_: InterruptedException) {
                    return@Thread
                }
                val line = NativeSessionBridge.nativeLivenessTrace(activeHandle)
                if (line.isNotEmpty()) diagnosticsLog.log("liveness_trace", "handle=$activeHandle $line")
            }
        }, "lt-log")
        thread.isDaemon = true
        thread.start()
        livenessTraceThread = thread
    }

    override fun onDestroy() {
        livenessTraceThread?.interrupt()
        livenessTraceThread = null
''')
open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', CR + '\n') if crlf else s)
print('ok kotlin')

q = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/native_video_client_session.cpp'
t = open(q, encoding='utf-8', newline='').read()
a = '  std::string line = livenessTrace_.Format(now_us());'
assert t.count(a) == 1
t = t.replace(a, '  std::string line = std::string(kLivenessTraceMarker) + " " + livenessTrace_.Format(now_us());')
open(q, 'w', encoding='utf-8', newline='').write(t)
print('ok session marker')
