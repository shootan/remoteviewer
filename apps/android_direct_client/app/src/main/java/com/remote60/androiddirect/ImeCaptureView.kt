package com.remote60.androiddirect

import android.content.Context
import android.text.InputType
import android.util.AttributeSet
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection

/**
 * Invisible view that owns the soft keyboard and forwards what the IME produces to the host.
 *
 * Composing text has to be mirrored, not ignored. Korean (and Japanese/Chinese) IMEs build a
 * syllable through repeated setComposingText calls and only commit it once the *next* syllable
 * starts, so dropping composition meant the trailing syllable was never sent at all — typing
 * "유튜브" delivered "유튜" — and in-composition backspace, which the IME reports as a shorter
 * composing string rather than a delete, did nothing.
 *
 * The view therefore keeps the text currently shown on the host and, on every change, sends the
 * minimal edit: backspace away the part that no longer matches, then send the new tail.
 */
class ImeCaptureView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {
    interface Listener {
        fun onCommitText(text: CharSequence)
        fun onDeleteBackward(count: Int)
        fun onSpecialKey(keyCode: Int, action: Int)
        fun onPreviewTextChanged(text: CharSequence)
    }

    var listener: Listener? = null

    /** Text the host is currently showing for the in-progress composition. */
    private var composing: String = ""
    /** Confirmed text retained locally so small remote text remains readable above the IME. */
    private val previewCommitted = StringBuilder()

    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onCheckIsTextEditor(): Boolean = true

