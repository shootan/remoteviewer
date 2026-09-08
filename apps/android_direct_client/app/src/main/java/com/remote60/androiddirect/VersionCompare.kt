package com.remote60.androiddirect

/**
 * Version comparison -- the Kotlin side of a contract three runtimes have to agree on.
 *
 * The directory server publishes a manifest version; the Windows updater (C++) and this client
 * each decide from it whether they are behind. Nothing can be shared across those three but the
 * answers, so the answers are pinned in `apps/shared/version_compare_vectors.txt` and all three
 * test suites read that same file. The contract and the reasoning for each rule are written at
 * the top of it. See `docs/업데이트_기능_설계.md` 2(d).
 */
object VersionCompare {

    /**
     * The ceiling a single component saturates at, so the three runtimes cannot disagree by
     * overflowing differently. A Kotlin Long would not overflow here, which is exactly why the
     * clamp has to be explicit.
     */
    const val COMPONENT_MAX: Long = 2147483647L

    /**
     * Negative when [a] is older, 0 when equal, positive when [a] is newer.
     *
     * A null arrives as the empty string rather than throwing: this reads a manifest field that
     * came over the wire, and "missing version" has to compare as older, not crash.
     */
    @JvmStatic
    fun compare(a: String?, b: String?): Int {
        val left = a ?: ""
        val right = b ?: ""

        var i = 0
        var j = 0
        while (i < left.length || j < right.length) {
            // A run of digits is one component. No digits here -- end of string, a separator
            // straight away, or a non-numeric character -- reads as zero, which is what makes
            // "0.1" and "0.1.0" compare equal and ".1" equal "0.1".
            var lv = 0L
            while (i < left.length && left[i].isAsciiDigit()) {
                lv = minOf(lv * 10 + (left[i].code - '0'.code), COMPONENT_MAX)
                i++
            }
            var rv = 0L
            while (j < right.length && right[j].isAsciiDigit()) {
                rv = minOf(rv * 10 + (right[j].code - '0'.code), COMPONENT_MAX)
                j++
            }
            if (lv != rv) return if (lv < rv) -1 else 1

            if (i < left.length && left[i] == '.') i++
            if (j < right.length && right[j] == '.') j++

            // Anything that is not a digit or a separator ends the comparison: a suffix like
            // "-beta" or "+build3" can never decide the outcome, in either direction.
            if ((i < left.length && !left[i].isAsciiDigit()) ||
                (j < right.length && !right[j].isAsciiDigit())
            ) {
                break
            }
        }
        return 0
    }

    /** Deliberately not [Char.isDigit], which accepts non-ASCII digits the contract does not. */
    private fun Char.isAsciiDigit(): Boolean = this in '0'..'9'
}
