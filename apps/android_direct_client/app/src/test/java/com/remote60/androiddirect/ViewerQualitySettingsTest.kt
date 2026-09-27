package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** apk-ui r1: the two quality levels, three frame rates, and the one-time move of old settings. */
class ViewerQualitySettingsTest {

    @Test
    fun bitrateMigration_mapsTheOldPresetsAndInputs() {
        // The old presets and the old default.
        assertEquals(3000, ViewerQualitySettings.migrateBitrateKbps(8000))
        assertEquals(3000, ViewerQualitySettings.migrateBitrateKbps(6000))
        assertEquals(3000, ViewerQualitySettings.migrateBitrateKbps(3000))
        // Just below the line is low.
        assertEquals(1500, ViewerQualitySettings.migrateBitrateKbps(2999))
        assertEquals(1500, ViewerQualitySettings.migrateBitrateKbps(1500))
        assertEquals(1500, ViewerQualitySettings.migrateBitrateKbps(300))
    }

    @Test
    fun bitrateMigration_invalidFallsBackToLow() {
        for (bad in listOf(null, 0, -1, 299, 2_000_000)) {
            assertEquals("stored=$bad", 1500, ViewerQualitySettings.migrateBitrateKbps(bad))
        }
    }

    @Test
    fun fpsMigration_keepsTheChoicesAndRoundsTheRest() {
        assertEquals(30, ViewerQualitySettings.migrateFps(30))
        assertEquals(45, ViewerQualitySettings.migrateFps(45))
        assertEquals(60, ViewerQualitySettings.migrateFps(60))
        // Nearest.
        assertEquals(30, ViewerQualitySettings.migrateFps(15))
        assertEquals(30, ViewerQualitySettings.migrateFps(24))
        assertEquals(45, ViewerQualitySettings.migrateFps(40))
        assertEquals(45, ViewerQualitySettings.migrateFps(50))
        assertEquals(60, ViewerQualitySettings.migrateFps(55))
        assertEquals(60, ViewerQualitySettings.migrateFps(120))
        assertEquals(45, ViewerQualitySettings.nearestFps(52))  // 7 from 45, 8 from 60
        assertEquals(60, ViewerQualitySettings.nearestFps(53))  // 8 from 45, 7 from 60
    }

    @Test
    fun fpsMigration_tieGoesToTheLowerRate() {
        // 30/45/60 have no integer midpoint (37.5, 52.5), so both sides of each are pinned ...
        assertEquals(30, ViewerQualitySettings.nearestFps(37))
        assertEquals(45, ViewerQualitySettings.nearestFps(38))
        // ... and the tie rule itself on a list that has one: 40 is 10 from both 30 and 50.
        assertEquals(30, ViewerQualitySettings.nearestFps(40, listOf(30, 50)))
        assertEquals(30, ViewerQualitySettings.nearestFps(40, listOf(50, 30)))
    }

    @Test
    fun fpsMigration_invalidIsThirty() {
        for (bad in listOf(null, 0, -5, 121, 1000)) {
            assertEquals("stored=$bad", 30, ViewerQualitySettings.migrateFps(bad))
        }
    }

    @Test
    fun resolve_migratesOnceFromTheOldSchema() {
        val old = ViewerQualitySettings.resolve(schema = 1, storedBitrateKbps = 8000, storedFps = 15)
        assertEquals(3000, old.bitrateKbps)
        assertEquals(30, old.fps)
        assertTrue(old.migrated)

        // A fresh install: nothing stored, the defaults, low / 30.
        val fresh = ViewerQualitySettings.resolve(schema = 0, storedBitrateKbps = null, storedFps = null)
        assertEquals(1500, fresh.bitrateKbps)
        assertEquals(30, fresh.fps)

        // Already migrated and valid: left exactly as it is, nothing to write.
        val current = ViewerQualitySettings.resolve(schema = 2, storedBitrateKbps = 3000, storedFps = 45)
        assertEquals(3000, current.bitrateKbps)
        assertEquals(45, current.fps)
        assertFalse(current.migrated)
    }

    @Test
    fun resolve_normalisesAnImpossibleValueEvenOnTheNewSchema() {
        val odd = ViewerQualitySettings.resolve(schema = 2, storedBitrateKbps = 8000, storedFps = 50)
        assertEquals(3000, odd.bitrateKbps)
        assertEquals(45, odd.fps)
        assertTrue(odd.migrated)
    }

    @Test
    fun qualityLevels_areExactlyTheTwoTheUserChose() {
        assertEquals(listOf(1500, 3000), QualityLevel.values().map { it.bitrateKbps })
        assertEquals(listOf(30, 45, 60), ViewerQualitySettings.FPS_CHOICES)
        assertEquals(QualityLevel.HIGH, QualityLevel.fromBitrateKbps(3000))
        assertEquals(QualityLevel.LOW, QualityLevel.fromBitrateKbps(1500))
    }
}
