package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HangulComposerTest {
    /** Types [jamo] and returns (final text so far, preview) -- what the PC has vs what is shown. */
    private fun type(c: HangulComposer, jamo: String): Pair<String, String> {
        val out = StringBuilder()
        jamo.forEach { out.append(c.input(it)) }
        return out.toString() to c.preview
    }

    private fun typeAll(jamo: String): String {
        val c = HangulComposer()
        val (out, _) = type(c, jamo)
        return out + c.flush()
    }

    @Test
    fun compoundFinal() {
        val c = HangulComposer()
        assertEquals("" to "값", type(c, "ㄱㅏㅂㅅ"))
        assertEquals("값", c.flush())
    }

    @Test
    fun aCompoundFinalThatCannotGrowCommitsAndTheNextSyllableStarts() {
        assertEquals("닭은", typeAll("ㄷㅏㄹㄱㅇㅡㄴ"))
    }

    @Test
    fun aVowelTakesTheFinalConsonantOrTheSecondHalfOfACompoundOne() {
        assertEquals("가나", typeAll("ㄱㅏㄴㅏ"))       // 간 + ㅏ → 가나
        assertEquals("달가", typeAll("ㄷㅏㄹㄱㅏ"))     // 닭 + ㅏ → 달가
        assertEquals("앉아", typeAll("ㅇㅏㄴㅈㅇㅏ"))
    }

    @Test
    fun compoundVowels() {
        assertEquals("와", typeAll("ㅇㅗㅏ"))
        assertEquals("의", typeAll("ㅇㅡㅣ"))
        assertEquals("웨", typeAll("ㅇㅜㅔ"))
        assertEquals("ㅘ", typeAll("ㅗㅏ"))
    }

    @Test
    fun whatIsFinalAndWhatIsStillBeingBuilt() {
        val c = HangulComposer()
        assertEquals("" to "한", type(c, "ㅎㅏㄴ"))
        // ㄱ after 한 is a new syllable: 한 is final, ㄱ is shown.
        assertEquals("한" to "ㄱ", type(c, "ㄱ"))
        assertEquals("" to "그", type(c, "ㅡ"))
        assertEquals("" to "글", type(c, "ㄹ"))
        assertEquals("글", c.flush())
        assertFalse(c.isComposing)
        assertEquals("", c.preview)
    }

    @Test
    fun consonantsThatCannotEndASyllableStartTheNextOne() {
        assertEquals("가ㄸ", typeAll("ㄱㅏㄸ"))
        assertEquals("가빠", typeAll("ㄱㅏㅃㅏ"))
        assertEquals("ㄱㄴ", typeAll("ㄱㄴ"))
    }

    @Test
    fun backspaceUndoesOneStepThenReportsNothingLeft() {
        val c = HangulComposer()
        type(c, "ㄱㅗㅏㄹㄱ")                           // 괅
        assertEquals("괅", c.preview)
        assertTrue(c.backspace()); assertEquals("괄", c.preview)
        assertTrue(c.backspace()); assertEquals("과", c.preview)
        assertTrue(c.backspace()); assertEquals("고", c.preview)
        assertTrue(c.backspace()); assertEquals("ㄱ", c.preview)
        assertTrue(c.backspace()); assertEquals("", c.preview)
        // Nothing being built: the caller sends a real Backspace to the PC instead.
        assertFalse(c.backspace())
    }

    @Test
    fun backspaceAfterAFinalMovedStaysInTheNewSyllable() {
        val c = HangulComposer()
        val (out, preview) = type(c, "ㄷㅏㄹㄱㅏ")
        assertEquals("달" to "가", out to preview)
        assertTrue(c.backspace()); assertEquals("ㄱ", c.preview)
        assertTrue(c.backspace()); assertEquals("", c.preview)
        assertFalse(c.backspace())
    }

    @Test
    fun twoSetKeyMapWithShift() {
        assertEquals('ㄱ', HangulComposer.jamoForKey('R'.code, shift = false))
        assertEquals('ㄲ', HangulComposer.jamoForKey('R'.code, shift = true))
        assertEquals('ㅃ', HangulComposer.jamoForKey('Q'.code, shift = true))
        assertEquals('ㅉ', HangulComposer.jamoForKey('W'.code, shift = true))
        assertEquals('ㄸ', HangulComposer.jamoForKey('E'.code, shift = true))
        assertEquals('ㅆ', HangulComposer.jamoForKey('T'.code, shift = true))
        assertEquals('ㅒ', HangulComposer.jamoForKey('O'.code, shift = true))
        assertEquals('ㅖ', HangulComposer.jamoForKey('P'.code, shift = true))
        // Shift does not change a key with no shifted jamo.
        assertEquals('ㅏ', HangulComposer.jamoForKey('K'.code, shift = true))
        assertEquals(null, HangulComposer.jamoForKey('1'.code, shift = false))
        assertEquals(null, HangulComposer.jamoForKey(0x20, shift = false))
    }

    @Test
    fun anythingThatIsNotAJamoEndsTheSyllable() {
        val c = HangulComposer()
        type(c, "ㄱㅏ")
        assertEquals("가!", c.input('!'))
        assertFalse(c.isComposing)
    }
}
