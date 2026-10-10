package com.remote60.androiddirect

/**
 * Which way the screen is held outside the viewer (apk-keyboard-tabs r2 item 2).
 *
 * The viewer turns the device to the shape of the remote screen (a desktop locks it landscape).
 * Leaving it used to undo that only on one path (moveToTargets), and only to "unspecified", so a
 * disconnect, an error or a lost host left a phone stuck in landscape on the host list, and even
 * the released state stayed landscape while the phone was held that way. Every scene other than
 * the viewer now gets the decision below, applied wherever the scene is rendered, so no exit path
 * can be missed.
 *
 * Phones go back to portrait: the lists and the login screen are laid out for it, and that is what
 * the user asked for ("데스크톱 나가면 다시 세로로"). Tablets (smallest width 600dp and up, e.g. the
 * SM-T975N) get the lock released instead (UNSPECIFIED): the system picks the orientation by its
 * own and the user's rotation settings -- not a promise to keep whatever way it is held. Their
 * natural way is landscape, often in a keyboard cover, and the same screens fit there, so forcing
 * them upright would be the new annoyance.
 */
object SceneOrientationPolicy {
    enum class Request {
        /** The viewer decides from the content (applyOrientationForContent); leave it alone. */
        VIEWER_DECIDES,
        /** SCREEN_ORIENTATION_SENSOR_PORTRAIT. */
        PORTRAIT,
        /** SCREEN_ORIENTATION_UNSPECIFIED: the system's (and the user's rotation setting's) choice. */
        SYSTEM_DECIDES,
    }

    const val TABLET_SMALLEST_WIDTH_DP = 600

    fun forScene(inViewer: Boolean, smallestScreenWidthDp: Int): Request = when {
        inViewer -> Request.VIEWER_DECIDES
        smallestScreenWidthDp >= TABLET_SMALLEST_WIDTH_DP -> Request.SYSTEM_DECIDES
        else -> Request.PORTRAIT
    }
}
