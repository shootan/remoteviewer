package com.remote60.androiddirect

import android.content.Context
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * PC-style key panel for the viewer.
 *
 * The soft keyboard can only produce text. Modifiers, function keys and combinations like
 * Ctrl+C or Alt+Tab have no representation there, so they are sent from here as raw Windows
 * virtual-key down/up pairs, bypassing the IME entirely.
 *
 * Modifiers are sticky: tapping Ctrl holds it until a normal key is pressed (or it is tapped
 * again), which is how a one-finger touch device can express a chord.
 */
class ViewerKeyPanel(
    private val context: Context,
    private val root: LinearLayout,
    private val onKey: (vk: Int, down: Boolean) -> Unit,
) {
    private object Vk {
        const val BACK = 0x08; const val TAB = 0x09; const val ENTER = 0x0D
        const val SHIFT = 0x10; const val CTRL = 0x11; const val ALT = 0x12
        const val ESC = 0x1B; const val SPACE = 0x20
        const val PGUP = 0x21; const val PGDN = 0x22; const val END = 0x23; const val HOME = 0x24
        const val LEFT = 0x25; const val UP = 0x26; const val RIGHT = 0x27; const val DOWN = 0x28
        const val PRTSC = 0x2C; const val INSERT = 0x2D; const val DELETE = 0x2E
        const val WIN = 0x5B
        const val F1 = 0x70
    }

    private data class Chord(val label: String, val mods: List<Int>, val key: Int)

    private val shortcuts = listOf(
        Chord("복사\nCtrl+C", listOf(Vk.CTRL), 'C'.code),
        Chord("붙여넣기\nCtrl+V", listOf(Vk.CTRL), 'V'.code),
        Chord("잘라내기\nCtrl+X", listOf(Vk.CTRL), 'X'.code),
        Chord("전체선택\nCtrl+A", listOf(Vk.CTRL), 'A'.code),
        Chord("실행취소\nCtrl+Z", listOf(Vk.CTRL), 'Z'.code),
        Chord("저장\nCtrl+S", listOf(Vk.CTRL), 'S'.code),
        Chord("창 닫기\nCtrl+W", listOf(Vk.CTRL), 'W'.code),
        Chord("새로고침\nF5", emptyList(), Vk.F1 + 4),
        Chord("삭제\nDelete", emptyList(), Vk.DELETE),
        Chord("이름 바꾸기\nF2", emptyList(), Vk.F1 + 1),
        Chord("실행\nWin+R", listOf(Vk.WIN), 'R'.code),
        Chord("파일 탐색기\nWin+E", listOf(Vk.WIN), 'E'.code),
        Chord("작업 관리자\nCtrl+Shift+Esc", listOf(Vk.CTRL, Vk.SHIFT), Vk.ESC),
        Chord("창 전환\nAlt+Tab", listOf(Vk.ALT), Vk.TAB),
        Chord("화면 잠금\nWin+L", listOf(Vk.WIN), 'L'.code),
        Chord("검색\nWin+Q", listOf(Vk.WIN), 'Q'.code),
    )

    /** label to virtual key; null label entries are spacers. */
    private val keyRows: List<List<Pair<String, Int>>> = listOf(
        listOf("Esc" to Vk.ESC) + (0..11).map { "F${it + 1}" to (Vk.F1 + it) },
        "1234567890".mapIndexed { i, c -> c.toString() to ('0'.code + ((i + 1) % 10)) } +
            listOf("Back" to Vk.BACK),
        listOf("Tab" to Vk.TAB) + "QWERTYUIOP".map { it.toString() to it.code },
        "ASDFGHJKL".map { it.toString() to it.code } + listOf("Enter" to Vk.ENTER),
        listOf("Shift" to Vk.SHIFT) + "ZXCVBNM".map { it.toString() to it.code } +
            listOf("Del" to Vk.DELETE),
        listOf(
            "Ctrl" to Vk.CTRL, "Win" to Vk.WIN, "Alt" to Vk.ALT, "Space" to Vk.SPACE,
            "Ins" to Vk.INSERT, "Home" to Vk.HOME, "End" to Vk.END,
            "PgUp" to Vk.PGUP, "PgDn" to Vk.PGDN, "PrtSc" to Vk.PRTSC,
        ),
        listOf("←" to Vk.LEFT, "↑" to Vk.UP, "↓" to Vk.DOWN, "→" to Vk.RIGHT),
    )

    private val modifierKeys = setOf(Vk.CTRL, Vk.SHIFT, Vk.ALT, Vk.WIN)
    private val heldModifiers = linkedSetOf<Int>()

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
        buildKeys()
        showShortcuts(true)
        renderModifiers()
    }

    val isOpen: Boolean get() = root.visibility == View.VISIBLE

    fun toggle() {
        if (isOpen) hide() else show()
    }

    fun show() {
        root.visibility = View.VISIBLE
    }

    fun hide() {
        releaseHeldModifiers()
        root.visibility = View.GONE
    }

    private fun dp(v: Int): Int = (v * context.resources.displayMetrics.density).toInt()

    private fun makeButton(label: String, wide: Boolean): Button {
        val b = Button(context)
        b.text = label
        b.isAllCaps = false
        b.textSize = if (label.contains('\n')) 9f else 12f
        b.setTextColor(0xFFF4F0E8.toInt())
        b.setBackgroundResource(R.drawable.viewer_control_button_background)
        b.minWidth = 0
        b.minimumWidth = 0
        b.setPadding(dp(6), dp(4), dp(6), dp(4))
        val lp = LinearLayout.LayoutParams(
            if (wide) dp(96) else ViewGroup.LayoutParams.WRAP_CONTENT,
            dp(if (wide) 46 else 40),
        )
        lp.setMargins(dp(3), dp(3), dp(3), dp(3))
        b.layoutParams = lp
        b.gravity = Gravity.CENTER
        return b
    }

    private fun buildShortcuts() {
        shortcutRow.removeAllViews()
        for (chord in shortcuts) {
            val b = makeButton(chord.label, wide = true)
            b.setOnClickListener { sendChord(chord) }
            shortcutRow.addView(b)
        }
    }

    private fun buildKeys() {
        keysRoot.removeAllViews()
        for (row in keyRows) {
            val line = LinearLayout(context)
            line.orientation = LinearLayout.HORIZONTAL
            for ((label, vk) in row) {
                val b = makeButton(label, wide = false)
                b.setOnClickListener { onKeyTapped(vk, b) }
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

    private fun onKeyTapped(vk: Int, button: Button) {
        if (vk in modifierKeys) {
            // Sticky: hold it until a real key follows or it is tapped again.
            if (heldModifiers.contains(vk)) {
                heldModifiers.remove(vk)
                onKey(vk, false)
            } else {
                heldModifiers.add(vk)
                onKey(vk, true)
            }
            renderModifiers()
            button.alpha = if (heldModifiers.contains(vk)) 1.0f else 0.75f
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
        for (i in 0 until keysRoot.childCount) {
            val line = keysRoot.getChildAt(i) as? LinearLayout ?: continue
            for (j in 0 until line.childCount) line.getChildAt(j).alpha = 1.0f
        }
    }

    private fun renderModifiers() {
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
