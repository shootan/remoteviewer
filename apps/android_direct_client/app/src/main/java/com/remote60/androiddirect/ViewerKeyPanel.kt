package com.remote60.androiddirect

import android.content.Context
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView

/**
 * PC keyboard for the viewer.
 *
 * The soft keyboard can only produce text. Modifiers, function keys and chords like Ctrl+C
 * have no representation there, so they are sent from here as Windows virtual-key down/up
 * pairs, bypassing the IME entirely.
 *
 * Rows are laid out with weights rather than wrap_content so the result is shaped like a real
 * keyboard: every ordinary key is one unit wide, Tab/Caps/Shift/Enter/Space take their usual
 * multiples, and the rows line up as a grid.
 */
class ViewerKeyPanel(
    private val context: Context,
    private val root: LinearLayout,
    private val onKey: (vk: Int, down: Boolean) -> Unit,
    /** Finished Hangul text for the PC (apk-ui r3): sent as text, not as keys. */
    private val onText: (text: String) -> Unit = {},
    /** The syllable being composed, for the on-screen preview; "" clears it. */
    private val onPreview: (text: String) -> Unit = {},
    /** The 휴대폰 자판 tab: close the panel and bring up the phone's own keyboard. */
    private val onPhoneKeyboard: () -> Unit = {},
) {
    private object Vk {
        const val BACK = 0x08; const val TAB = 0x09; const val ENTER = 0x0D
        const val SHIFT = 0x10; const val CTRL = 0x11; const val ALT = 0x12
        const val PAUSE = 0x13; const val CAPS = 0x14
        const val ESC = 0x1B; const val SPACE = 0x20
        const val PGUP = 0x21; const val PGDN = 0x22; const val END = 0x23; const val HOME = 0x24
        const val LEFT = 0x25; const val UP = 0x26; const val RIGHT = 0x27; const val DOWN = 0x28
        const val PRTSC = 0x2C; const val INSERT = 0x2D; const val DELETE = 0x2E
        const val WIN = 0x5B; const val APPS = 0x5D
        /** Not a Windows key: the panel's own 한/영 switch, never sent to the host. */
        const val HANYEONG = -2
        const val F1 = 0x70
        const val SCROLL = 0x91
        const val OEM_1 = 0xBA      // ;:
        const val OEM_PLUS = 0xBB   // =+
        const val OEM_COMMA = 0xBC  // ,<
        const val OEM_MINUS = 0xBD  // -_
        const val OEM_PERIOD = 0xBE // .>
        const val OEM_2 = 0xBF      // /?
        const val OEM_3 = 0xC0      // `~
        const val OEM_4 = 0xDB      // [{
        const val OEM_5 = 0xDC      // \|
        const val OEM_6 = 0xDD      // ]}
        const val OEM_7 = 0xDE      // '"
    }

    /** One key: main label, optional second line (Hangul jamo / shifted symbol), vk, width units. */
    private data class Key(
        val label: String,
        val sub: String? = null,
        val vk: Int,
        val units: Float = 1f,
    )

    private data class Chord(val label: String, val mods: List<Int>, val key: Int)

    private val shortcuts = listOf(
        Chord("복사\nCtrl+C", listOf(Vk.CTRL), 'C'.code),
        Chord("붙여넣기\nCtrl+V", listOf(Vk.CTRL), 'V'.code),
        Chord("잘라내기\nCtrl+X", listOf(Vk.CTRL), 'X'.code),
        Chord("전체선택\nCtrl+A", listOf(Vk.CTRL), 'A'.code),
        Chord("실행취소\nCtrl+Z", listOf(Vk.CTRL), 'Z'.code),
        Chord("다시실행\nCtrl+Y", listOf(Vk.CTRL), 'Y'.code),
        Chord("저장\nCtrl+S", listOf(Vk.CTRL), 'S'.code),
        Chord("찾기\nCtrl+F", listOf(Vk.CTRL), 'F'.code),
        Chord("창 닫기\nCtrl+W", listOf(Vk.CTRL), 'W'.code),
        Chord("새로고침\nF5", emptyList(), Vk.F1 + 4),
        Chord("삭제\nDelete", emptyList(), Vk.DELETE),
        Chord("이름 바꾸기\nF2", emptyList(), Vk.F1 + 1),
        Chord("실행\nWin+R", listOf(Vk.WIN), 'R'.code),
        Chord("탐색기\nWin+E", listOf(Vk.WIN), 'E'.code),
        Chord("작업 관리자\nCtrl+Shift+Esc", listOf(Vk.CTRL, Vk.SHIFT), Vk.ESC),
        Chord("창 전환\nAlt+Tab", listOf(Vk.ALT), Vk.TAB),
        Chord("화면 잠금\nWin+L", listOf(Vk.WIN), 'L'.code),
        Chord("검색\nWin+Q", listOf(Vk.WIN), 'Q'.code),
        Chord("바탕화면\nWin+D", listOf(Vk.WIN), 'D'.code),
        Chord("창 캡처\nAlt+PrtSc", listOf(Vk.ALT), Vk.PRTSC),
    )

    // Two-beolsik jamo, matching the layout printed on a Korean keyboard.
    /**
     * The function-key and navigation rows, hidden unless asked for.
     *
     * Every row costs height that comes straight off the picture, and on a phone held sideways
     * seven rows leaves keys too thin to hit. These two are the ones people rarely need.
     */
    private val extraRows: List<List<Key>> = listOf(
        listOf(
            Key("Esc", vk = Vk.ESC),
            Key("F1", vk = Vk.F1), Key("F2", vk = Vk.F1 + 1),
            Key("F3", vk = Vk.F1 + 2), Key("F4", vk = Vk.F1 + 3),
            Key("F5", vk = Vk.F1 + 4), Key("F6", vk = Vk.F1 + 5),
            Key("F7", vk = Vk.F1 + 6), Key("F8", vk = Vk.F1 + 7),
            Key("F9", vk = Vk.F1 + 8), Key("F10", vk = Vk.F1 + 9),
            Key("F11", vk = Vk.F1 + 10), Key("F12", vk = Vk.F1 + 11),
            Key("PrtSc", vk = Vk.PRTSC), Key("Scr", vk = Vk.SCROLL), Key("Pause", vk = Vk.PAUSE),
        ),  // 16 units
        listOf(
            Key("Ins", vk = Vk.INSERT), Key("Home", vk = Vk.HOME), Key("PgUp", vk = Vk.PGUP),
            Key("Del", vk = Vk.DELETE), Key("End", vk = Vk.END), Key("PgDn", vk = Vk.PGDN),
            Key("", vk = 0, units = 10f),
        ),
    )

    private val coreRows: List<List<Key>> = listOf(
        listOf(
            Key("Esc", vk = Vk.ESC),
            Key("1", "!", '1'.code), Key("2", "@", '2'.code), Key("3", "#", '3'.code),
            Key("4", "$", '4'.code), Key("5", "%", '5'.code), Key("6", "^", '6'.code),
            Key("7", "&", '7'.code), Key("8", "*", '8'.code), Key("9", "(", '9'.code),
            Key("0", ")", '0'.code),
            Key("-", "_", Vk.OEM_MINUS), Key("=", "+", Vk.OEM_PLUS),
            Key("Back", vk = Vk.BACK, units = 2f),
            Key("", vk = 0, units = 1f),
        ),
        listOf(
            Key("Tab", vk = Vk.TAB, units = 1.5f),
            Key("Q", "ㅂ", 'Q'.code), Key("W", "ㅈ", 'W'.code), Key("E", "ㄷ", 'E'.code),
            Key("R", "ㄱ", 'R'.code), Key("T", "ㅅ", 'T'.code), Key("Y", "ㅛ", 'Y'.code),
            Key("U", "ㅕ", 'U'.code), Key("I", "ㅑ", 'I'.code), Key("O", "ㅐ", 'O'.code),
            Key("P", "ㅔ", 'P'.code),
            Key("[", "{", Vk.OEM_4), Key("]", "}", Vk.OEM_6),
            Key("\\", "|", Vk.OEM_5, units = 1.5f),
            Key("", vk = 0, units = 1f),
        ),
        listOf(
            Key("Caps", vk = Vk.CAPS, units = 1.75f),
            Key("A", "ㅁ", 'A'.code), Key("S", "ㄴ", 'S'.code), Key("D", "ㅇ", 'D'.code),
            Key("F", "ㄹ", 'F'.code), Key("G", "ㅎ", 'G'.code), Key("H", "ㅗ", 'H'.code),
            Key("J", "ㅓ", 'J'.code), Key("K", "ㅏ", 'K'.code), Key("L", "ㅣ", 'L'.code),
            Key(";", ":", Vk.OEM_1), Key("'", "\"", Vk.OEM_7),
            Key("Enter", vk = Vk.ENTER, units = 2.25f),
            Key("", vk = 0, units = 1f),
        ),
        listOf(
            Key("Shift", vk = Vk.SHIFT, units = 2.25f),
            Key("Z", "ㅋ", 'Z'.code), Key("X", "ㅌ", 'X'.code), Key("C", "ㅊ", 'C'.code),
            Key("V", "ㅍ", 'V'.code), Key("B", "ㅠ", 'B'.code), Key("N", "ㅜ", 'N'.code),
            Key("M", "ㅡ", 'M'.code),
            Key(",", "<", Vk.OEM_COMMA), Key(".", ">", Vk.OEM_PERIOD), Key("/", "?", Vk.OEM_2),
            Key("Shift", vk = Vk.SHIFT, units = 1.75f),
            Key("↑", vk = Vk.UP),
            Key("", vk = 0, units = 1f),
        ),
        listOf(
            Key("Ctrl", vk = Vk.CTRL, units = 1.4f),
            Key("Win", vk = Vk.WIN, units = 1.2f),
            Key("Alt", vk = Vk.ALT, units = 1.2f),
            Key("Space", vk = Vk.SPACE, units = 5f),
            // Where a Korean keyboard has 한/영 (right Alt).
            Key("한/영", vk = Vk.HANYEONG, units = 1.2f),
            Key("Menu", vk = Vk.APPS, units = 1.2f),
            Key("Ctrl", vk = Vk.CTRL, units = 1.4f),
            Key("←", vk = Vk.LEFT), Key("↓", vk = Vk.DOWN), Key("→", vk = Vk.RIGHT),
            Key("", vk = 0, units = 0.4f),
        ),
    )

    private val modifierKeys = setOf(Vk.CTRL, Vk.SHIFT, Vk.ALT, Vk.WIN)
    private val heldModifiers = linkedSetOf<Int>()
    private val modifierButtons = mutableMapOf<Int, MutableList<Button>>()

    private val shortcutRow: LinearLayout = root.findViewById(R.id.keyPanelShortcutRow)
    private val shortcutScroll: HorizontalScrollView = root.findViewById(R.id.keyPanelShortcutScroll)
    private val keysRoot: LinearLayout = root.findViewById(R.id.keyPanelKeysRoot)
    private val modifierText: TextView = root.findViewById(R.id.keyPanelModifierText)
    private val tabShortcut: Button = root.findViewById(R.id.keyPanelTabShortcut)
    private val tabKeys: Button = root.findViewById(R.id.keyPanelTabKeys)
    private val tabExtra: Button = root.findViewById(R.id.keyPanelTabExtra)
    private val tabPhone: Button = root.findViewById(R.id.keyPanelTabPhone)
    private var showExtraRows = false

    /**
     * 한/영. The host puts the target's IME back to alphanumeric before every key it injects, so
     * VK_HANGUL would not stick; in 한 mode this panel composes Hangul itself (HangulComposer) and
     * sends finished syllables as text, like the PC viewer.
     */
    var koreanMode = false
        private set
    private val composer = HangulComposer()
    private val hanYeongButtons = mutableListOf<Button>()

    init {
        root.findViewById<Button>(R.id.keyPanelCloseButton).setOnClickListener { hide() }
        tabShortcut.setOnClickListener { commitComposition(); showShortcuts(true) }
        tabKeys.setOnClickListener { showShortcuts(false) }
        tabPhone.setOnClickListener {
            commitComposition()
            onPhoneKeyboard()
        }
        tabExtra.setOnClickListener {
            commitComposition()
            showExtraRows = !showExtraRows
            buildKeyboard()
            renderModifiers()
            showShortcuts(false)
            // Two more rows need more of the screen; without this the seven rows squeeze into
            // the five-row height and every key turns into a sliver.
            if (isOpen) applyPanelHeight()
        }
        buildShortcuts()
        buildKeyboard()
        showShortcuts(false)
        renderModifiers()
    }

    val isOpen: Boolean get() = root.visibility == View.VISIBLE

    /**
     * Called after every show() and hide(), whoever made it (the rail button, the close button,
     * Back, a scene change), so the [휴대폰 자판 | PC 키] bar above the panel goes with it.
     */
    var onOpenChanged: (() -> Unit)? = null

    /**
     * Height the panel must leave to the rest of the viewer. In landscape the rail stands beside
     * the picture above the panel, and four 48dp buttons need their room (apk-ui r3d).
     */
    var reservedHeightPx: Int = 0
        set(value) {
            field = value
            if (isOpen) applyPanelHeight()
        }

    fun toggle() {
        if (isOpen) hide() else show()
    }

    fun show() {
        root.visibility = View.VISIBLE
        applyPanelHeight()
        onOpenChanged?.invoke()
    }

    // The panel pushes the picture up rather than covering it, so it gets a fixed share of
    // the screen and the rows divide whatever that comes to. Setting the height here rather
    // than measuring afterwards avoids the panel appearing at full size and then snapping.
    private fun applyPanelHeight() {
        root.post {
            val parentHeight = (root.parent as? View)?.height ?: 0
            if (parentHeight > 0) {
                val share = if (showExtraRows) 0.62f else 0.5f
                val wanted = (parentHeight * share).toInt()
                root.layoutParams = root.layoutParams.also {
                    it.height = wanted.coerceAtMost(parentHeight - reservedHeightPx).coerceAtLeast(0)
                }
                root.requestLayout()
            }
        }
    }

    fun hide() {
        // A syllable still being built is sent, not dropped, and its preview goes with the panel.
        commitComposition()
        releaseHeldModifiers()
        root.layoutParams = root.layoutParams.also {
            it.height = LinearLayout.LayoutParams.WRAP_CONTENT
        }
        root.visibility = View.GONE
        onOpenChanged?.invoke()
    }

    private fun dp(v: Float): Int = (v * context.resources.displayMetrics.density).toInt()

    private fun buildShortcuts() {
        shortcutRow.removeAllViews()
        for (chord in shortcuts) {
            val b = Button(context)
            b.text = chord.label
            b.isAllCaps = false
            b.textSize = 9f
            b.setTextColor(0xFFF4F0E8.toInt())
            b.setBackgroundResource(R.drawable.viewer_control_button_background)
            b.minWidth = 0
            b.minimumWidth = 0
            b.setPadding(dp(6f), dp(4f), dp(6f), dp(4f))
            b.gravity = Gravity.CENTER
            val lp = LinearLayout.LayoutParams(dp(94f), dp(46f))
            lp.setMargins(dp(3f), dp(3f), dp(3f), dp(3f))
            b.layoutParams = lp
            b.setOnClickListener { sendChord(chord) }
            shortcutRow.addView(b)
        }
    }

    private fun buildKeyboard() {
        keysRoot.removeAllViews()
        modifierButtons.clear()
        hanYeongButtons.clear()
        tabExtra.alpha = if (showExtraRows) 1.0f else 0.55f
        val rows = if (showExtraRows) extraRows + coreRows else coreRows
        for (row in rows) {
            val line = LinearLayout(context)
            line.orientation = LinearLayout.HORIZONTAL
            // Equal weight per row, so however much height the panel gets, the whole keyboard
            // is visible instead of the last rows dropping off the bottom.
            line.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f,
            )
            for (key in row) {
                if (key.vk == 0) {
                    val filler = View(context)
                    filler.layoutParams =
                        LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, key.units)
                    line.addView(filler)
                    continue
                }
                val b = Button(context)
                b.text = if (key.sub != null) "${key.label}\n${key.sub}" else key.label
                b.isAllCaps = false
                b.textSize = if (key.sub != null) 10f else 11f
                b.setTextColor(0xFFF4F0E8.toInt())
                b.setBackgroundResource(R.drawable.viewer_control_button_background)
                b.minWidth = 0
                b.minimumWidth = 0
                b.minHeight = 0
                b.minimumHeight = 0
                b.setPadding(0, 0, 0, 0)
                b.gravity = Gravity.CENTER
                // Width comes from the weight, so every ordinary key is exactly one unit and
                // the rows line up like a real keyboard.
                val lp = LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, key.units,
                )
                lp.setMargins(dp(1.5f), dp(1.5f), dp(1.5f), dp(1.5f))
                b.layoutParams = lp
                b.setOnClickListener { onKeyTapped(key.vk) }
                if (key.vk in modifierKeys) {
                    modifierButtons.getOrPut(key.vk) { mutableListOf() }.add(b)
                }
                if (key.vk == Vk.HANYEONG) hanYeongButtons.add(b)
                line.addView(b)
            }
            keysRoot.addView(line)
        }
        renderHanYeong()
    }

    /** The 한/영 key says which mode is on, as a Korean keyboard's indicator would. */
    private fun renderHanYeong() {
        hanYeongButtons.forEach {
            it.text = context.getString(
                if (koreanMode) R.string.key_panel_hanyeong_han else R.string.key_panel_hanyeong_eng
            )
            it.alpha = if (koreanMode) 1.0f else 0.72f
            it.contentDescription =
                context.getString(R.string.key_panel_hanyeong_description, if (koreanMode) "한" else "영")
        }
    }

    /** Sends the syllable being built, if any, and clears its preview. */
    fun commitComposition() {
        val text = composer.flush()
        if (text.isNotEmpty()) onText(text)
        onPreview("")
    }

    private fun showShortcuts(shortcuts: Boolean) {
        shortcutScroll.visibility = if (shortcuts) View.VISIBLE else View.GONE
        keysRoot.visibility = if (shortcuts) View.GONE else View.VISIBLE
        tabShortcut.alpha = if (shortcuts) 1.0f else 0.55f
        tabKeys.alpha = if (shortcuts) 0.55f else 1.0f
    }

    private fun onKeyTapped(vk: Int) {
        if (vk == Vk.HANYEONG) {
            commitComposition()
            koreanMode = !koreanMode
            renderHanYeong()
            return
        }
        if (koreanMode) {
            // With nothing but Shift held, a letter is a jamo: Shift picks ㄲ ㄸ ㅃ ㅆ ㅉ ㅒ ㅖ and is
            // let go before the text goes out. Ctrl/Alt/Win + letter stays a shortcut.
            val onlyShift = heldModifiers.all { it == Vk.SHIFT }
            val jamo = if (onlyShift) HangulComposer.jamoForKey(vk, Vk.SHIFT in heldModifiers) else null
            if (jamo != null) {
                releaseHeldModifiers()
                val done = composer.input(jamo)
                if (done.isNotEmpty()) onText(done)
                onPreview(composer.preview)
                return
            }
            if (vk == Vk.BACK && heldModifiers.isEmpty() && composer.backspace()) {
                onPreview(composer.preview)
                return
            }
            // Space, Enter, digits, arrows, modifiers, any shortcut: the syllable is finished first,
            // so it lands before the key that follows it.
            if (vk != Vk.SHIFT) commitComposition()
        }
        if (vk in modifierKeys) {
            // Sticky, so one finger can express a chord.
            if (heldModifiers.contains(vk)) {
                heldModifiers.remove(vk)
                onKey(vk, false)
            } else {
                heldModifiers.add(vk)
                onKey(vk, true)
            }
            renderModifiers()
            return
        }
        onKey(vk, true)
        onKey(vk, false)
        releaseHeldModifiers()
    }

    private fun sendChord(chord: Chord) {
        commitComposition()
        releaseHeldModifiers()
        HostKeyChord.steps(chord.mods, chord.key).forEach { onKey(it.vk, it.down) }
    }

    private fun releaseHeldModifiers() {
        if (heldModifiers.isEmpty()) return
        heldModifiers.reversed().forEach { onKey(it, false) }
        heldModifiers.clear()
        renderModifiers()
    }

    private fun renderModifiers() {
        modifierButtons.forEach { (vk, buttons) ->
            val held = heldModifiers.contains(vk)
            buttons.forEach { it.alpha = if (held) 1.0f else 0.72f }
        }
        modifierText.text = if (heldModifiers.isEmpty()) {
            context.getString(R.string.key_panel_modifiers_none)
        } else {
            val names = heldModifiers.joinToString("+") {
                when (it) {
                    Vk.CTRL -> "Ctrl"; Vk.SHIFT -> "Shift"; Vk.ALT -> "Alt"; Vk.WIN -> "Win"
                    else -> "?"
                }
            }
            context.getString(R.string.key_panel_modifiers, names)
        }
    }
}
