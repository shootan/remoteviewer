package com.remote60.androiddirect

/**
 * The phone viewer's sharpening choice (sh-filter r1; Codex codex_sh_filter_r1.md S2 + the user's
 * 2026-10-22 delta: "일단 0.2 0.35 선택되게 둘 다 봐 보자").
 *
 * A 5-tap cross kernel run once on the decoded picture at its own size, before the compositor
 * enlarges it: out = (1 + 4s)·C − s·(L + R + T + B) on RGB, alpha 1, clamped to 0..1, with the four
 * neighbours one VISIBLE source pixel away. The form was observed in another product's renderer;
 * this is our own implementation of that formula, not their code, and it is a fixed kernel -- not
 * contrast-adaptive sharpening, and no claim of matching anyone's picture.
 *
 * OFF is not "s = 0 through GL": it is the decoder writing straight to the TextureView, the path
 * every build before this one used.
 *
 * Stored on its own key, apart from the bitrate/fps schema, so reading it never changes those.
 * Unset means WEAK (the default); an explicit OFF is kept.
 *
 * sh-strength r2 (Codex codex_field_0162_0231.md §4, user 10-22 "약하게랑 강하게랑 차이가 없어"): STRONG is 0.60
 * (was 0.35). The stored code stays "strong", so a phone that had chosen STRONG keeps STRONG and gets the new
 * strength -- by design: the choice is "the stronger one", and the UI shows the number. OFF, WEAK and unset are
 * unchanged.
 */
enum class SharpnessLevel(val code: String, val strength: Float) {
    OFF("off", 0f),
    WEAK("weak", 0.20f),
    STRONG("strong", 0.60f);

    val filtered: Boolean get() = this != OFF

    companion object {
        val DEFAULT = WEAK

        /** A stored value: unset or unknown -> the default; "off" stays off. */
        fun fromStored(stored: String?): SharpnessLevel =
            values().firstOrNull { it.code == stored } ?: DEFAULT
    }
}
