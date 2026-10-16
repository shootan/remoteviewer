package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * apk-ui r1 / quality-6m r1: the three quality levels (1500 / 3000 / 6000), three frame rates, and the
 * one-time move of old settings -- without raising a stored 3000.
 */
class ViewerQualitySettingsTest {

    @Test
    fun levels_boundaries() {
        val expect = mapOf(1499 to 1500, 1500 to 1500, 2999 to 1500, 3000 to 3000, 5999 to 3000, 6000 to 6000, 8000 to 6000)
        for ((stored, level) in expect) {
            assertEquals("stored=$stored", level, ViewerQualitySettings.migrateBitrateKbps(stored))
        }
        assertEquals(QualityLevel.LOW, QualityLevel.fromBitrateKbps(2999))
        assertEquals(QualityLevel.HIGH, QualityLevel.fromBitrateKbps(3000))
        assertEquals(QualityLevel.HIGH, QualityLevel.fromBitrateKbps(5999))
        assertEquals(QualityLevel.BEST, QualityLevel.fromBitrateKbps(6000))
        assertEquals(QualityLevel.BEST, QualityLevel.fromBitrateKbps(8000))
        // The lowest valid input of the old screen is still low.
        assertEquals(1500, ViewerQualitySettings.migrateBitrateKbps(300))
    }

    @Test
    fun bitrateMigration_invalidFallsBackToLow() {
        for (bad in listOf(null, 0, -1, 299, 1_000_001, 2_000_000)) {
            assertEquals("stored=$bad", 1500, ViewerQualitySettings.migrateBitrateKbps(bad))
        }
        // The top of the valid range is a valid value: best.
        assertEquals(6000, ViewerQualitySettings.migrateBitrateKbps(1_000_000))
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
    fun resolve_schema1_movesTheOldPresetsOnce() {
        // The free-form era's presets: 6000 and 8000 are best now (they were high before quality-6m).
        for ((stored, level) in listOf(8000 to 6000, 6000 to 6000, 3000 to 3000, 1500 to 1500)) {
            val old = ViewerQualitySettings.resolve(schema = 1, storedBitrateKbps = stored, storedFps = 15)
            assertEquals("schema1 $stored", level, old.bitrateKbps)
            assertEquals(30, old.fps)
            assertTrue(old.migrated)
        }
        // A fresh install: nothing stored, the defaults, low / 30.
        val fresh = ViewerQualitySettings.resolve(schema = 0, storedBitrateKbps = null, storedFps = null)
        assertEquals(1500, fresh.bitrateKbps)
        assertEquals(30, fresh.fps)
    }

    @Test
    fun resolve_schema2_keepsEveryLevelExactly_andNeverRaisesAStored3000() {
        // An older APK already turned 6000/8000 into 3000 on schema 2; that 3000 cannot be told apart from a
        // 3000 the user chose, so it stays 3000 -- and nothing is written.
        for ((bitrate, fps) in listOf(1500 to 30, 3000 to 45, 6000 to 60)) {
            val current = ViewerQualitySettings.resolve(schema = 2, storedBitrateKbps = bitrate, storedFps = fps)
            assertEquals(bitrate, current.bitrateKbps)
            assertEquals(fps, current.fps)
            assertFalse("schema2 $bitrate: no rewrite", current.migrated)
        }
    }

    @Test
    fun resolve_schema2_normalisesAValueOffTheChoices() {
        val odd = ViewerQualitySettings.resolve(schema = 2, storedBitrateKbps = 8000, storedFps = 50)
        assertEquals(6000, odd.bitrateKbps)
        assertEquals(45, odd.fps)
        assertTrue(odd.migrated)
        val bad = ViewerQualitySettings.resolve(schema = 2, storedBitrateKbps = 0, storedFps = 30)
        assertEquals(1500, bad.bitrateKbps)
        assertTrue(bad.migrated)
    }

    @Test
    fun resolve_isStableOverRepeatedLoadAndSave() {
        // What load writes back (the resolved values, schema 2) resolves to itself: written once, then never.
        for (start in listOf(null, 0, 1499, 1500, 2999, 3000, 5999, 6000, 8000, 2_000_000)) {
            var schema = 1
            var bitrate: Int? = start
            var fps: Int? = 50
            var writes = 0
            repeat(4) {
                val r = ViewerQualitySettings.resolve(schema, bitrate, fps)
                if (r.migrated || schema < ViewerQualitySettings.CURRENT_SCHEMA) {
                    writes++
                    schema = ViewerQualitySettings.CURRENT_SCHEMA
                    bitrate = r.bitrateKbps
                    fps = r.fps
                }
            }
            assertEquals("start=$start: written once", 1, writes)
            assertEquals(ViewerQualitySettings.migrateBitrateKbps(start), bitrate)
        }
    }

    @Test
    fun best_pickedAndReloaded_staysBest() {
        // A pick stores level.bitrateKbps (MainActivity.selectQuality -> SessionPersistence): the next start reads 6000.
        val reloaded = ViewerQualitySettings.resolve(schema = 2, storedBitrateKbps = QualityLevel.BEST.bitrateKbps, storedFps = 30)
        assertEquals(6000, reloaded.bitrateKbps)
        assertEquals(QualityLevel.BEST, QualityLevel.fromBitrateKbps(reloaded.bitrateKbps))
        assertFalse(reloaded.migrated)
        // What the session is asked for: bps, not kbps.
        assertEquals(6_000_000, QualityLevel.BEST.bitrateKbps * 1000)
    }

    @Test
    fun qualityLevels_areExactlyTheThreeTheUserChose() {
        assertEquals(listOf(1500, 3000, 6000), QualityLevel.values().map { it.bitrateKbps })
        assertEquals(QualityLevel.LOW, ViewerQualitySettings.DEFAULT_QUALITY)
        assertEquals(2, ViewerQualitySettings.CURRENT_SCHEMA)
        assertEquals(listOf(30, 45, 60), ViewerQualitySettings.FPS_CHOICES)
        assertEquals(30, ViewerQualitySettings.DEFAULT_FPS)
    }
}
