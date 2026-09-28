package com.remote60.androiddirect

/**
 * Two-set (두벌식) Hangul composition for the PC keyboard panel (apk-ui r3).
 *
 * Why here and not on the PC: the host resets the target's IME to alphanumeric before each key it
 * injects (host_input_inject.cpp ensure_foreground_ime_alphanumeric), so a VK_HANGUL toggle does
 * not survive to the next key. The PC viewer composes Hangul locally and sends finished text; this
 * does the same on the phone. Only finished text leaves [input] / [flush]; the syllable still being
 * built is [preview].
 *
 * Handles: consonant+vowel(+final), compound vowels (ㅘ ㅙ ㅚ ㅝ ㅞ ㅟ ㅢ), compound finals
 * (ㄳ ㄵ ㄶ ㄺ ㄻ ㄼ ㄽ ㄾ ㄿ ㅀ ㅄ), a final moving to the next syllable when a vowel follows
 * (닭+ㅇ... / 달+ㄱ+ㅡ → 달그), and step-by-step backspace inside the syllable being built.
 */
class HangulComposer {
    private data class State(val cho: Int = -1, val jung: Int = -1, val jong: Int = 0)

    private var state = State()
    /** Earlier states of the syllable being built, for backspace one step at a time. */
    private val history = ArrayDeque<State>()

    val isComposing: Boolean get() = state != State()

    /** The syllable (or lone jamo) being built; empty when nothing is. */
    val preview: String get() = render(state)

    /** Feeds one jamo (compatibility jamo, e.g. 'ㄱ', 'ㅏ'); returns text that is now final. */
    fun input(jamo: Char): String {
        val cho = CHO.indexOf(jamo)
        val jung = JUNG.indexOf(jamo)
        return when {
            jung >= 0 -> inputVowel(jung)
            cho >= 0 -> inputConsonant(jamo)
            // Anything else (a compound final typed on its own, or not a jamo) ends the syllable.
            else -> flush() + jamo
        }
    }

    /** Finishes whatever is being built and returns it. */
    fun flush(): String {
        val out = render(state)
        state = State()
        history.clear()
        return out
    }

    /** Undoes one step of the syllable being built; false when there was nothing to undo. */
    fun backspace(): Boolean {
        if (!isComposing) return false
        state = if (history.isEmpty()) State() else history.removeLast()
        return true
    }

    private fun push(next: State) {
        history.addLast(state)
        state = next
    }

    /** Starts a new syllable from [next], returning the old one as final text. */
    private fun commitAndStart(next: State): String {
        val out = render(state)
        history.clear()
        state = State()
        push(next)
        return out
    }

    private fun inputConsonant(c: Char): String {
        val s = state
        val choIndex = CHO.indexOf(c)
        return when {
            // Nothing yet, or only a vowel standing alone: this consonant starts a new syllable.
            s.cho < 0 && s.jung < 0 -> { push(State(cho = choIndex)); "" }
            s.jung < 0 -> commitAndStart(State(cho = choIndex))
            s.cho < 0 -> commitAndStart(State(cho = choIndex))
            s.jong == 0 -> {
                val jong = JONG.indexOf(c)
                if (jong > 0) { push(s.copy(jong = jong)); "" } else commitAndStart(State(cho = choIndex))
            }
            else -> {
                val combined = COMPOUND_JONG[JONG[s.jong].toString() + c]
                if (combined != null) {
                    push(s.copy(jong = JONG.indexOf(combined)))
                    ""
                } else {
                    commitAndStart(State(cho = choIndex))
                }
            }
        }
    }