    // Virtual keyboards normally use InputConnection, but accessibility tools and Android's
    // synthetic keyboard inject directly into the focused View. Handle that route too so the
    // remote keystroke and local preview stay consistent.
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_DEL) {
            listener?.onDeleteBackward(1)
            deletePreviewCodePoints(1)
        } else {
            finishPreviewComposition()
            recordHardwareKeyEvent(event)
            listener?.onSpecialKey(keyCode, KeyEvent.ACTION_DOWN)
        }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode != KeyEvent.KEYCODE_DEL) {
            listener?.onSpecialKey(keyCode, KeyEvent.ACTION_UP)
        }
        return true
    }

    /** Host-visible length, in code points, because one backspace deletes one code point. */
    private fun codePointLength(s: String): Int =
        if (s.isEmpty()) 0 else s.codePointCount(0, s.length)

    private fun commonPrefixLength(a: String, b: String): Int {
        val max = minOf(a.length, b.length)
        var i = 0
        while (i < max && a[i] == b[i]) i++
        // Never split a surrogate pair.
        if (i > 0 && i < max && Character.isLowSurrogate(a[i]) && Character.isHighSurrogate(a[i - 1])) {
            i--
        }
        return i
    }

    /** Replace the current composing text on the host with [next]. */
    private fun replaceComposing(next: String) {
        val current = composing
        if (current == next) return
        val keep = commonPrefixLength(current, next)
        val removed = current.substring(keep)
        val added = next.substring(keep)
        val backspaces = codePointLength(removed)
        if (backspaces > 0) listener?.onDeleteBackward(backspaces)
        if (added.isNotEmpty()) listener?.onCommitText(added)
        composing = next
        notifyPreviewChanged()
    }

    private fun notifyPreviewChanged() {
        listener?.onPreviewTextChanged(previewCommitted.toString() + composing)
    }

    private fun trimPreviewToLimit() {
        val maximumCodePoints = 160
        val currentCodePoints = previewCommitted.codePointCount(0, previewCommitted.length)
        if (currentCodePoints <= maximumCodePoints) return
        val removeUntil = previewCommitted.offsetByCodePoints(0, currentCodePoints - maximumCodePoints)
        previewCommitted.delete(0, removeUntil)
    }

    private fun finishPreviewComposition() {
        if (composing.isNotEmpty()) {
            previewCommitted.append(composing)
            trimPreviewToLimit()
            composing = ""
            notifyPreviewChanged()
        }
    }

    private fun deletePreviewCodePoints(count: Int) {
        var remaining = count.coerceAtLeast(0)
        while (remaining > 0 && previewCommitted.isNotEmpty()) {
            val end = previewCommitted.length
            val start = previewCommitted.offsetByCodePoints(end, -1)
            previewCommitted.delete(start, end)
            remaining--
        }
        notifyPreviewChanged()
    }

    /** Mirror printable keys from a USB/Bluetooth keyboard without sending them twice. */
    fun recordHardwareKeyEvent(event: KeyEvent) {
        if (event.action != KeyEvent.ACTION_DOWN) return
        if (event.keyCode == KeyEvent.KEYCODE_DEL) {
            deletePreviewCodePoints(1)
            return
        }
        if (event.isCtrlPressed || event.isAltPressed || event.isMetaPressed) return
        val codePoint = event.unicodeChar
        if (codePoint <= 0 || Character.isISOControl(codePoint)) return
        previewCommitted.appendCodePoint(codePoint)
        trimPreviewToLimit()
        notifyPreviewChanged()
    }

    /** Drop composition tracking; whatever is on the host stays there. */
    fun resetComposingState() {
        finishPreviewComposition()
    }

    /** Clear only the phone-side preview; it never sends edits to the remote host. */
    fun resetPreviewState() {
        composing = ""
        previewCommitted.clear()
        notifyPreviewChanged()
    }

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType =
            InputType.TYPE_CLASS_TEXT or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS or
                InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
        outAttrs.imeOptions =
            EditorInfo.IME_FLAG_NO_EXTRACT_UI or
                EditorInfo.IME_FLAG_NO_ENTER_ACTION or
                EditorInfo.IME_ACTION_NONE
        outAttrs.initialSelStart = 0
        outAttrs.initialSelEnd = 0
        finishPreviewComposition()

        return object : BaseInputConnection(this@ImeCaptureView, false) {
            override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
                val value = text?.toString().orEmpty()
                // A commit replaces whatever composition is on screen.
                replaceComposing(value)
                previewCommitted.append(value)
                trimPreviewToLimit()
                composing = ""
                notifyPreviewChanged()
                return true
            }

            override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean {
                replaceComposing(text?.toString().orEmpty())
                return true
            }

            override fun finishComposingText(): Boolean {
                // The composed text stays on the host; stop tracking it as replaceable.
                finishPreviewComposition()
                return true
            }

            override fun setComposingRegion(start: Int, end: Int): Boolean = true

            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                if (composing.isNotEmpty()) {
                    // The IME edits composition through setComposingText; deleting here too
                    // would remove characters twice.
                    return true
                }
                if (beforeLength > 0) {
                    listener?.onDeleteBackward(beforeLength)
                    deletePreviewCodePoints(beforeLength)
                }
                return true
            }

            override fun deleteSurroundingTextInCodePoints(beforeLength: Int, afterLength: Int): Boolean {
                if (composing.isNotEmpty()) return true
                if (beforeLength > 0) {
                    listener?.onDeleteBackward(beforeLength)
                    deletePreviewCodePoints(beforeLength)
                }
                return true
            }

            override fun sendKeyEvent(event: KeyEvent): Boolean {
                if (event.keyCode == KeyEvent.KEYCODE_DEL) {
                    // While composing, the IME decomposes the syllable itself and reports the
                    // result via setComposingText; a raw backspace here would double-delete.
                    if (composing.isNotEmpty()) return true
                    if (event.action == KeyEvent.ACTION_DOWN) {
                        listener?.onDeleteBackward(1)
                        deletePreviewCodePoints(1)
                    }
                    return true
                }
                // Any other key ends the composition on the host side.
                if (event.action == KeyEvent.ACTION_DOWN) {
                    finishPreviewComposition()
                    recordHardwareKeyEvent(event)
                }
                listener?.onSpecialKey(event.keyCode, event.action)
                return true
            }

            override fun performEditorAction(actionCode: Int): Boolean {
                finishPreviewComposition()
                listener?.onSpecialKey(KeyEvent.KEYCODE_ENTER, KeyEvent.ACTION_DOWN)
                listener?.onSpecialKey(KeyEvent.KEYCODE_ENTER, KeyEvent.ACTION_UP)
                return true
            }
        }
    }
}
