package com.remote60.androiddirect

/**
 * The two picture choices and three frame rates the viewer offers, and the one-time move of the
 * old free-form settings onto them.
 *
 * The user's call (2026-09-27): "화질은 저화질 1500 / 고화질 3000 두 개만", "프레임은 30 / 45 / 60".
 * Measured the same day on the same PC: 1500 and 6000 looked the same on the phone (both 1080p),
 * so the old 6000/8000 presets bought traffic and nothing visible.
 *
 * These are the encoder's targets, not a cap on what crosses the network -- FEC, retransmits and
 * headers come on top -- so nothing here promises a data figure.
 */
enum class QualityLevel(val bitrateKbps: Int) {
    LOW(1500),
    HIGH(3000);

    companion object {
        /** The level a stored bitrate means: 3000 and above is high, anything valid below is low. */
        fun fromBitrateKbps(kbps: Int): QualityLevel = if (kbps >= HIGH.bitrateKbps) HIGH else LOW
    }
}

object ViewerQualitySettings {
    val FPS_CHOICES = listOf(30, 45, 60)
    val DEFAULT_QUALITY = QualityLevel.LOW
    const val DEFAULT_FPS = 30

    /** Schema of the stored bitrate/fps: 1 = free-form numbers (APK 0.2.20 and earlier), 2 = the choices above. */
    const val CURRENT_SCHEMA = 2

    /**
     * A stored bitrate from the free-form era as one of the two levels. Valid means what the old
     * settings screen accepted (>= 300 kbps); anything else -- unset, zero, negative, absurd --
     * falls back to the default.
     */
    fun migrateBitrateKbps(stored: Int?): Int {
        if (stored == null || stored < 300 || stored > 1_000_000) return DEFAULT_QUALITY.bitrateKbps
        return QualityLevel.fromBitrateKbps(stored).bitrateKbps
    }

    /**
     * A stored fps as one of 30/45/60: kept when it already is one, otherwise the nearest, and on a
     * tie the lower (the cheaper) one. Invalid (outside what the old screen accepted, 1..120) is 30.
     */
    fun migrateFps(stored: Int?): Int {
        if (stored == null || stored !in 1..120) return DEFAULT_FPS
        return nearestFps(stored)
    }

    /** The nearest of `choices`; on equal distance the lower one. */
    fun nearestFps(fps: Int, choices: List<Int> = FPS_CHOICES): Int =
        choices.minWithOrNull(compareBy<Int>({ kotlin.math.abs(it - fps) }, { it })) ?: DEFAULT_FPS

    /** What a load does: migrate once when the stored schema is older, otherwise trust the values. */
    data class Resolved(val bitrateKbps: Int, val fps: Int, val migrated: Boolean)

    fun resolve(schema: Int, storedBitrateKbps: Int?, storedFps: Int?): Resolved {
        if (schema >= CURRENT_SCHEMA) {
            // Already on the choices -- but a value that is not one of them (a hand-edited or
            // corrupted file) is still normalised, so the UI never shows an impossible selection.
            val bitrate = migrateBitrateKbps(storedBitrateKbps)
            val fps = migrateFps(storedFps)
            return Resolved(bitrate, fps, bitrate != storedBitrateKbps || fps != storedFps)
        }
        return Resolved(migrateBitrateKbps(storedBitrateKbps), migrateFps(storedFps), true)
    }
}
