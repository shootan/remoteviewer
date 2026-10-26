package com.remote60.androiddirect

/**
 * sh-strength r3 (Codex codex_sh_strength_r2.md §2): WHERE the sharpening pass draws -- a user choice, apart from the
 * strength (SharpnessLevel) and stored on its own key.
 *
 * DEFAULT (A): the decoded picture at its own size; the compositor enlarges it. Unset and every existing user: A.
 * DISPLAY (B, "화면 크기 (실험)"): at the screen's own pixel size, enlarging and sharpening in one draw -- text and thin
 * lines come out crisper, at some extra GPU time and heat. An independent implementation, not anyone else's method.
 *
 * Off (SharpnessLevel.OFF) is the decoder writing straight to the view whatever this is.
 */
enum class SharpenMethod(val code: String) {
    DEFAULT("default"),
    DISPLAY("display");

    companion object {
        /** Unset or unknown -> DEFAULT. */
        fun fromStored(stored: String?): SharpenMethod = values().firstOrNull { it.code == stored } ?: DEFAULT
    }
}
