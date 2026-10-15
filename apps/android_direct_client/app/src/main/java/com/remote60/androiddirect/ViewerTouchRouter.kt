package com.remote60.androiddirect

/**
 * viewer-sharpness r2 R1: who owns one touch stream on the picture -- decided at its ACTION_DOWN and kept until its
 * final ACTION_UP / ACTION_CANCEL, whatever changes in between.
 *
 * - LOCAL: the stream began in 크게보기 with 화면 이동 on. Every later event of it moves / scales the picture, even
 *   after a pinch below the fit has gone back to 전체보기 -- a finger put down again (ACTION_POINTER_DOWN, no new
 *   ACTION_DOWN) is not a click, and two fingers moving under a tablet lock are not Ctrl+wheel. (bf0e3a2 checked the
 *   mode on every event: both reached the PC.)
 * - PC: the ordinary remote input path.
 * - SWALLOW: a PC stream cut by a mode / size / scene change. The caller sends the release once ([interrupt]); the
 *   rest of that stream reaches nobody, so a held finger does not press again.
 * The next ACTION_DOWN starts the next stream in whatever mode is current then.
 *
 * The action values are MotionEvent's (ACTION_DOWN 0, UP 1, MOVE 2, CANCEL 3, POINTER_DOWN 5, POINTER_UP 6), kept here so
 * this runs in a plain unit test.
 */
class ViewerTouchRouter {
    enum class Owner { NONE, PC, LOCAL, SWALLOW }

    var owner = Owner.NONE
        private set

    /** The route for this event. [localNow] = 크게보기 with 화면 이동 on, read only at ACTION_DOWN. */
    fun route(actionMasked: Int, localNow: Boolean): Owner {
        if (actionMasked == ACTION_DOWN) {
            owner = if (localNow) Owner.LOCAL else Owner.PC
        } else if (owner == Owner.NONE) {
            // A stream whose ACTION_DOWN this view never saw (it began elsewhere): nobody's.
            owner = Owner.SWALLOW
        }
        val routed = owner
        if (actionMasked == ACTION_UP || actionMasked == ACTION_CANCEL) owner = Owner.NONE
        return routed
    }

    /**
     * A mode / size / scene change now. true = a PC stream was in progress: the caller releases what it pressed (once);
     * the rest of the stream is swallowed. A LOCAL stream stays local until it ends.
     */
    fun interrupt(): Boolean {
        if (owner != Owner.PC) return false
        owner = Owner.SWALLOW
        return true
    }

    companion object {
        const val ACTION_DOWN = 0
        const val ACTION_UP = 1
        const val ACTION_MOVE = 2
        const val ACTION_CANCEL = 3
        const val ACTION_POINTER_DOWN = 5
        const val ACTION_POINTER_UP = 6
    }
}
