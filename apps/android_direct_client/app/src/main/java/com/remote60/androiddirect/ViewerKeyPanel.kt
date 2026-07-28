package com.remote60.androiddirect

import android.content.Context
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
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
    private val rows: List<List<Key>> = listOf(
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
            Key("`", "~", Vk.OEM_3),
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
            Key("Alt", vk = Vk.ALT, units = 1.2f),
            Key("Menu", vk = Vk.APPS, units = 1.2f),
            Key("Ctrl", vk = Vk.CTRL, units = 1.4f),
            Key("←", vk = Vk.LEFT), Key("↓", vk = Vk.DOWN), Key("→", vk = Vk.RIGHT),
            Key("", vk = 0, units = 0.4f),
        ),
        listOf(
            Key("Ins", vk = Vk.INSERT), Key("Home", vk = Vk.HOME), Key("PgUp", vk = Vk.PGUP),
            Key("Del", vk = Vk.DELETE), Key("End", vk = Vk.END), Key("PgDn", vk = Vk.PGDN),
            Key("", vk = 0, units = 10f),
        ),
    )

    private val modifierKeys = setOf(Vk.CTRL, Vk.SHIFT, Vk.ALT, Vk.WIN)
    private val heldModifiers = linkedSetOf<Int>()
    private val modifierButtons = mutableMapOf<Int, MutableList<Button>>()

    private val shortcutRow: LinearLayout = root.findViewById(R.id.keyPanelShortcutRow)
    private val shortcutScroll: HorizontalScrollView = root.findViewById(R.id.keyPanelShortcutScroll)
    private val keysScroll: ScrollView = root.findViewById(R.id.keyPanelKeysScroll)
    private val keysRoot: LinearLayout = root.findViewById(R.id.keyPanelKeysRoot)
    private val modifierText: TextView = root.findViewById(R.id.keyPanelModifierText)
    private val tabShortcut: Button = root.findViewById(R.id.keyPanelTabShortcut)
    private val tabKeys: Button = root.findViewById(R.id.keyPanelTabKeys)

    init {
        root.findViewById<Button>(R.id.keyPanelCloseButton).setOnClickListener { hide() }
        tabShortcut.setOnClickListener { showShortcuts(true) }
        tabKeys.setOnClickListener { showShortcuts(false) }
        buildShortcuts()
        buildKeyboard()
        showShortcuts(false)
        renderModifiers()
    }

    val isOpen: Boolean get() = root.visibility == View.VISIBLE

    fun toggle() {
        if (isOpen) hide() else show()
    }

    fun show() {
        root.visibility = View.VISIBLE
        // The panel pushes the video up rather than covering it, so cap its height or a
        // seven-row keyboard would leave almost no picture on a phone held sideways.
        root.post {
            val parentHeight = (root.parent as? View)?.height ?: 0
            if (parentHeight > 0) {
                val cap = (parentHeight * 0.55f).toInt()
                if (root.height > cap) {
                    root.layoutParams = root.layoutParams.also { it.height = cap }
                    root.requestLayout()
                }
            }
        }
    }

    fun hide() {
        releaseHeldModifiers()
        root.layoutParams = root.layoutParams.also {
            it.height = LinearLayout.LayoutParams.WRAP_CONTENT
        }
        root.visibility = View.GONE
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
        for (row in rows) {
            val line = LinearLayout(context)
            line.orientation = LinearLayout.HORIZONTAL
            line.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT,
            )
            for (key in row) {
                if (key.vk == 0) {
                    val filler = View(context)
                    filler.layoutParams = LinearLayout.LayoutParams(0, dp(42f), key.units)
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
                val lp = LinearLayout.LayoutParams(0, dp(42f), key.units)
                lp.setMargins(dp(1.5f), dp(1.5f), dp(1.5f), dp(1.5f))
                b.layoutParams = lp
                b.setOnClickListener { onKeyTapped(key.vk) }
                if (key.vk in modifierKeys) {
                    modifierButtons.getOrPut(key.vk) { mutableListOf() }.add(b)
                }
                line.addView(b)
            }
            keysRoot.addView(line)
        }
    }

    private fun showShortcuts(shortcuts: Boolean) {
        shortcutScroll.visibility = if (shortcuts) View.VISIBLE else View.GONE
        keysScroll.visibility = if (shortcuts) View.GONE else View.VISIBLE
        tabShortcut.alpha = if (shortcuts) 1.0f else 0.55f
        tabKeys.alpha = if (shortcuts) 0.55f else 1.0f
    }

    private fun onKeyTapped(vk: Int) {
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
        releaseHeldModifiers()
        chord.mods.forEach { onKey(it, true) }
        onKey(chord.key, true)
        onKey(chord.key, false)
        chord.mods.reversed().forEach { onKey(it, false) }
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
