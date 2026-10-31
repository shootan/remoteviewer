P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SharpenPathPolicy.kt'
CR = chr(13)
s = open(P, encoding='utf-8', newline='').read()
crlf = (CR + '\n') in s
if crlf:
    s = s.replace(CR + '\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep(''' * Every renderer event carries the generation it was started with;''', ''' * apk-decoder-liveness r3 (Codex D1). The decoder's detach (nativeSetSurface(null)) is bounded: when it does not finish
 * within its deadline the decoder may still be a producer -- on the TextureView surface (direct) or on the renderer's
 * input. Then: the rest of that command batch is not carried out (no new producer); a renderer release it held is
 * deferred (the renderer's input is still fed); the current surface is BLOCKED; nothing of it is released. When the
 * native side reports the detach done (late), only the deferred clean-up runs -- never a bind: the blocked surface stays
 * blocked until it goes, and a surface that arrived meanwhile waits for the next bind. A SurfaceTexture is released by
 * the app once, when its last holder (a renderer, the decoder) has let go.
 *
 * Every renderer event carries the generation it was started with;''')
rep('''    /** RELEASING / BLOCKED: the generation that still holds the current surface (0: stuck for good). */
    private var blockingGeneration = 0L''', '''    /** RELEASING / BLOCKED: the generation that still holds the current surface (0: stuck for good). */
    private var blockingGeneration = 0L
    /** r3 D1: the surface a timed-out decoder detach may still hold (null: none), if the app owns its SurfaceTexture,
     *  and the clean-up held back until it is done. */
    private var decoderHoldSurface: Long? = null
    private var decoderHoldActive = false
    private var decoderHoldAppOwns = false
    private val decoderHoldDeferred = ArrayList<Command>()
    /** r3 D1: the current surface was blocked by a decoder detach (cleared only by a new surface). */
    private var decoderBlocked = false
    val decoderDetachPending: Boolean get() = decoderHoldActive''')
rep('''    fun surfaceAvailable(id: Long) {
        surfaceId = id
        path = Path.NONE
        blockingGeneration = 0L
        afterRelease = Path.NONE
    }''', '''    fun surfaceAvailable(id: Long) {
        surfaceId = id
        path = Path.NONE
        blockingGeneration = 0L
        afterRelease = Path.NONE
        decoderBlocked = false
    }

    /**
     * r3 D1: nativeSetSurface(null) did not finish within its deadline. `decoderOnSurface`: the TextureView surface the
     * decoder was writing into directly (null when it was the renderer's input). `remaining`: the commands of the batch
     * not carried out -- only a renderer release among them is kept (deferred); no producer is connected.
     */
    fun decoderDetachTimedOut(decoderOnSurface: Long?, remaining: List<Command>): List<Command> {
        decoderHoldActive = true
        if (decoderOnSurface != null) decoderHoldSurface = decoderOnSurface
        decoderHoldDeferred += remaining.filterIsInstance<Command.ReleaseRenderer>()
        if (surfaceId != null) {
            path = Path.BLOCKED
            decoderBlocked = true
            afterRelease = Path.NONE
        }
        return emptyList()
    }

    /** r3 D1: the surface `id` is (still) held by a pending decoder detach. */
    fun decoderHolds(id: Long): Boolean = decoderHoldActive && decoderHoldSurface == id

    /** r3 D1: that surface's SurfaceTexture was destroyed while the decoder may hold it: the app releases it, later. */
    fun decoderHoldTakesSurfaceTexture() {
        decoderHoldAppOwns = true
    }

    /**
     * r3 D1: the native side reports the detach done (the codec stopped and deleted, its window let go). The deferred
     * clean-up only -- no bind, nothing unblocked.
     */
    fun decoderDetachCompleted(): List<Command> {
        if (!decoderHoldActive) return emptyList()
        val commands = ArrayList<Command>(decoderHoldDeferred)
        decoderHoldDeferred.clear()
        val id = decoderHoldSurface
        if (id != null && decoderHoldAppOwns && pending.values.none { it.surfaceId == id }) {
            commands += Command.ReleaseSurfaceTexture(id)
        }
        decoderHoldActive = false
        decoderHoldSurface = null
        decoderHoldAppOwns = false
        return commands
    }

    /** r3 D1: the native side refused a new surface (a detach is still pending). Nothing is connected. */
    fun decoderAttachRefused(): List<Command> = when (path) {
        Path.DIRECT -> { path = Path.NONE; emptyList() }
        Path.STARTING, Path.LIVE -> releaseRenderer(after = Path.NONE)
        else -> emptyList()
    }''')
rep('''    fun bind(): List<Command> {
        if (surfaceId == null) return emptyList()
        return when (path) {''', '''    fun bind(): List<Command> {
        if (surfaceId == null) return emptyList()
        // r3 D1: a detach still pending somewhere: no producer is connected until it is done -- and its late
        // completion does not connect one either; the next bind does.
        if (decoderHoldActive && path == Path.NONE) return emptyList()
        return when (path) {''')
rep('''        val p = pending.remove(gen) ?: return emptyList()
        if (p.surfaceId != surfaceId || gen != blockingGeneration) {''', '''        val p = pending.remove(gen) ?: return emptyList()
        if (decoderHoldActive && decoderHoldSurface == p.surfaceId) {
            // r3 D1: the decoder may still hold that surface too: its SurfaceTexture waits for the decoder's detach.
            if (p.appOwnsSurfaceTexture) decoderHoldAppOwns = true
            if (p.surfaceId == surfaceId) { blockingGeneration = 0L; path = Path.BLOCKED }
            return emptyList()
        }
        if (decoderBlocked && p.surfaceId == surfaceId) {
            // r3 D1: blocked by a decoder detach that missed its deadline: the renderer's late release does not
            // unblock it (only a new surface does).
            blockingGeneration = 0L
            path = Path.BLOCKED
            return if (p.appOwnsSurfaceTexture) listOf(Command.ReleaseSurfaceTexture(p.surfaceId)) else emptyList()
        }
        if (p.surfaceId != surfaceId || gen != blockingGeneration) {''')
rep('''        surfaceId = null
        path = Path.NONE
        blockingGeneration = 0L
        afterRelease = Path.NONE
        // A new surface is a new episode: the next one may try the filter again.
        fallbackReason = null
        return result''', '''        surfaceId = null
        path = Path.NONE
        blockingGeneration = 0L
        afterRelease = Path.NONE
        decoderBlocked = false
        // A new surface is a new episode: the next one may try the filter again.
        fallbackReason = null
        return result''')
s = s.replace('\n', CR + '\n') if crlf else s
open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok policy')
