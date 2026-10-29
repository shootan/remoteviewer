package com.remote60.androiddirect

/**
 * Who writes into the viewer's TextureView, and how that changes (sh-filter r1, Codex contract 3/4/7;
 * r2 H1: ownership per surface and per renderer generation).
 *
 * Two producers can feed a TextureView surface's BufferQueue: the decoder directly (OFF, and every
 * fallback), or the sharpening renderer's EGL window surface (the decoder then writes into the renderer's
 * own input SurfaceTexture). They must never be connected at once, so every change of producer is
 * serialized:
 *
 *   direct  -> filter : the decoder leaves the TextureView (nativeSetSurface(null) -- a codec reset, done
 *                       synchronously by the native side) and its Surface is released; THEN the renderer
 *                       is started on the TextureView; the decoder is bound to the renderer's input only
 *                       when the renderer says it is up, for that same generation.
 *   filter -> direct  : the decoder leaves the renderer's input (nativeSetSurface(null)); the renderer is
 *                       released on its own thread; the direct Surface is made only when the renderer
 *                       reports it has DESTROYED its EGL surface on that TextureView surface.
 *   weak  <-> strong  : no producer change -- a new strength and one redraw of the frame already shown.
 *
 * r2 H1. A renderer whose release has not been confirmed still owns its surface. A release that times out
 * leaves that surface BLOCKED: no bind, no new renderer, no direct Surface on it, whatever is asked in the
 * meantime -- the requests are only remembered -- until the late release arrives (then they are carried
 * out) or the surface itself goes. A release that reports the EGL surface was NOT destroyed blocks the
 * surface for good (until it goes): never two producers, never a "direct" that was not really free.
 * Each surface has an id (MainActivity, per onSurfaceTextureAvailable) and each pending release remembers
 * the surface it was on, so a late release of a renderer from an OLD surface only cleans up that old
 * surface (the app releases that SurfaceTexture, once) and never touches the current one.
 *
 * apk-decoder-liveness r3 (Codex D1). The decoder's detach (nativeSetSurface(null)) is bounded: when it does not finish
 * within its deadline the decoder may still be a producer -- on the TextureView surface (direct) or on the renderer's
 * input. Then: the rest of that command batch is not carried out (no new producer); a renderer release it held is
 * deferred (the renderer's input is still fed); the current surface is BLOCKED; nothing of it is released. When the
 * native side reports the detach done (late), only the deferred clean-up runs -- never a bind: the blocked surface stays
 * blocked until it goes, and a surface that arrived meanwhile waits for the next bind. A SurfaceTexture is released by
 * the app once, when its last holder (a renderer, the decoder) has let go.
 *
 * Every renderer event carries the generation it was started with; ready/failed from anything but the
 * current generation are ignored. A renderer failure falls back to direct and LATCHES for this surface
 * episode (until a new TextureView surface, or the user picks a different filtered level) -- no retry
 * loop, and the user's choice is kept as requested (requested=weak|strong, effective=off).
 *
 * Pure: no Android types. MainActivity executes the commands; SharpenPathPolicyTest drives it.
 */
class SharpenPathPolicy(requested: SharpnessLevel) {

    enum class Path { NONE, DIRECT, STARTING, LIVE, RELEASING, BLOCKED }

    sealed class Command {
        /** Make the direct Surface on the current TextureView surface and give it to the decoder. */
        object BindDirect : Command()
        /** nativeSetSurface(null) and release whatever Surface the decoder had. */
        object DetachDecoder : Command()
        /** Start a renderer on the current TextureView surface; it reports with this generation. */
        data class StartRenderer(val generation: Long, val strength: Float) : Command()
        /** Give the renderer's input Surface (of this generation) to the decoder. */
        data class AttachDecoderToRenderer(val generation: Long) : Command()
        /** Release the renderer of this generation (posted to its thread; it answers Released). */
        data class ReleaseRenderer(val generation: Long) : Command()
        data class SetStrength(val generation: Long, val strength: Float) : Command()
        /** Tell the user, briefly: 선명도 적용 불가, 원본 표시. */
        data class ShowFallback(val reason: String) : Command()
        /** The app owns this (destroyed) surface's SurfaceTexture: release it now, once. */
        data class ReleaseSurfaceTexture(val surfaceId: Long) : Command()
        /** The renderer of `generation` could not confirm its EGL surface on the current surface is gone. */
        data class ProducerStuck(val generation: Long) : Command()
    }

