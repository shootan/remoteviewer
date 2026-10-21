package com.remote60.androiddirect

/**
 * sh-filter r2 (Codex H2/H3): frames are told apart by WHICH frame they are, not by counting.
 *
 * Every frame the decoder hands to a Surface carries its timestamp (the decoder's release stamps the buffer,
 * and the sharpening pass passes it on with eglPresentationTimeANDROID). So the TextureView's latched
 * SurfaceTexture timestamp names the frame, and the native ring (nativeHandoffInfo) says when it was handed
 * over, from which stream (selection) generation and codec instance, and its visible size. A redraw of the
 * frame already shown (a strength change) re-swaps the SAME timestamp. Pure: no Android types.
 */
data class FrameInfo(
    val handoffNs: Long,
    val streamGeneration: Long,
    val codecEpoch: Long,
    val visibleWidth: Int,
    val visibleHeight: Int,
) {
    companion object {
        /** From nativeHandoffInfo's [handoffNs, streamGeneration, codecEpoch, width, height] (null: unknown). */
        fun of(values: LongArray?): FrameInfo? =
            if (values == null || values.size < 5) null
            else FrameInfo(values[0], values[1], values[2], values[3].toInt(), values[4].toInt())
    }
}

/**
 * A latch is a NEW frame when its timestamp differs from the last one latched on the same surface;
 * otherwise it is a redraw of what is already shown. Unlike r1's "subtract one pending redraw per swap",
 * this does not depend on how many swaps the consumer coalesced into one latch.
 */
class LatchClassifier {
    private var surfaceId: Long? = null
    private var lastTimestamp = Long.MIN_VALUE

    /** true: a new frame on screen; false: the same frame again (a redraw). */
    fun isNewFrame(surfaceId: Long, timestampNs: Long): Boolean {
        if (surfaceId != this.surfaceId) {
            this.surfaceId = surfaceId
            lastTimestamp = timestampNs
            return true
        }
        if (timestampNs == lastTimestamp) return false
        lastTimestamp = timestampNs
        return true
    }
}

/**
 * Contract 6 / r2 H2: with the sharpening pass in between, "ready" is a frame of THIS renderer, on THIS
 * surface, decoded by the codec started for it, actually latched by the view -- whenever that latch happens
 * (before or after anyone looks). Armed when the decoder is attached to the renderer; confirmed by the first
 * latch that qualifies; a selection is confirmed only by a qualifying latch of ITS stream generation.
 */
class SharpenOutputGate {
    private var armed = false
    private var rendererGeneration = 0L
    private var surfaceId = 0L
    private var attachNs = 0L
    private var codecEpochAtAttach = 0L
    var armedAtMs = 0L
        private set
    var confirmed = false
        private set
    /** The stream generation of the newest qualifying latch (0: none yet). */
    var lastQualifiedStreamGeneration = 0L
        private set

    fun arm(rendererGeneration: Long, surfaceId: Long, attachNs: Long, codecEpochAtAttach: Long, nowMs: Long) {
        armed = true
        confirmed = false
        lastQualifiedStreamGeneration = 0L
        this.rendererGeneration = rendererGeneration
        this.surfaceId = surfaceId
        this.attachNs = attachNs
        this.codecEpochAtAttach = codecEpochAtAttach
        armedAtMs = nowMs
    }

    fun cancel() {
        armed = false
        confirmed = false
        lastQualifiedStreamGeneration = 0L
    }

    val isArmed: Boolean get() = armed

    /** For an armed gate: is this latch a frame of the armed renderer/surface/codec? */
    fun qualifies(rendererGeneration: Long, surfaceId: Long, newFrame: Boolean, info: FrameInfo?): Boolean =
        armed && newFrame && info != null &&
            rendererGeneration == this.rendererGeneration && surfaceId == this.surfaceId &&
            info.handoffNs >= attachNs && info.codecEpoch > codecEpochAtAttach

    /** A latch happened. Returns true exactly when it is the one that first confirms the gate. */
    fun onLatch(rendererGeneration: Long, surfaceId: Long, newFrame: Boolean, info: FrameInfo?): Boolean {
        if (!qualifies(rendererGeneration, surfaceId, newFrame, info)) return false
        lastQualifiedStreamGeneration = info!!.streamGeneration
        if (confirmed) return false
        confirmed = true
        return true
    }

    /** Is the armed renderer's output confirmed for this selection's stream generation? */
    fun confirmedFor(streamGeneration: Long): Boolean =
        armed && confirmed && (streamGeneration == 0L || lastQualifiedStreamGeneration == streamGeneration)

    fun timedOut(nowMs: Long, limitMs: Long): Boolean = armed && !confirmed && nowMs - armedAtMs > limitMs
}
