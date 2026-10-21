package com.remote60.androiddirect

/**
 * Who writes into the viewer's TextureView, and how that changes (sh-filter r1, Codex contract 3/4/7).
 *
 * Two producers can feed the TextureView's BufferQueue: the decoder directly (OFF, and every fallback),
 * or the sharpening renderer's EGL window surface (the decoder then writes into the renderer's own input
 * SurfaceTexture). They must never be connected at once, so every change of producer is serialized:
 *
 *   direct  -> filter : the decoder leaves the TextureView (nativeSetSurface(null) -- a codec reset, done
 *                       synchronously by the native side) and its Surface is released; THEN the renderer
 *                       is started on the TextureView; the decoder is bound to the renderer's input only
 *                       when the renderer says it is up, for that same generation.
 *   filter -> direct  : the decoder leaves the renderer's input (nativeSetSurface(null)); the renderer is
 *                       released on its own thread; the direct Surface is made only when the renderer
 *                       reports it has let go of the TextureView. If it never does, nothing is connected:
 *                       a black picture is better than two producers.
 *   weak  <-> strong  : no producer change -- a new strength and one redraw of the frame already shown.
 *
 * Every renderer event carries the generation it was started with; anything from an older generation is
 * ignored. A renderer failure falls back to direct and LATCHES for this surface episode (until a new
 * TextureView surface, or the user picks a filtered level again) -- no retry loop, and the user's choice
 * is kept as requested (requested=weak|strong, effective=off).
 *
 * Pure: no Android types. MainActivity executes the commands; SharpenPathPolicyTest drives it.
 */
class SharpenPathPolicy(requested: SharpnessLevel) {

    enum class Path { NONE, DIRECT, STARTING, LIVE, RELEASING }

    sealed class Command {
        /** Make the direct Surface on the TextureView and give it to the decoder. */
        object BindDirect : Command()
        /** nativeSetSurface(null) and release whatever Surface the decoder had. */
        object DetachDecoder : Command()
        /** Start a renderer on the TextureView; it reports Ready/Failed/Released with this generation. */
        data class StartRenderer(val generation: Long, val strength: Float) : Command()
        /** Give the renderer's input Surface (of this generation) to the decoder. */
        data class AttachDecoderToRenderer(val generation: Long) : Command()
        /** Release the renderer of this generation (posted to its thread; it answers Released). */
        data class ReleaseRenderer(val generation: Long, val thenReleaseSurfaceTexture: Boolean) : Command()
        data class SetStrength(val generation: Long, val strength: Float) : Command()
        /** Tell the user, briefly: 선명도 적용 불가, 원본 표시. */
        data class ShowFallback(val reason: String) : Command()
        /** The app owns the TextureView's SurfaceTexture now (onSurfaceTextureDestroyed returned false). */
        object ReleaseSurfaceTexture : Command()
    }

    var requested: SharpnessLevel = requested
        private set
    var path: Path = Path.NONE
        private set
    var generation: Long = 0L
        private set
    /** Set by a renderer failure; clears with a new surface episode or a new filtered choice. */
    var fallbackReason: String? = null
        private set
    /** What should be wired once the current renderer has let go (RELEASING). */
    private var afterRelease: Path = Path.NONE
    private var releaseOwnsSurfaceTexture = false

    val effective: SharpnessLevel
        get() = if (path == Path.LIVE || path == Path.STARTING) requested else SharpnessLevel.OFF

    private fun wantsFilter(): Boolean = requested.filtered && fallbackReason == null

    /** The TextureView has a surface and the decoder should be wired to it. */
    fun bind(): List<Command> = when (path) {
        Path.NONE -> if (wantsFilter()) startRenderer() else { path = Path.DIRECT; listOf(Command.BindDirect) }
        // Already wired or starting: nothing new.
        Path.DIRECT, Path.STARTING, Path.LIVE -> emptyList()
        // Still letting the old renderer go: wire again once it has (unless the surface itself is going).
        Path.RELEASING -> {
            if (!releaseOwnsSurfaceTexture) afterRelease = if (wantsFilter()) Path.LIVE else Path.DIRECT
            emptyList()
        }
    }