    var requested: SharpnessLevel = requested
        private set
    /** The state of the CURRENT surface. */
    var path: Path = Path.NONE
        private set
    var generation: Long = 0L
        private set
    /** Set by a renderer failure; clears with a new surface episode or a new filtered choice. */
    var fallbackReason: String? = null
        private set
    /** The current TextureView surface (null: none). */
    var surfaceId: Long? = null
        private set

    private class Pending(val surfaceId: Long, var appOwnsSurfaceTexture: Boolean)
    /** Renderers released but not yet confirmed gone, by generation. */
    private val pending = HashMap<Long, Pending>()
    /** RELEASING / BLOCKED: the generation that still holds the current surface (0: stuck for good). */
    private var blockingGeneration = 0L
    /** r3 D1: the surface a timed-out decoder detach may still hold (null: none), if the app owns its SurfaceTexture,
     *  and the clean-up held back until it is done. */
    private var decoderHoldSurface: Long? = null
    private var decoderHoldActive = false
    private var decoderHoldAppOwns = false
    private val decoderHoldDeferred = ArrayList<Command>()
    /** r3 D1: the current surface was blocked by a decoder detach (cleared only by a new surface). */
    private var decoderBlocked = false
    val decoderDetachPending: Boolean get() = decoderHoldActive
    /** What should be wired once the current surface is free again. */
    private var afterRelease: Path = Path.NONE

    val pendingReleases: Int get() = pending.size

    val effective: SharpnessLevel
        get() = if (path == Path.LIVE || path == Path.STARTING) requested else SharpnessLevel.OFF

    private fun wantsFilter(): Boolean = requested.filtered && fallbackReason == null
    private fun wanted(): Path = if (wantsFilter()) Path.LIVE else Path.DIRECT

    /** A new TextureView surface exists (onSurfaceTextureAvailable). It starts unconnected. */
    fun surfaceAvailable(id: Long) {
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
    }

    /** The TextureView has a surface and the decoder should be wired to it. */
    fun bind(): List<Command> {
        if (surfaceId == null) return emptyList()
        // r3 D1: a detach still pending somewhere: no producer is connected until it is done -- and its late
        // completion does not connect one either; the next bind does.
        if (decoderHoldActive && path == Path.NONE) return emptyList()
        return when (path) {
            Path.NONE -> wire(wanted())
            Path.DIRECT, Path.STARTING, Path.LIVE -> emptyList()
            // The surface is still held by a renderer that has not confirmed it let go: remember only.
            Path.RELEASING, Path.BLOCKED -> { afterRelease = wanted(); emptyList() }
        }
    }

    /** The viewer no longer shows video (scene change): detach and let go, keep the TextureView. */
    fun unbind(): List<Command> = when (path) {
        Path.NONE -> emptyList()
        Path.DIRECT -> { path = Path.NONE; listOf(Command.DetachDecoder) }
        Path.STARTING, Path.LIVE -> releaseRenderer(after = Path.NONE)
        Path.RELEASING, Path.BLOCKED -> { afterRelease = Path.NONE; emptyList() }
    }

    /**
     * The current TextureView surface is being destroyed. Returns the commands and whether
     * onSurfaceTextureDestroyed should return true (Android releases it) or false (the app does, once the
     * renderer that may still use it has let go -- contract 4's single-release responsibility).
     */
    fun surfaceDestroyed(): Pair<List<Command>, Boolean> {
        if (surfaceId == null) return Pair(emptyList(), true)
        val result: Pair<List<Command>, Boolean> = when (path) {
            Path.NONE -> Pair(emptyList(), true)
            Path.DIRECT -> Pair(listOf(Command.DetachDecoder), true)
            Path.STARTING, Path.LIVE -> {
                val c = releaseRenderer(after = Path.NONE)
                pending[generation]?.appOwnsSurfaceTexture = true
                Pair(c, false)
            }
            Path.RELEASING, Path.BLOCKED -> {
                val holder = pending[blockingGeneration]
                if (holder != null) {
                    holder.appOwnsSurfaceTexture = true
                    Pair(emptyList(), false)
                } else {
                    // Stuck for good (the renderer reported its EGL surface was not destroyed): nothing will
                    // ever answer for this surface, so Android keeps the release (it is going anyway).
                    Pair(emptyList(), true)
                }
            }
        }
        surfaceId = null
        path = Path.NONE
        blockingGeneration = 0L
        afterRelease = Path.NONE
        decoderBlocked = false
        // A new surface is a new episode: the next one may try the filter again.
        fallbackReason = null
        return result
    }

