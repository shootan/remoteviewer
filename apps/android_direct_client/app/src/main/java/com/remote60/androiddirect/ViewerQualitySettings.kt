package com.remote60.androiddirect

/**
 * The three picture choices and three frame rates the viewer offers, and the one-time move of the
 * old free-form settings onto them.
 *
 * History: the user's call of 2026-09-27 was "화질은 저화질 1500 / 고화질 3000 두 개만", after 1500
 * and 6000 looked the same on the phone that day (both 1080p, the same PC). That was one observation
 * and the choice made from it, not a rule that the two always look alike. Whether the mobile ABR's
 * false step-downs of that time (fixed since) coloured it is a hypothesis the logs of the day cannot
 * settle. The user's call of 2026-10-15 ("다시넣어") brought 6000 back as a third choice.
 *
 * What a choice means: the user's ceiling on the video line budget the host aims at (R). The host
 * decides the encoder payload (E) within it, and its ABR still adapts below the ceiling -- so a
 * choice is neither the encoder's fixed target nor a promise about the app's total traffic.
 */
enum class QualityLevel(val bitrateKbps: Int) {
    LOW(1500),
    HIGH(3000),
    BEST(6000);

    companion object {
        /** The level a stored bitrate means: 6000 and above is best, 3000 and above high, anything valid below low. */
        fun fromBitrateKbps(kbps: Int): QualityLevel = when {
            kbps >= BEST.bitrateKbps -> BEST
            kbps >= HIGH.bitrateKbps -> HIGH
            else -> LOW
        }
    }
}

object ViewerQualitySettings {
    val FPS_CHOICES = listOf(30, 45, 60)
    val DEFAULT_QUALITY = QualityLevel.LOW
    const val DEFAULT_FPS = 30

    /** Schema of the stored bitrate/fps: 1 = free-form numbers (APK 0.2.20 and earlier), 2 = the choices above. */
    const val CURRENT_SCHEMA = 2

    /**
     * A stored bitrate as one of the three levels. Valid means what the old settings screen
     * accepted (>= 300 kbps); anything else -- unset, zero, negative, absurd -- falls back to the
     * default. A 3000 stays 3000: an older APK already turned 6000/8000 into 3000, and that cannot
     * be told apart from a 3000 the user chose, so nothing is raised back (quality-6m r1).
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
