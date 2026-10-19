package com.remote60.androiddirect

/**
 * Remote reboot from the phone (prelogin-ui r1, item 8; user decision 2026-10-07: "in the menu, with a
 * confirm popup, on the phone and the PC").
 *
 * The rule is the PC viewer's (native_poc/src/reboot_menu_policy.hpp); both are checked against the
 * same vectors (apps/shared/reboot_menu_vectors.txt), so the two viewers cannot drift. The 84/85
 * messages themselves are built and parsed by the shared C++ codec (reboot_wire.hpp) behind the JNI
 * bridge -- no second copy of the layout here.
 */
object RemoteRebootPolicy {
    const val FLAG_PRELOGIN_NOW = 0x2000        // the host is on the pre-login screen right now
    const val FLAG_REMOTE_REBOOT = 0x4000       // the host takes 84 and answers 85
    const val FLAG_PRELOGIN_ENABLED = 0x8000    // reachable before login after a restart (durable)

    data class Menu(
        val enabled: Boolean,
        val disabledBecauseOldHost: Boolean,
        val showPreloginWarning: Boolean,
    )

    fun menu(hostFlags: Int): Menu {
        val canReboot = hostFlags and FLAG_REMOTE_REBOOT != 0
        val durable = hostFlags and FLAG_PRELOGIN_ENABLED != 0
        // Reachability AFTER the restart depends on durable-enabled, not on being in prelogin now.
        return Menu(enabled = canReboot, disabledBecauseOldHost = !canReboot, showPreloginWarning = canReboot && !durable)
    }

    /** The phone has no sealed credential entry yet: on the pre-login screen it says so, never plain text. */
    fun showCredentialUpdateNote(hostFlags: Int): Boolean = hostFlags and FLAG_PRELOGIN_NOW != 0

    const val MENU_TITLE = "원격 PC 다시 시작"
    const val DISABLED_REASON = "이 원격 PC의 GNLink 는 원격 다시 시작을 지원하지 않습니다 (원격 PC 업데이트 필요)"
    const val CONFIRM_BODY = "원격 PC를 다시 시작합니다. 저장하지 않은 작업은 사라질 수 있습니다."
    const val PRELOGIN_WARNING =
        "로그인 전 원격 접속이 꺼져 있어, 다시 시작한 뒤에는 그 PC 에서 로그인할 때까지 접속할 수 없습니다."
    const val CREDENTIAL_UPDATE_NOTE =
        "로그인 화면 비밀번호 입력은 앱 업데이트 뒤 가능합니다. 지금은 PC 뷰어의 \"Windows 로그인\" 으로 로그인하세요."

    fun confirmMessage(menu: Menu): String =
        if (menu.showPreloginWarning) "$CONFIRM_BODY\n\n$PRELOGIN_WARNING" else CONFIRM_BODY
}

/**
 * One confirm -> one request. The state the menu shows between the confirm and the reconnect:
 * "requesting" before an answer, "accepted / waiting to reconnect" after Accepted (the OS took the
 * request -- NOT "reboot complete"), "not known" when no answer came. A lost answer is never sent
 * again by itself; the user decides. The disconnect a restart causes is not an error.
 */
class RemoteRebootFlow(private val send: () -> Int) {
    enum class Phase { Idle, Confirming, Requesting, Accepted, Refused, NotAuthorized, Failed, Unknown }

    var phase: Phase = Phase.Idle
        private set
    var requestId: Int = 0
        private set
    var win32: Long = 0
        private set
    var sends: Int = 0
        private set

    /** The menu item was pressed: the confirm popup is up (nothing is sent yet). */
    fun onMenu(menu: RemoteRebootPolicy.Menu): Boolean {
        if (!menu.enabled || phase == Phase.Requesting || phase == Phase.Confirming) return false
        phase = Phase.Confirming
        return true
    }

    fun onCancel() {
        if (phase == Phase.Confirming) phase = Phase.Idle
    }

    /** "다시 시작" in the popup: exactly one request. A second tap of the same popup sends nothing. */
    fun onConfirm(): Boolean {
        if (phase != Phase.Confirming) return false
        val id = send()
        sends++
        if (id == 0) {
            phase = Phase.Unknown
            return false
        }
        requestId = id
        phase = Phase.Requesting
        return true
    }

    /** 85 for this request: outcome = RebootResult (0..3), or OUTCOME_NO_REPLY when the link gave none. */
    fun onOutcome(id: Int, outcome: Int, win32: Long) {
        if (phase != Phase.Requesting || id != requestId) return
        this.win32 = win32
        phase = when (outcome) {
            0 -> Phase.Accepted
            1 -> Phase.Refused
            2 -> Phase.NotAuthorized
            3 -> Phase.Failed
            else -> Phase.Unknown
        }
    }

    /** Back to the start (the request's state has been shown after the disconnect). */
    fun reset() {
        phase = Phase.Idle
        requestId = 0
        win32 = 0
    }

    /** The connection went. After a request it is what a restart does: a state, not an error. */
    fun onDisconnected(): Boolean = phase == Phase.Requesting || phase == Phase.Accepted || phase == Phase.Unknown

    fun statusText(): String? = when (phase) {
        Phase.Idle, Phase.Confirming -> null
        Phase.Requesting -> "다시 시작을 요청하는 중…"
        Phase.Accepted -> "재시작 요청을 받았습니다. 원격 PC가 다시 켜지면 다시 접속하세요."
        Phase.Refused -> "원격 PC가 다시 시작 요청을 받지 않았습니다 (이미 진행 중이거나 허용되지 않음)."
        Phase.NotAuthorized -> "인증된 연결에서만 원격 PC를 다시 시작할 수 있습니다."
        Phase.Failed -> "원격 PC가 다시 시작하지 못했습니다 (오류 $win32)."
        Phase.Unknown -> "다시 시작 요청의 처리 여부를 확인하지 못했습니다. 자동으로 다시 요청하지 않습니다."
    }

    companion object {
        const val OUTCOME_NO_REPLY = 100
    }
}