    /** The user picked a level. */
    fun select(level: SharpnessLevel): List<Command> {
        val previous = requested
        requested = level
        if (level.filtered && previous != level) fallbackReason = null  // an explicit re-choice retries once
        return when (path) {
            Path.NONE -> emptyList()
            Path.DIRECT -> if (wantsFilter()) {
                // Producer change: the decoder leaves the TextureView first, synchronously.
                listOf(Command.DetachDecoder) + startRenderer()
            } else emptyList()
            Path.STARTING -> if (!level.filtered) releaseRenderer(after = Path.DIRECT) else emptyList()
            Path.LIVE -> if (!level.filtered) releaseRenderer(after = Path.DIRECT)
                         else listOf(Command.SetStrength(generation, level.strength))
            Path.RELEASING, Path.BLOCKED -> { afterRelease = wanted(); emptyList() }
        }
    }

    /**
     * sh-strength r3: the renderer is replaced (the drawing method changed): the current one leaves through the usual
     * release -- its release confirmed before another producer is wired -- and a new one starts after it. Nothing to
     * do when no renderer is the producer.
     */
    fun restartRenderer(): List<Command> = when (path) {
        Path.STARTING, Path.LIVE -> releaseRenderer(after = Path.LIVE)
        else -> emptyList()
    }

    fun rendererReady(gen: Long): List<Command> {
        if (gen != generation || path != Path.STARTING) return emptyList()
        path = Path.LIVE
        // The level may have moved between weak and strong while it started.
        return listOf(Command.AttachDecoderToRenderer(gen), Command.SetStrength(gen, requested.strength))
    }

    fun rendererFailed(gen: Long, reason: String): List<Command> {
        if (gen != generation || (path != Path.STARTING && path != Path.LIVE)) return emptyList()
        fallbackReason = reason
        return releaseRenderer(after = Path.DIRECT) + Command.ShowFallback(reason)
    }

    /**
     * The renderer of `gen` has finished its release. `detached`: its EGL window surface was destroyed (or
     * never made), so it is no longer a producer on that surface.
     */
    fun rendererReleased(gen: Long, detached: Boolean): List<Command> {
        val p = pending.remove(gen) ?: return emptyList()
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
        if (p.surfaceId != surfaceId || gen != blockingGeneration) {
            // A renderer of a surface that is gone: clean up that surface only. Its consumer side is released
            // even if the EGL surface could not be destroyed -- nothing can connect to a released
            // SurfaceTexture, and nothing else would ever free it.
            return if (p.appOwnsSurfaceTexture) listOf(Command.ReleaseSurfaceTexture(p.surfaceId)) else emptyList()
        }
        blockingGeneration = 0L
        if (!detached) {
            // Not confirmed free: this surface stays blocked until it goes (surfaceDestroyed).
            path = Path.BLOCKED
            return listOf(Command.ProducerStuck(gen))
        }
        path = Path.NONE
        val next = afterRelease
        afterRelease = Path.NONE
        return wire(next)
    }

    /** The renderer did not let go in time: the surface stays BLOCKED for it (never two producers). */
    fun rendererReleaseTimedOut(gen: Long): List<Command> {
        if (gen != blockingGeneration || path != Path.RELEASING) return emptyList()
        path = Path.BLOCKED
        return emptyList()
    }

    private fun wire(target: Path): List<Command> = when (target) {
        Path.DIRECT -> { path = Path.DIRECT; listOf(Command.BindDirect) }
        Path.LIVE -> if (wantsFilter()) startRenderer() else { path = Path.DIRECT; listOf(Command.BindDirect) }
        else -> { path = Path.NONE; emptyList() }
    }

    private fun startRenderer(): List<Command> {
        generation += 1
        path = Path.STARTING
        return listOf(Command.StartRenderer(generation, requested.strength))
    }

    private fun releaseRenderer(after: Path): List<Command> {
        val gen = generation
        val id = surfaceId ?: return emptyList()
        pending[gen] = Pending(id, appOwnsSurfaceTexture = false)
        path = Path.RELEASING
        blockingGeneration = gen
        afterRelease = after
        // The decoder leaves the renderer's input before the renderer goes.
        return listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen))
    }
}
