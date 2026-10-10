package com.remote60.androiddirect

import com.remote60.androiddirect.SceneOrientationPolicy.Request
import org.junit.Assert.assertEquals
import org.junit.Test

class SceneOrientationPolicyTest {
    private val phoneDp = 384 // SM-S948N class
    private val tabletDp = 800 // SM-T975N

    /** MainActivity's scenes and whether each is the viewer, as applySceneVisibility reads them. */
    private val scenes = mapOf(
        "LOGIN" to false, "HOSTS" to false, "CONNECT" to false, "TARGETS" to false,
        "SWITCHING" to true, "VIEWER" to true,
    )

    @Test
    fun everySceneOutsideTheViewerTurnsAPhoneBackUpright() {
        for ((scene, inViewer) in scenes) {
            val want = if (inViewer) Request.VIEWER_DECIDES else Request.PORTRAIT
            assertEquals(scene, want, SceneOrientationPolicy.forScene(inViewer, phoneDp))
        }
    }

    @Test
    fun theViewerKeepsItsOwnContentDecision() {
        // A desktop locked landscape at selection must not be undone while switching or viewing.
        assertEquals(Request.VIEWER_DECIDES, SceneOrientationPolicy.forScene(inViewer = true, phoneDp))
        assertEquals(Request.VIEWER_DECIDES, SceneOrientationPolicy.forScene(inViewer = true, tabletDp))
    }

    @Test
    fun exitPathsAllLandOnAPortraitPhone() {
        // Disconnect -> HOSTS, an error -> CONNECT, Back or a lost stream -> TARGETS, sign-out -> LOGIN.
        // Each is a scene, not a call site, so the request does not depend on which code got there.
        for (exitScene in listOf("HOSTS", "CONNECT", "TARGETS", "LOGIN")) {
            assertEquals(exitScene, Request.PORTRAIT, SceneOrientationPolicy.forScene(scenes.getValue(exitScene), phoneDp))
        }
    }

    @Test
    fun aTabletIsLeftToTheWayItIsHeld() {
        assertEquals(Request.FOLLOW_DEVICE, SceneOrientationPolicy.forScene(inViewer = false, tabletDp))
        assertEquals(Request.FOLLOW_DEVICE, SceneOrientationPolicy.forScene(inViewer = false, SceneOrientationPolicy.TABLET_SMALLEST_WIDTH_DP))
        assertEquals(Request.PORTRAIT, SceneOrientationPolicy.forScene(inViewer = false, SceneOrientationPolicy.TABLET_SMALLEST_WIDTH_DP - 1))
    }
}