    private fun inputVowel(v: Int): String {
        val s = state
        return when {
            s.cho < 0 && s.jung < 0 -> { push(State(jung = v)); "" }
            s.jung < 0 -> { push(s.copy(jung = v)); "" }
            s.jong == 0 -> {
                val combined = COMPOUND_JUNG[JUNG[s.jung].toString() + JUNG[v]]
                if (combined != null) {
                    push(s.copy(jung = JUNG.indexOf(combined)))
                    ""
                } else {
                    commitAndStart(State(jung = v))
                }
            }
            else -> {
                // The final consonant moves to the new syllable; a compound final splits and only
                // its second half moves (닭 + ㅏ → 달가).
                val jongChar = JONG[s.jong]
                val split = SPLIT_JONG[jongChar]
                val keep = if (split != null) JONG.indexOf(split.first) else 0
                val moving = split?.second ?: jongChar
                val out = render(s.copy(jong = keep))
                history.clear()
                state = State()
                push(State(cho = CHO.indexOf(moving)))
                push(state.copy(jung = v))
                out
            }
        }
    }

    private fun render(s: State): String = when {
        s.cho >= 0 && s.jung >= 0 -> (0xAC00 + (s.cho * 21 + s.jung) * 28 + s.jong).toChar().toString()
        s.cho >= 0 -> CHO[s.cho].toString()
        s.jung >= 0 -> JUNG[s.jung].toString()
        else -> ""
    }

    companion object {
        private const val CHO = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
        private const val JUNG = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ"
        // Index 0 is "no final".
        private const val JONG = " ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"

        private val COMPOUND_JUNG = mapOf(
            "ㅗㅏ" to 'ㅘ', "ㅗㅐ" to 'ㅙ', "ㅗㅣ" to 'ㅚ',
            "ㅜㅓ" to 'ㅝ', "ㅜㅔ" to 'ㅞ', "ㅜㅣ" to 'ㅟ', "ㅡㅣ" to 'ㅢ',
        )
        private val COMPOUND_JONG = mapOf(
            "ㄱㅅ" to 'ㄳ', "ㄴㅈ" to 'ㄵ', "ㄴㅎ" to 'ㄶ', "ㄹㄱ" to 'ㄺ', "ㄹㅁ" to 'ㄻ',
            "ㄹㅂ" to 'ㄼ', "ㄹㅅ" to 'ㄽ', "ㄹㅌ" to 'ㄾ', "ㄹㅍ" to 'ㄿ', "ㄹㅎ" to 'ㅀ', "ㅂㅅ" to 'ㅄ',
        )
        private val SPLIT_JONG: Map<Char, Pair<Char, Char>> =
            COMPOUND_JONG.entries.associate { (pair, combined) -> combined to (pair[0] to pair[1]) }

        /** 두벌식: the jamo a letter key types, as printed on a Korean keyboard. */
        private val DUBEOLSIK = mapOf(
            'Q' to 'ㅂ', 'W' to 'ㅈ', 'E' to 'ㄷ', 'R' to 'ㄱ', 'T' to 'ㅅ', 'Y' to 'ㅛ', 'U' to 'ㅕ',
            'I' to 'ㅑ', 'O' to 'ㅐ', 'P' to 'ㅔ', 'A' to 'ㅁ', 'S' to 'ㄴ', 'D' to 'ㅇ', 'F' to 'ㄹ',
            'G' to 'ㅎ', 'H' to 'ㅗ', 'J' to 'ㅓ', 'K' to 'ㅏ', 'L' to 'ㅣ', 'Z' to 'ㅋ', 'X' to 'ㅌ',
            'C' to 'ㅊ', 'V' to 'ㅍ', 'B' to 'ㅠ', 'N' to 'ㅜ', 'M' to 'ㅡ',
        )
        private val DUBEOLSIK_SHIFT = mapOf(
            'Q' to 'ㅃ', 'W' to 'ㅉ', 'E' to 'ㄸ', 'R' to 'ㄲ', 'T' to 'ㅆ', 'O' to 'ㅒ', 'P' to 'ㅖ',
        )

        /** The jamo for a letter key's VK ('A'..'Z'), or null for any other key. */
        fun jamoForKey(vk: Int, shift: Boolean): Char? {
            if (vk !in 'A'.code..'Z'.code) return null
            val letter = vk.toChar()
            return (if (shift) DUBEOLSIK_SHIFT[letter] else null) ?: DUBEOLSIK[letter]
        }
    }
}
