P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'
CR = chr(13)
s = open(P, encoding='utf-8', newline='').read()
crlf = (CR + '\n') in s
if crlf:
    s = s.replace(CR + '\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''    private fun executeSharpen(commands: List<SharpenPathPolicy.Command>) {
        for (command in commands) {
            when (command) {
                SharpenPathPolicy.Command.BindDirect -> {
                    cancelSharpenGate()
                    val st = videoTextureView.surfaceTexture
                    if (st == null) {
                        diagnosticsLog.log("sharpen_direct_skipped", "reason=no_surface_texture")
                        continue
                    }
                    val surface = Surface(st)
                    videoSurface = surface
                    NativeSessionBridge.nativeSetSurface(surface)
                    diagnosticsLog.log(
                        "sharpen_path",
                        "path=direct requested=${sharpenPolicy.requested.code} effective=off " +
                            "fallback=${sharpenPolicy.fallbackReason ?: "none"}"
                    )
                }
                SharpenPathPolicy.Command.DetachDecoder -> {
                    // Synchronous on the native side: the codec is stopped and the window released before
                    // this returns -- the old producer is gone before any new one connects.
                    NativeSessionBridge.nativeSetSurface(null)
                    videoSurface?.release()
                    videoSurface = null
                    sharpenInputSurface = null  // the renderer owns and releases its input Surface
                }''', '''    private fun executeSharpen(commands: List<SharpenPathPolicy.Command>) {
        for ((index, command) in commands.withIndex()) {
            when (command) {
                SharpenPathPolicy.Command.BindDirect -> {
                    cancelSharpenGate()
                    val st = videoTextureView.surfaceTexture
                    if (st == null) {
                        diagnosticsLog.log("sharpen_direct_skipped", "reason=no_surface_texture")
                        continue
                    }
                    val surface = Surface(st)
                    if (NativeSessionBridge.nativeSetSurface(surface) == 0L) {
                        // r3 D1: refused -- a decoder detach is still pending. Nothing connected; this Surface was
                        // never handed over.
                        surface.release()
                        diagnosticsLog.log("decoder_attach_refused", "path=direct surface=${sharpenPolicy.surfaceId}")
                        executeSharpen(sharpenPolicy.decoderAttachRefused())
                        return
                    }
                    videoSurface = surface
                    decoderDirectSurfaceId = sharpenPolicy.surfaceId
                    diagnosticsLog.log(
                        "sharpen_path",
                        "path=direct requested=${sharpenPolicy.requested.code} effective=off " +
                            "fallback=${sharpenPolicy.fallbackReason ?: "none"}"
                    )
                }
                SharpenPathPolicy.Command.DetachDecoder -> {
                    // r3 D1 (Codex): the native side waits for the codec to stop and its window to be let go -- at
                    // most its deadline (2 s for this handoff). In time: the old producer is gone, go on. Not in
                    // time: the decoder may still be a producer -- nothing more of this batch is carried out, no
                    // Surface is released, the surface is BLOCKED; the late completion is watched for.
                    val result = NativeSessionBridge.nativeSetSurface(null)
                    if (result < 0L) {
                        val seq = -result
                        videoSurface?.let { decoderHeldSurfaces += it }
                        videoSurface = null
                        sharpenInputSurface = null  // the renderer still owns its input; its release is deferred
                        val onSurface = decoderDirectSurfaceId
                        decoderDirectSurfaceId = null
                        val rest = commands.subList(index + 1, commands.size)
                        diagnosticsLog.log(
                            "decoder_detach_blocked",
                            "seq=$seq direct=${onSurface ?: "none"} surface=${sharpenPolicy.surfaceId} notCarriedOut=${rest.size}"
                        )
                        executeSharpen(sharpenPolicy.decoderDetachTimedOut(onSurface, rest))
                        watchDecoderDetach(seq)
                        return
                    }
                    videoSurface?.release()
                    videoSurface = null
                    decoderDirectSurfaceId = null
                    sharpenInputSurface = null  // the renderer owns and releases its input Surface
                }''')
rep('''                        val epochBefore = NativeSessionBridge.nativeCodecEpoch()
                        armSharpenGate(command.generation, epochBefore)
                        NativeSessionBridge.nativeSetSurface(input)''', '''                        val epochBefore = NativeSessionBridge.nativeCodecEpoch()
                        if (NativeSessionBridge.nativeSetSurface(input) == 0L) {
                            // r3 D1: refused -- a decoder detach is still pending: the renderer goes, nothing connects.
                            diagnosticsLog.log("decoder_attach_refused", "path=sharpen gen=${command.generation}")
                            executeSharpen(sharpenPolicy.decoderAttachRefused())
                            return
                        }
                        armSharpenGate(command.generation, epochBefore)''')
rep('''    private fun startSharpenRenderer(generation: Long, strength: Float) {''', '''    // r3 D1: Java Surfaces handed to the decoder whose detach missed its deadline -- released only once the native
    // side reports it done; the TextureView surface the decoder is writing into directly (null: none / the renderer's).
    private val decoderHeldSurfaces = ArrayList<Surface>()
    private var decoderDirectSurfaceId: Long? = null
    private var decoderDetachWaitSeq = 0L
    private var decoderDetachWatchSinceMs = 0L

    private fun watchDecoderDetach(seq: Long) {
        val first = decoderDetachWaitSeq == 0L
        if (seq > decoderDetachWaitSeq) decoderDetachWaitSeq = seq
        if (!first) return  // one watcher, for the latest seq
        decoderDetachWatchSinceMs = SystemClock.elapsedRealtime()
        val poll = object : Runnable {
            override fun run() {
                if (NativeSessionBridge.nativeSurfaceReleasedSeq() < decoderDetachWaitSeq) {
                    val waited = SystemClock.elapsedRealtime() - decoderDetachWatchSinceMs
                    sharpenHandler.postDelayed(this, if (waited < 10_000L) 100L else 500L)
                    return
                }
                val waited = SystemClock.elapsedRealtime() - decoderDetachWatchSinceMs
                for (surface in decoderHeldSurfaces) surface.release()
                val released = decoderHeldSurfaces.size
                decoderHeldSurfaces.clear()
                diagnosticsLog.log("decoder_detach_late_done", "seq=$decoderDetachWaitSeq afterMs=$waited surfacesReleased=$released")
                decoderDetachWaitSeq = 0L
                executeSharpen(sharpenPolicy.decoderDetachCompleted())
            }
        }
        sharpenHandler.postDelayed(poll, 100L)
    }

    private fun startSharpenRenderer(generation: Long, strength: Float) {''')
rep('''        if (destroyedId != null) {
            if (androidReleases) sharpenSurfaceTextures.remove(destroyedId) else sharpenAppOwnedSurfaces += destroyedId
        }
        executeSharpen(commands)
        surfaceBufferWidth = 0
        surfaceBufferHeight = 0
        diagnosticsLog.log("viewer_surface_released", "scene=$currentScene destroyed=1 appReleases=${if (androidReleases) 0 else 1}")
        return androidReleases''', '''        executeSharpen(commands)
        // r3 D1: a decoder detach that missed its deadline may still hold this surface: the app releases it, once,
        // when that detach is done -- never Android now.
        val decoderHolds = destroyedId != null && sharpenPolicy.decoderHolds(destroyedId)
        if (decoderHolds) sharpenPolicy.decoderHoldTakesSurfaceTexture()
        val releasesNow = androidReleases && !decoderHolds
        if (destroyedId != null) {
            if (releasesNow) sharpenSurfaceTextures.remove(destroyedId) else sharpenAppOwnedSurfaces += destroyedId
        }
        surfaceBufferWidth = 0
        surfaceBufferHeight = 0
        diagnosticsLog.log(
            "viewer_surface_released",
            "scene=$currentScene destroyed=1 appReleases=${if (releasesNow) 0 else 1} decoderHolds=${if (decoderHolds) 1 else 0}"
        )
        return releasesNow''')
s = s.replace('\n', CR + '\n') if crlf else s
open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok activity')

B = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt'
t = open(B, encoding='utf-8', newline='').read()
a = '    external fun nativeSetSurface(surface: Surface?)'
assert t.count(a) == 1
t = t.replace(a, '''    // apk-decoder-liveness r3 D1: > 0 done / taken; 0 refused (a detach still pending); < 0 a detach not done within
    // its deadline (-seq: nativeSurfaceReleasedSeq() reaches seq once it is).
    external fun nativeSetSurface(surface: Surface?): Long
    external fun nativeSurfaceReleasedSeq(): Long''')
open(B, 'w', encoding='utf-8', newline='').write(t)
print('ok bridge')
