package com.remote60.androiddirect

/**
 * How a pending target selection reads the native panel's status token (t-970r4zgo r3).
 *
 * The native session shows a select answer in that token only when it belongs to the selection
 * now in progress (a late answer to an earlier pick is not shown), so this is the whole decision:
 * a failure token ends the pending selection, a selected token is its acknowledgement, anything
 * else -- the request still in flight, a list being fetched -- changes nothing.
 */
object SelectionStatusPolicy {
    enum class Verdict { NONE, ACKED, FAILED }

    fun verdict(status: String): Verdict = when {
        status.startsWith("window_select_failed") -> Verdict.FAILED
        status.startsWith("window_selected") -> Verdict.ACKED
        else -> Verdict.NONE
    }
}