    /** The viewer no longer shows video (scene change): detach and let go, keep the TextureView. */
    fun unbind(): List<Command> = when (path) {
        Path.NONE -> emptyList()
        Path.DIRECT -> { path = Path.NONE; listOf(Command.DetachDecoder) }
        Path.STARTING, Path.LIVE -> releaseRenderer(after = Path.NONE, ownsSurfaceTexture = false)
        Path.RELEASING -> { afterRelease = Path.NONE; emptyList() }
    }

    /**
     * The TextureView's surface is being destroyed. Returns the commands and whether
     * onSurfaceTextureDestroyed should return true (Android releases it) or false (the app does,
     * once the renderer has let go -- contract 4's single-release responsibility).
     */
    fun surfaceDestroyed(): Pair<List<Command>, Boolean> {
        val result = when (path) {
            Path.NONE -> Pair(emptyList<Command>(), true)
            Path.DIRECT -> { path = Path.NONE; Pair(listOf(Command.DetachDecoder), true) }
            Path.STARTING, Path.LIVE -> Pair(releaseRenderer(after = Path.NONE, ownsSurfaceTexture = true), false)
            Path.RELEASING -> {
                afterRelease = Path.NONE
                releaseOwnsSurfaceTexture = true
                Pair(emptyList<Command>(), false)
            }
        }
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
            Path.STARTING -> if (!level.filtered) releaseRenderer(after = Path.DIRECT, ownsSurfaceTexture = false)
                             else emptyList()  // the strength goes with the Ready
            Path.LIVE -> if (!level.filtered) releaseRenderer(after = Path.DIRECT, ownsSurfaceTexture = false)
                         else listOf(Command.SetStrength(generation, level.strength))
            Path.RELEASING -> { afterRelease = if (wantsFilter()) Path.LIVE else Path.DIRECT; emptyList() }
        }
    }

    fun rendererReady(gen: Long): List<Command> {
        if (gen != generation || path != Path.STARTING) return emptyList()
        path = Path.LIVE
        val out = mutableListOf<Command>(Command.AttachDecoderToRenderer(gen))
        // The level may have moved between weak and strong while it started.
        out += Command.SetStrength(gen, requested.strength)
        return out
    }

    fun rendererFailed(gen: Long, reason: String): List<Command> {
        if (gen != generation || (path != Path.STARTING && path != Path.LIVE)) return emptyList()
        fallbackReason = reason
        return releaseRenderer(after = Path.DIRECT, ownsSurfaceTexture = false) + Command.ShowFallback(reason)
    }

    /** The renderer of `gen` has destroyed its EGL surface: the TextureView has no producer now. */
    fun rendererReleased(gen: Long): List<Command> {
        if (gen != generation || path != Path.RELEASING) return emptyList()
        val out = mutableListOf<Command>()
        if (releaseOwnsSurfaceTexture) {
            out += Command.ReleaseSurfaceTexture
            releaseOwnsSurfaceTexture = false
            path = Path.NONE
            return out
        }
        when (afterRelease) {
            Path.DIRECT -> { path = Path.DIRECT; out += Command.BindDirect }
            Path.LIVE -> out += startRenderer()
            else -> path = Path.NONE
        }
        afterRelease = Path.NONE
        return out
    }

    /** The renderer did not let go in time: leave the TextureView unconnected (never two producers). */
    fun rendererReleaseTimedOut(gen: Long): List<Command> {
        if (gen != generation || path != Path.RELEASING) return emptyList()
        path = Path.NONE
        afterRelease = Path.NONE
        return emptyList()
    }

    private fun startRenderer(): List<Command> {
        generation += 1
        path = Path.STARTING
        return listOf(Command.StartRenderer(generation, requested.strength))
    }

    private fun releaseRenderer(after: Path, ownsSurfaceTexture: Boolean): List<Command> {
        val gen = generation
        path = Path.RELEASING
        afterRelease = after
        releaseOwnsSurfaceTexture = ownsSurfaceTexture
        // The decoder leaves the renderer's input before the renderer goes.
        return listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen, ownsSurfaceTexture))
    }
}
