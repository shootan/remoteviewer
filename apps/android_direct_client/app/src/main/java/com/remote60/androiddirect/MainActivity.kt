package com.remote60.androiddirect

import android.app.Activity
import android.app.AlertDialog
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.graphics.drawable.ColorDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.inputmethod.InputMethodManager
import android.graphics.Bitmap
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.GridView
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import org.json.JSONException
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.roundToInt

class MainActivity : Activity(), TextureView.SurfaceTextureListener {
    companion object {
        private const val LOG_TAG = "remote60_android_direct"
        private const val INPUT_KIND_MOUSE_MOVE = 1
        private const val INPUT_KIND_MOUSE_DOWN = 2
        private const val INPUT_KIND_MOUSE_UP = 3
        private const val INPUT_KIND_MOUSE_WHEEL = 4
        private const val INPUT_KIND_KEY_DOWN = 5
        private const val INPUT_KIND_KEY_UP = 6
        private const val INPUT_BUTTON_PRIMARY = 0x1
        private const val INPUT_BUTTON_SECONDARY = 0x2
        private const val INPUT_BUTTON_MIDDLE = 0x4
        private const val INPUT_VK_LBUTTON = 0x01
        private const val INPUT_VK_RBUTTON = 0x02
        private const val INPUT_VK_MBUTTON = 0x04
        private const val INPUT_VK_BACK = 0x08

        /** Held around a wheel event to turn scrolling into zooming, as applications expect. */
        private const val INPUT_VK_CONTROL = 0x11
        private const val INPUT_WHEEL_DELTA_STEP = 120
        // Authored in dp; converted per-device below. As a raw pixel constant one wheel
        // notch needed 4x more finger travel on a 4x-density phone than on a 1x tablet.
        private const val SCROLL_GESTURE_STEP_DP = 28f

        /** Matches kWindowThumbnailMaxWidth/Height in the wire protocol. */
        private const val THUMBNAIL_MAX_EDGE = 320

        private const val MACRO_STATE_IDLE = 0
        private const val MACRO_STATE_RECORDING = 1
        private const val MACRO_STATE_PLAYING = 2
        private const val MACRO_LIST_MAX_ROWS = 300
        private const val VIEWER_STALL_OVERLAY_US = 3_000_000L
    }

    private var lastVideoOutputPtsUs = 0L
    /**
     * What the letterbox margin does, split by where along it you press.
     *
     * The bars beside the picture are dead space -- taps there map to nothing -- so they are free
     * modifiers. One behaviour for the whole bar wasted that: three zones give right-click,
     * tablet gestures and the on-screen mouse without stealing any room from the picture.
     */
    private enum class MarginZone { TOP, MIDDLE, BOTTOM }

    /** Pointer held in the top zone: the next touch on the picture is a right click. */
    private var rightClickModifierPointerId = MotionEvent.INVALID_POINTER_ID

    /** Pointer held in the middle zone: touches behave like a tablet (scroll, pinch zoom). */
    private var tabletModePointerId = MotionEvent.INVALID_POINTER_ID
    private var rightClickHintShown = false
    /** User override: keep the phone upright even when the remote screen is landscape. */
    private var forcePortrait = false
    private var lastAppliedLandscape: Boolean? = null
    private var sessionBytesReceived = 0L
    private var quickSettingsDialog: AlertDialog? = null

    /**
     * Lay the control rail along the device's short edge. Always safe to call: it only
     * reflows views and never touches the requested orientation.
     */
    private fun applyViewerRailLayout() {
        if (!::viewerSplit.isInitialized) return
        val deviceLandscape =
            resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        viewerSplit.orientation =
            if (deviceLandscape) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
        val lp = viewerControlsBar.layoutParams as LinearLayout.LayoutParams
        if (deviceLandscape) {
            lp.width = LinearLayout.LayoutParams.WRAP_CONTENT
            lp.height = LinearLayout.LayoutParams.MATCH_PARENT
            viewerControlsBar.orientation = LinearLayout.VERTICAL
        } else {
            lp.width = LinearLayout.LayoutParams.MATCH_PARENT
            lp.height = LinearLayout.LayoutParams.WRAP_CONTENT
            viewerControlsBar.orientation = LinearLayout.HORIZONTAL
        }
        viewerControlsBar.layoutParams = lp

        // Every direct child of the split carries sizes written for one orientation. Left as
        // they were, a portrait split gave the 34dp-wide, full-height zone strip the entire
        // column and the video a width of zero: a black screen with no controls and no way
        // back, which is exactly what the rotate button used to produce.
        val videoFrame = findViewById<View>(R.id.viewerVideoFrame)
        videoFrame.layoutParams = if (deviceLandscape) {
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f)
        } else {
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
        }

        val zoneBar = findViewById<LinearLayout>(R.id.viewerZoneBar)
        val stripPx = dp(34f)
        zoneBar.orientation =
            if (deviceLandscape) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
        zoneBar.layoutParams = if (deviceLandscape) {
            LinearLayout.LayoutParams(stripPx, LinearLayout.LayoutParams.MATCH_PARENT)
        } else {
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, stripPx)
        }
        for (i in 0 until zoneBar.childCount) {
            val child = zoneBar.getChildAt(i)
            val divider = child !is TextView
            child.layoutParams = when {
                deviceLandscape && divider ->
                    LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1f))
                deviceLandscape ->
                    LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
                divider ->
                    LinearLayout.LayoutParams(dp(1f), LinearLayout.LayoutParams.MATCH_PARENT)
                else ->
                    LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f)
            }
        }

        viewerRotateButton.text =
            if (forcePortrait) "PORT" else getString(R.string.viewer_rotate_button)
    }

    /**
     * Match the device orientation to the remote screen.
     *
     * Only runs once a frame has actually decoded and the viewer is settled. Rotating the
     * device destroys and recreates the TextureView's SurfaceTexture, which drops the
     * decoder's output surface; doing that while a selection is still waiting for its first
     * frame made the handshake time out and bounced the user back to the target list.
     */
    private fun applyViewerOrientation() {
        applyViewerRailLayout()
        if (currentScene != UiScene.VIEWER) return
        if (videoWidth <= 0 || videoHeight <= 0) return
        // Normally applyOrientationForContent already settled this at selection time; this
        // only corrects the rare case where the listed size disagreed with what decoded.
        val wantLandscape = videoWidth >= videoHeight && !forcePortrait
        if (lastAppliedLandscape == wantLandscape) return
        lastAppliedLandscape = wantLandscape
        requestedOrientation =
            if (wantLandscape) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            else ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
        diagnosticsLog.log(
            "orientation_corrected",
            "decoded=${videoWidth}x${videoHeight} landscape=$wantLandscape"
        )
    }

    /**
     * Point the device at the shape of the target before opening the viewer.
     *
     * The window list already carries each target's client size, so the orientation is known
     * at selection time. Committing to it here means the viewer surface is created once, in
     * its final orientation, and no rotation happens while video is flowing. Desktop targets
     * have no listed size and monitors are landscape, so they default that way.
     */
    private fun applyOrientationForContent(width: Int, height: Int, tab: TargetTab) {
        val landscapeContent = when {
            width > 0 && height > 0 -> width >= height
            tab == TargetTab.DESKTOP -> true
            else -> return
        }
        val wantLandscape = landscapeContent && !forcePortrait
        lastAppliedLandscape = wantLandscape
        requestedOrientation =
            if (wantLandscape) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            else ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
        diagnosticsLog.log(
            "orientation_preset",
            "expected=${width}x${height} tab=$tab landscape=$wantLandscape forcePortrait=$forcePortrait"
        )
    }

    /** Let the next viewer entry re-evaluate orientation from scratch. */
    private fun resetViewerOrientationState() {
        lastAppliedLandscape = null
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
    }

    private fun formatMegabytes(bytes: Long): String {
        val mb = bytes.toDouble() / 1_000_000.0
        return if (mb >= 100) String.format(Locale.US, "%.0f", mb)
        else String.format(Locale.US, "%.1f", mb)
    }

    private fun renderDataUsage() {
        sessionBytesReceived = NativeSessionBridge.nativeGetSessionBytesReceived()
        viewerDataUsageText.text = getString(R.string.viewer_data_usage, formatMegabytes(sessionBytesReceived))
    }

    /** Quick settings popup: bitrate/fps presets and the orientation override in one place. */
    private fun showQuickSettingsDialog() {
        if (quickSettingsDialog?.isShowing == true) return
        val items = arrayOf(
            getString(R.string.quick_preset_mobile),
            getString(R.string.quick_preset_balanced),
            getString(R.string.quick_preset_sharp),
            if (forcePortrait) getString(R.string.quick_orientation_portrait)
            else getString(R.string.quick_orientation_auto),
            // The rail ran out of room, so the rarely-needed diagnostics live here now.
            getString(R.string.quick_diagnostics_log),
        )
        quickSettingsDialog = AlertDialog.Builder(this)
            .setTitle(R.string.quick_settings_title)
            .setItems(items) { _, which ->
                when (which) {
                    0 -> applyQuickPreset(3000, 15)
                    1 -> applyQuickPreset(6000, 30)
                    2 -> applyQuickPreset(8000, 30)
                    3 -> {
                        forcePortrait = !forcePortrait
                        lastAppliedLandscape = null
                        applyOrientationForContent(
                            if (videoWidth > 0) videoWidth else expectedContentWidth,
                            if (videoHeight > 0) videoHeight else expectedContentHeight,
                            pendingSelectionTab,
                        )
                        applyViewerRailLayout()
                    }
                    4 -> toggleViewerLogDialog()
                }
                renderStatus()
            }
            .setNegativeButton(R.string.quick_close, null)
            .create()
        quickSettingsDialog?.setOnDismissListener { quickSettingsDialog = null }
        quickSettingsDialog?.show()
    }

    private fun applyQuickPreset(bitrateKbps: Int, fps: Int) {
        requestedRuntimeBitrateKbps = bitrateKbps
        requestedRuntimeFps = fps
        settingsBitrateInput.setText(bitrateKbps.toString())
        settingsFpsInput.setText(fps.toString())
        NativeSessionBridge.nativeRequestRuntimeConfig(bitrateKbps * 1000, fps)
        settingsStatusMessage =
            "Current request: ${bitrateKbps} kbps / ${fps} fps / desktop ${requestedDesktopBackend.label}"
        saveCurrentEndpoint()
        diagnosticsLog.log("quick-preset", "bitrateKbps=$bitrateKbps fps=$fps")
    }

    private var lastVideoOutputSeenUs = 0L

    private val scrollGestureStepPx: Float
        get() = SCROLL_GESTURE_STEP_DP * resources.displayMetrics.density

    private enum class UiScene {
        LOGIN,
        HOSTS,
        CONNECT,
        TARGETS,
        SWITCHING,
        VIEWER,
    }

    private enum class TargetTab {
        WINDOWS,
        DESKTOP,
        SETTINGS,
    }

    private enum class SelectionStage {
        IDLE,
        REQUESTING,
        WAITING_FIRST_FRAME,
    }

    private enum class ViewerTouchMode {
        DIRECT,
        SCROLL,
    }

    private enum class DesktopCaptureBackendOption(val code: Int, val label: String) {
        DXGI(1, "DXGI"),
        WGC(2, "WGC");

        companion object {
            fun fromCode(code: Int): DesktopCaptureBackendOption =
                values().firstOrNull { it.code == code } ?: WGC
        }
    }

    private data class WindowPanelItem(
        val id: Long,
        val title: String,
        val width: Int,
        val height: Int,
        val minimized: Boolean,
        val thumbVersion: Long = 0L,
    )

    private data class WindowPanelUiSnapshot(
        val selectedId: Long,
        val selectedTitle: String,
        val selectedWidth: Int,
        val selectedHeight: Int,
        val selectionLocked: Boolean,
        val status: String,
        val lastSelectSeq: Int,
        val lastSelectOk: Boolean,
        val lastSelectWindowId: Long,
        val lastSelectStreamGeneration: Long,
        val lastSelectHostSendQpcUs: Long,
        val items: List<WindowPanelItem>,
    ) {
        companion object {
            val EMPTY = WindowPanelUiSnapshot(
                selectedId = 0L,
                selectedTitle = "desktop",
                selectedWidth = 0,
                selectedHeight = 0,
                selectionLocked = false,
                status = "waiting_control",
                lastSelectSeq = 0,
                lastSelectOk = false,
                lastSelectWindowId = 0L,
                lastSelectStreamGeneration = 0L,
                lastSelectHostSendQpcUs = 0L,
                items = emptyList(),
            )
        }
    }

    private data class ViewerContentRect(
        val left: Float,
        val top: Float,
        val width: Float,
        val height: Float,
        val contentWidth: Int,
        val contentHeight: Int,
    )

    private lateinit var diagnosticsLog: SessionDiagnosticsLog
    private lateinit var loginScene: View
    private lateinit var hostsScene: View
    private lateinit var connectScene: View
    private lateinit var targetsScene: View
    private lateinit var viewerScene: View
    private lateinit var loginServerInput: EditText
    private lateinit var loginIdInput: EditText
    private lateinit var loginPasswordInput: EditText
    private lateinit var loginButton: Button
    private lateinit var loginErrorText: TextView
    private lateinit var loginManualButton: Button
    private lateinit var hostsTitleText: TextView
    private lateinit var hostsStatusText: TextView
    private lateinit var hostsListView: ListView
    private lateinit var hostsEmptyText: TextView
    private lateinit var hostsRefreshButton: Button
    private lateinit var hostsLogoutButton: Button
    private lateinit var hostEdit: EditText
    private lateinit var videoPortEdit: EditText
    private lateinit var controlPortEdit: EditText
    private lateinit var connectStatusText: TextView
    private lateinit var connectErrorText: TextView
    private lateinit var listSelectedText: TextView
    private lateinit var listStatusText: TextView
    private lateinit var targetListEmptyText: TextView
    private lateinit var targetListView: GridView
    private lateinit var listDisconnectButton: Button
    private lateinit var listWindowsButton: Button
    private lateinit var listDevicesButton: Button
    private lateinit var listSettingsButton: Button
    private lateinit var listRefreshButton: Button
    private lateinit var settingsPanel: View
    private lateinit var settingsBitrateInput: EditText
    private lateinit var settingsFpsInput: EditText
    private lateinit var settingsDesktopBackendDxgiButton: Button
    private lateinit var settingsDesktopBackendWgcButton: Button
    private lateinit var settingsApplyButton: Button
    private lateinit var settingsAppliedText: TextView
    private lateinit var viewerControlsBar: LinearLayout
    private lateinit var viewerBackButton: Button
    private lateinit var viewerKeyboardButton: Button
    private lateinit var viewerOverlayStatusText: TextView
    private lateinit var viewerSplit: LinearLayout
    private lateinit var viewerRotateButton: Button
    private lateinit var viewerKeysButton: Button
    private var viewerKeyPanel: ViewerKeyPanel? = null
    private lateinit var viewerMenuButton: Button
    private lateinit var viewerDataUsageText: TextView
    private lateinit var viewerLoadingPanel: View
    private lateinit var viewerLoadingText: TextView
    private lateinit var viewerImeCaptureView: ImeCaptureView
    private lateinit var viewerModeBanner: TextView
    private lateinit var videoTextureView: TextureView
    private lateinit var targetListAdapter: TargetCardAdapter
    private val targetListLabels = mutableListOf<String>()
    private val targetListIds = mutableListOf<Long>()
    private var targetListSelectedId = 0L
    private val thumbnailBitmaps = HashMap<Long, Bitmap>()
    private val thumbnailVersions = HashMap<Long, Long>()

    /** Card grid adapter: preview image on top, one-line title underneath. */
    private inner class TargetCardAdapter : BaseAdapter() {
        override fun getCount(): Int = targetListIds.size
        override fun getItem(position: Int): Any = targetListLabels[position]
        override fun getItemId(position: Int): Long = targetListIds[position]

        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val view = convertView
                ?: layoutInflater.inflate(R.layout.target_card, parent, false)
            val id = targetListIds[position]
            view.findViewById<TextView>(R.id.targetCardTitle).text = targetListLabels[position]
            val image = view.findViewById<ImageView>(R.id.targetCardThumbnail)
            val bmp = thumbnailBitmaps[id]
            if (bmp != null) {
                image.setImageBitmap(bmp)
            } else {
                image.setImageDrawable(null)
            }
            view.isActivated = id == targetListSelectedId
            return view
        }
    }

    /** Pull changed previews across JNI and decode them into reusable bitmaps. */
    private fun refreshThumbnails(items: List<WindowPanelItem>) {
        var changed = false
        val wanted = HashSet<Long>()
        val entries = ArrayList<Pair<Long, Long>>(items.size + 1)
        entries.add(0L to 0L)  // desktop preview has no version marker; fetch when absent
        items.forEach { entries.add(it.id to it.thumbVersion) }
        for ((id, version) in entries) {
            wanted.add(id)
            val known = thumbnailVersions[id]
            if (known != null && known == version && thumbnailBitmaps.containsKey(id)) continue
            if (version == 0L && thumbnailBitmaps.containsKey(id)) continue
            val raw = NativeSessionBridge.nativeGetWindowThumbnail(id) ?: continue
            if (raw.size < 8) continue
            val header = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            val w = header.int
            val h = header.int
            // Long arithmetic, and an upper bound from the protocol: a corrupt header would
            // otherwise overflow the size check or ask for an absurd allocation.
            val pixelBytes = w.toLong() * h.toLong() * 4L
            if (w <= 0 || h <= 0 || w > THUMBNAIL_MAX_EDGE || h > THUMBNAIL_MAX_EDGE) continue
            if (raw.size < 8L + pixelBytes) continue
            val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
            // Copied into a direct buffer starting at zero rather than handing over the wrapped
            // array with its position at 8. That form crashed inside the framework's critical
            // array release, taking the whole app down as soon as a preview arrived.
            val pixels = ByteBuffer.allocateDirect(pixelBytes.toInt()).order(ByteOrder.LITTLE_ENDIAN)
            pixels.put(raw, 8, pixelBytes.toInt())
            pixels.rewind()
            bmp.copyPixelsFromBuffer(pixels)
            thumbnailBitmaps[id] = bmp
            thumbnailVersions[id] = version
            changed = true
        }
        val stale = thumbnailBitmaps.keys.filter { it !in wanted }
        if (stale.isNotEmpty()) {
            stale.forEach {
                thumbnailBitmaps.remove(it)
                thumbnailVersions.remove(it)
            }
            changed = true
        }
        if (changed) targetListAdapter.notifyDataSetChanged()
    }
    private var currentScene = UiScene.LOGIN
    private val directoryExecutor = Executors.newSingleThreadExecutor()
    private var directoryHosts: List<DirectoryClient.Host> = emptyList()
    private lateinit var hostListAdapter: HostCardAdapter
    private var directoryBusy = false
    /** Set while a directory-brokered connection is being established, to keep the UI honest. */
    private var directoryConnectingName = ""
    /**
     * The user asked to type an address instead of signing in. Remembered so that hanging up
     * returns them to the manual form rather than to a PC list they chose not to use.
     */
    private var manualConnectMode = false
    private var activeTargetTab = TargetTab.WINDOWS
    private var connectFlowActive = false
    private var selectionStage = SelectionStage.IDLE
    private var selectionGenerationCounter = 0L
    private var pendingSelectionId: Long? = null
    private var pendingSelectionLabel = ""
    private var pendingSelectionTab = TargetTab.WINDOWS
    private var pendingSelectionGeneration = 0L
    private var pendingSelectionStartedAtMs = 0L
    private var pendingSelectionAckLogged = false
    private var lastLoggedScene = UiScene.CONNECT
    private var lastLoggedStatus = ""
    private var lastLoggedPanelStatus = ""
    private var lastLoggedVideoDebug = ""
    private var lastViewerOutCount = -1
    private var lastViewerOutChangeAtMs = 0L
    private var lastViewerStallLogAtMs = 0L
    private var lastViewerRecoveryTargetId = Long.MIN_VALUE
    private var lastViewerRecoveryAtMs = 0L
    private var lastViewerRecoveryAttempts = 0
    private var requestedRuntimeBitrateKbps = 8000
    private var requestedRuntimeFps = 30
    private var requestedDesktopBackend = DesktopCaptureBackendOption.WGC
    private var settingsStatusMessage = "Current request: 8000 kbps / 30 fps / desktop WGC"
    private var pendingRuntimeConfigSync = false
    private var pendingDesktopBackendSync = false
    private var desiredStreamActive = false
    private var lastAppliedStreamActive: Boolean? = null
    private var videoSurface: Surface? = null
    private var videoWidth = 0
    private var videoHeight = 0
    private var expectedContentWidth = 0
    private var expectedContentHeight = 0
    private var surfaceBufferWidth = 0
    private var surfaceBufferHeight = 0
    private var exitDialog: AlertDialog? = null
    private var viewerLogDialog: AlertDialog? = null
    private var viewerLogStatusText: TextView? = null
    private var viewerLogTextView: TextView? = null
    private var activeTouchPointerId = MotionEvent.INVALID_POINTER_ID
    private var activeViewerTouchMode = ViewerTouchMode.DIRECT
    private var virtualMouse: ViewerVirtualMouse? = null
    /** Where the on-screen mouse thinks the remote pointer is, in remote pixels. */
    private var virtualPointerX = -1
    private var virtualPointerY = -1
    /** Finger spread at the start of a pinch, for the zoom gesture in tablet mode. */
    private var pinchStartSpan = 0f
    private var pinchZoomCarry = 0f
    private var activeTouchButtons = 0
    private var activeTouchIsSecondary = false
    private var lastTouchVideoX = 0
    private var lastTouchVideoY = 0
    private var scrollLastTouchY = 0f
    private var scrollWheelCarryPx = 0f

    // The dedicated left strip. Its finger lives in a different view's event stream than the
    // picture's, so holding a zone cannot disturb the touch bookkeeping above -- which is also
    // why these are booleans and not pointer ids.
    private lateinit var viewerZoneRightClick: TextView
    private lateinit var viewerZoneTablet: TextView
    private lateinit var viewerTabletLockButton: TextView
    private var zoneRightClickHeld = false
    private var zoneTabletHeld = false

    /** Pinned tablet mode: survives the holding finger lifting, until the lock is tapped off. */
    private var zoneTabletLocked = false
    private val viewerControlsDimAlpha = 1.0f  // rail sits beside the video, nothing to uncover
    private val viewerControlsFadeRunnable = Runnable {
        if (currentScene == UiScene.VIEWER) {
            viewerControlsBar.animate().alpha(viewerControlsDimAlpha).setDuration(220L).start()
        }
    }

    private val statusHandler = Handler(Looper.getMainLooper())
    private val statusPollRunnable = object : Runnable {
        override fun run() {
            renderStatus()
            statusHandler.postDelayed(this, 250L)
        }
    }

    private var macroDialog: AlertDialog? = null
    private var macroStatusText: TextView? = null
    private var macroStepListContainer: LinearLayout? = null
    private var macroRecordButton: Button? = null
    private var macroPlayButton: Button? = null
    private var macroPauseButton: Button? = null
    private lateinit var macroRecordBar: View
    private lateinit var macroRecordBarText: TextView
    private lateinit var macroRecordBarPause: Button

    /**
     * Drives macro playback from the UI thread at a fine cadence.
     *
     * A macro's steps are milliseconds apart, so the 250 ms status poll is far too coarse to
     * dispatch them. Running here rather than on a thread of its own means playback cannot
     * outlive the screen it belongs to.
     */
    private val macroPumpRunnable = object : Runnable {
        override fun run() {
            val state = NativeSessionBridge.nativeMacroState()
            if (state == MACRO_STATE_PLAYING) {
                NativeSessionBridge.nativeMacroPump()
            }
            renderMacroUi()
            if (state != MACRO_STATE_IDLE) {
                statusHandler.postDelayed(this, 16L)
            } else {
                macroPumpScheduled = false
            }
        }
    }
    private var macroPumpScheduled = false

    /** What the step list currently shows; rebuilding 16 times a second would jank the pump. */
    private var macroListRenderedSignature: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        applyImmersiveMode()

        diagnosticsLog = SessionDiagnosticsLog(this)
        loginScene = findViewById(R.id.loginScene)
        hostsScene = findViewById(R.id.hostsScene)
        loginServerInput = findViewById(R.id.loginServerInput)
        loginIdInput = findViewById(R.id.loginIdInput)
        loginPasswordInput = findViewById(R.id.loginPasswordInput)
        loginButton = findViewById(R.id.loginButton)
        loginErrorText = findViewById(R.id.loginErrorText)
        loginManualButton = findViewById(R.id.loginManualButton)
        hostsTitleText = findViewById(R.id.hostsTitleText)
        hostsStatusText = findViewById(R.id.hostsStatusText)
        hostsListView = findViewById(R.id.hostsListView)
        hostsEmptyText = findViewById(R.id.hostsEmptyText)
        hostsRefreshButton = findViewById(R.id.hostsRefreshButton)
        hostsLogoutButton = findViewById(R.id.hostsLogoutButton)
        connectScene = findViewById(R.id.connectScene)
        targetsScene = findViewById(R.id.targetsScene)
        viewerScene = findViewById(R.id.viewerScene)
        hostEdit = findViewById(R.id.hostInput)
        videoPortEdit = findViewById(R.id.videoPortInput)
        controlPortEdit = findViewById(R.id.controlPortInput)
        connectStatusText = findViewById(R.id.connectStatusText)
        connectErrorText = findViewById(R.id.connectErrorText)
        listSelectedText = findViewById(R.id.listSelectedText)
        listStatusText = findViewById(R.id.listStatusText)
        targetListEmptyText = findViewById(R.id.targetListEmptyText)
        targetListView = findViewById(R.id.targetListView)
        listDisconnectButton = findViewById(R.id.listDisconnectButton)
        listWindowsButton = findViewById(R.id.listWindowsButton)
        listDevicesButton = findViewById(R.id.listDevicesButton)
        listSettingsButton = findViewById(R.id.listSettingsButton)
        listRefreshButton = findViewById(R.id.listRefreshButton)
        settingsPanel = findViewById(R.id.settingsPanel)
        settingsBitrateInput = findViewById(R.id.settingsBitrateInput)
        settingsFpsInput = findViewById(R.id.settingsFpsInput)
        settingsDesktopBackendDxgiButton = findViewById(R.id.settingsDesktopBackendDxgiButton)
        settingsDesktopBackendWgcButton = findViewById(R.id.settingsDesktopBackendWgcButton)
        settingsApplyButton = findViewById(R.id.settingsApplyButton)
        settingsAppliedText = findViewById(R.id.settingsAppliedText)
        viewerControlsBar = findViewById(R.id.viewerControlsBar)
        viewerBackButton = findViewById(R.id.viewerBackButton)
        viewerKeyboardButton = findViewById(R.id.viewerKeyboardButton)
        viewerOverlayStatusText = findViewById(R.id.viewerOverlayStatusText)
        viewerSplit = findViewById(R.id.viewerSplit)
        viewerControlsBar = findViewById(R.id.viewerControlsBar)
        viewerRotateButton = findViewById(R.id.viewerRotateButton)
        viewerMenuButton = findViewById(R.id.viewerMenuButton)
        viewerDataUsageText = findViewById(R.id.viewerDataUsageText)
        viewerRotateButton.setOnClickListener {
            forcePortrait = !forcePortrait
            lastAppliedLandscape = null
            applyOrientationForContent(
                if (videoWidth > 0) videoWidth else expectedContentWidth,
                if (videoHeight > 0) videoHeight else expectedContentHeight,
                pendingSelectionTab,
            )
            applyViewerRailLayout()
            renderStatus()
        }
        viewerMenuButton.setOnClickListener { showQuickSettingsDialog() }
        viewerKeysButton = findViewById(R.id.viewerKeysButton)
        viewerKeyPanel = ViewerKeyPanel(this, findViewById(R.id.viewerKeyPanel)) { vk, down ->
            queueViewerSpecialKey(vk, if (down) KeyEvent.ACTION_DOWN else KeyEvent.ACTION_UP)
        }
        viewerKeysButton.setOnClickListener {
            showViewerControls(emphasized = true)
            viewerKeyPanel?.toggle()
        }
        viewerLoadingPanel = findViewById(R.id.viewerLoadingPanel)
        viewerLoadingText = findViewById(R.id.viewerLoadingText)
        viewerImeCaptureView = findViewById(R.id.viewerImeCaptureView)
        videoTextureView = findViewById(R.id.videoTextureView)

        videoTextureView.surfaceTextureListener = this
        videoTextureView.isOpaque = true
        videoTextureView.isFocusableInTouchMode = true
        videoTextureView.setOnTouchListener { view, event ->
            handleViewerTouch(view, event)
        }
        videoTextureView.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            syncVideoSurface(forceRebind = false)
        }
        viewerImeCaptureView.listener =
            object : ImeCaptureView.Listener {
                override fun onCommitText(text: CharSequence) {
                    queueViewerCommittedText(text)
                }

                override fun onDeleteBackward(count: Int) {
                    repeat(count.coerceAtMost(8)) {
                        queueViewerSpecialKey(INPUT_VK_BACK, KeyEvent.ACTION_DOWN)
                        queueViewerSpecialKey(INPUT_VK_BACK, KeyEvent.ACTION_UP)
                    }
                }

                override fun onSpecialKey(keyCode: Int, action: Int) {
                    val windowsVk = mapAndroidKeyCodeToWindowsVk(keyCode) ?: return
                    queueViewerSpecialKey(windowsVk, action)
                }
            }

        targetListAdapter = TargetCardAdapter()
        targetListView.adapter = targetListAdapter
        targetListView.emptyView = targetListEmptyText
        targetListView.setOnItemClickListener { _, _, position, _ ->
            if (position < 0 || position >= targetListIds.size) return@setOnItemClickListener
            val targetId = targetListIds[position]
            startSelectionTransition(targetId, targetListLabels[position], activeTargetTab, "tap")
            renderStatus()
        }

        viewerModeBanner = findViewById(R.id.viewerModeBanner)
        initVirtualMouse()
        initZoneBar()
        macroRecordBar = findViewById(R.id.macroRecordBar)
        macroRecordBarText = findViewById(R.id.macroRecordBarText)
        macroRecordBarPause = findViewById(R.id.macroRecordBarPause)
        macroRecordBarPause.setOnClickListener { toggleMacroPause() }
        findViewById<Button>(R.id.macroRecordBarStop).setOnClickListener { stopMacroRecording() }
        findViewById<Button>(R.id.viewerMacroButton).setOnClickListener { showMacroDialog() }
        initDirectoryUi()

        val savedEndpoint = SessionPersistence.load(this)
        val launchHost = intent.getStringExtra("host")?.trim().orEmpty()
        val launchVideoPort = intent.getIntExtra("videoPort", 0)
        val launchControlPort = intent.getIntExtra("controlPort", 0)
        hostEdit.setText(if (launchHost.isNotEmpty()) launchHost else savedEndpoint.host.ifEmpty { "192.168.0.10" })
        videoPortEdit.setText(if (launchVideoPort > 0) launchVideoPort.toString() else savedEndpoint.videoPort.toString())
        controlPortEdit.setText(if (launchControlPort > 0) launchControlPort.toString() else savedEndpoint.controlPort.toString())
        requestedRuntimeBitrateKbps = savedEndpoint.bitrateKbps
        requestedRuntimeFps = savedEndpoint.fps
        requestedDesktopBackend = DesktopCaptureBackendOption.fromCode(savedEndpoint.desktopBackendCode)
        settingsStatusMessage =
            "Current request: ${requestedRuntimeBitrateKbps} kbps / ${requestedRuntimeFps} fps / " +
                "desktop ${requestedDesktopBackend.label}"
        settingsBitrateInput.setText(requestedRuntimeBitrateKbps.toString())
        settingsFpsInput.setText(requestedRuntimeFps.toString())
        settingsAppliedText.text = settingsStatusMessage
        updateDesktopBackendButtons()

        diagnosticsLog.log(
            "app_start",
            "savedHost=${savedEndpoint.host} savedVideoPort=${savedEndpoint.videoPort} " +
                "savedControlPort=${savedEndpoint.controlPort} logFile=${diagnosticsLog.filePath()}"
        )

        findViewById<Button>(R.id.connectButton).setOnClickListener {
            val host = hostEdit.text?.toString()?.trim().orEmpty()
            val videoPort = videoPortEdit.text?.toString()?.toIntOrNull() ?: 0
            val controlPort = controlPortEdit.text?.toString()?.toIntOrNull() ?: 0
            saveCurrentEndpoint()
            diagnosticsLog.log("connect_tap", "host=$host videoPort=$videoPort controlPort=$controlPort")

            val ok = NativeSessionBridge.nativeConnect(host, videoPort, controlPort)
            if (ok) {
                connectFlowActive = true
                pendingRuntimeConfigSync = true
                pendingDesktopBackendSync = true
                desiredStreamActive = false
                lastAppliedStreamActive = null
                clearPendingSelection()
                currentScene = UiScene.TARGETS
                NativeSessionBridge.nativeRequestWindowList()
            } else {
                diagnosticsLog.log("connect_failed", NativeSessionBridge.nativeGetLastError())
            }
            renderStatus()
        }

        listDisconnectButton.setOnClickListener {
            diagnosticsLog.log("disconnect_tap", "scene=$currentScene")
            cancelActiveViewerTouch("disconnect")
            hideViewerKeyboard("disconnect")
            desiredStreamActive = false
            pendingRuntimeConfigSync = false
            lastAppliedStreamActive = null
            NativeSessionBridge.nativeDisconnect()
            connectFlowActive = false
            clearPendingSelection()
            resetViewerObservability()
            currentScene = homeScene()
            if (currentScene == UiScene.HOSTS) loadHosts("disconnect")
            renderStatus()
        }

        listRefreshButton.setOnClickListener {
            diagnosticsLog.log("refresh_tap", "scene=$currentScene")
            NativeSessionBridge.nativeRequestWindowList()
            renderStatus()
        }

        listWindowsButton.setOnClickListener {
            activeTargetTab = TargetTab.WINDOWS
            diagnosticsLog.log("tab_switch", "tab=windows")
            NativeSessionBridge.nativeRequestWindowList()
            renderStatus()
        }

        listDevicesButton.setOnClickListener {
            activeTargetTab = TargetTab.DESKTOP
            diagnosticsLog.log("tab_switch", "tab=desktop")
            renderStatus()
        }

        listSettingsButton.setOnClickListener {
            activeTargetTab = TargetTab.SETTINGS
            diagnosticsLog.log("tab_switch", "tab=settings")
            renderStatus()
        }

        settingsDesktopBackendDxgiButton.setOnClickListener {
            requestedDesktopBackend = DesktopCaptureBackendOption.DXGI
            updateDesktopBackendButtons()
        }
        settingsDesktopBackendWgcButton.setOnClickListener {
            requestedDesktopBackend = DesktopCaptureBackendOption.WGC
            updateDesktopBackendButtons()
        }

        settingsApplyButton.setOnClickListener {
            val bitrateKbps = settingsBitrateInput.text?.toString()?.trim()?.toIntOrNull() ?: 0
            val fps = settingsFpsInput.text?.toString()?.trim()?.toIntOrNull() ?: 0
            val messages = mutableListOf<String>()
            if (bitrateKbps < 300 || fps !in 1..120) {
                messages += "Use bitrate >= 300 kbps and fps between 1 and 120."
                diagnosticsLog.log("runtime_config_invalid", "bitrateKbps=$bitrateKbps fps=$fps")
            } else {
                val bitrateBps = bitrateKbps * 1000
                val runtimeOk = NativeSessionBridge.nativeRequestRuntimeConfig(bitrateBps, fps)
                if (runtimeOk) {
                    requestedRuntimeBitrateKbps = bitrateKbps
                    requestedRuntimeFps = fps
                    diagnosticsLog.log("runtime_config_request", "bitrateBps=$bitrateBps fps=$fps")
                } else {
                    messages += "Runtime config request failed."
                    diagnosticsLog.log("runtime_config_failed", "bitrateBps=$bitrateBps fps=$fps")
                }
            }

            val backendOk =
                NativeSessionBridge.nativeRequestDesktopCaptureBackend(requestedDesktopBackend.code)
            if (backendOk) {
                pendingDesktopBackendSync = false
                diagnosticsLog.log(
                    "desktop_backend_request",
                    "backend=${requestedDesktopBackend.label.lowercase()}"
                )
            } else {
                messages += "Desktop backend request failed."
                diagnosticsLog.log(
                    "desktop_backend_failed",
                    "backend=${requestedDesktopBackend.label.lowercase()}"
                )
            }
            if (messages.isEmpty()) {
                settingsStatusMessage =
                    "Requested: ${requestedRuntimeBitrateKbps} kbps / ${requestedRuntimeFps} fps / " +
                        "desktop ${requestedDesktopBackend.label}"
            } else {
                settingsStatusMessage = messages.joinToString(" / ")
            }
            settingsAppliedText.text = settingsStatusMessage
            saveCurrentEndpoint()
            renderStatus()
        }

        viewerBackButton.setOnClickListener {
            showViewerControls(emphasized = true)
            handleViewerBack("viewer_back")
        }
        viewerKeyboardButton.setOnClickListener {
            toggleViewerKeyboard()
        }
        renderStatus()
    }

    override fun onBackPressed() {
        handleSystemBack()
    }

    override fun onResume() {
        super.onResume()
        statusHandler.removeCallbacks(statusPollRunnable)
        statusHandler.post(statusPollRunnable)
        applyImmersiveMode()
        syncVideoSurface(forceRebind = false)
    }

    override fun onPause() {
        saveCurrentEndpoint()
        statusHandler.removeCallbacks(statusPollRunnable)
        dismissViewerLogDialog()
        releaseViewerModifiers()
        cancelActiveViewerTouch("pause")
        hideViewerKeyboard("pause")
        super.onPause()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            applyImmersiveMode()
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (currentScene == UiScene.VIEWER || currentScene == UiScene.SWITCHING) {
            applyViewerRailLayout()
        }
        diagnosticsLog.log(
            "configuration_changed",
            "orientation=${if (newConfig.orientation == Configuration.ORIENTATION_LANDSCAPE) "landscape" else "portrait"}"
        )
        applyImmersiveMode()
        videoTextureView.post {
            renderStatus()
            syncVideoSurface(forceRebind = true)
        }
    }

    override fun onDestroy() {
        saveCurrentEndpoint()
        directoryExecutor.shutdownNow()
        exitDialog?.dismiss()
        exitDialog = null
        dismissViewerLogDialog()
        releaseViewerModifiers()
        hideViewerKeyboard("destroy")
        releaseVideoSurface()
        super.onDestroy()
    }

    override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
        syncVideoSurface(forceRebind = true)
    }

    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
        syncVideoSurface(forceRebind = false)
    }

    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        releaseVideoSurface()
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
    }

    private fun applyImmersiveMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.systemBars())
                controller.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
            return
        }

        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
    }

    private fun handleSystemBack() {
        when (currentScene) {
            UiScene.VIEWER, UiScene.SWITCHING -> {
                if (viewerImeCaptureView.hasFocus()) {
                    hideViewerKeyboard("system_back")
                    showViewerControls(emphasized = true)
                    return
                }
                handleViewerBack("system_back")
            }
            UiScene.CONNECT ->
                // Typing an address was a detour from signing in; back should undo the detour.
                if (manualConnectMode && DirectoryClient.savedUrl(this).isNotEmpty()) {
                    manualConnectMode = false
                    currentScene = homeScene()
                    renderStatus()
                } else {
                    showExitConfirmDialog()
                }
            UiScene.TARGETS, UiScene.LOGIN, UiScene.HOSTS -> showExitConfirmDialog()
        }
    }

    private fun handleViewerBack(reason: String) {
        diagnosticsLog.log(
            "viewer_back",
            "reason=$reason selected=${pendingSelectionLabel.ifBlank { "none" }} " +
                "currentScene=$currentScene stage=$selectionStage"
        )
        moveToTargets(reason, abortPendingSwitch = selectionStage != SelectionStage.IDLE)
        renderStatus()
    }

    private fun showExitConfirmDialog() {
        if (exitDialog?.isShowing == true) return

        exitDialog = AlertDialog.Builder(this)
            .setMessage(R.string.exit_confirm_message)
            .setPositiveButton(R.string.exit_confirm_yes) { _, _ ->
                exitDialog = null
                exitApplication()
            }
            .setNegativeButton(R.string.exit_confirm_no) { dialog, _ ->
                dialog.dismiss()
            }
            .setOnDismissListener {
                exitDialog = null
                applyImmersiveMode()
            }
            .show()
    }

    private fun exitApplication() {
        diagnosticsLog.log("exit_confirmed", "scene=$currentScene status=${NativeSessionBridge.nativeGetStatus()}")
        dismissViewerLogDialog()
        releaseViewerModifiers()
        cancelActiveViewerTouch("exit")
        hideViewerKeyboard("exit")
        desiredStreamActive = false
        pendingRuntimeConfigSync = false
        lastAppliedStreamActive = null
        if (selectionStage != SelectionStage.IDLE) {
            NativeSessionBridge.nativeAbortVideoSwitch()
        } else {
            NativeSessionBridge.nativeResetVideoStream()
        }
        NativeSessionBridge.nativeDisconnect()
        connectFlowActive = false
        clearPendingSelection()
        resetViewerObservability()
        finishAffinity()
    }

    private fun resetViewerObservability() {
        videoWidth = 0
        videoHeight = 0
        expectedContentWidth = 0
        expectedContentHeight = 0
        lastViewerOutCount = -1
        lastViewerOutChangeAtMs = 0L
        lastViewerStallLogAtMs = 0L
        lastViewerRecoveryTargetId = Long.MIN_VALUE
        lastViewerRecoveryAtMs = 0L
        lastViewerRecoveryAttempts = 0
        resetViewerTouchState()
    }

    private fun clearPendingSelection() {
        selectionStage = SelectionStage.IDLE
        pendingSelectionId = null
        pendingSelectionLabel = ""
        pendingSelectionTab = activeTargetTab
        pendingSelectionGeneration = 0L
        pendingSelectionStartedAtMs = 0L
        pendingSelectionAckLogged = false
    }

    private fun updateExpectedContentSize(width: Int, height: Int, reason: String) {
        val nextWidth = width.coerceAtLeast(0)
        val nextHeight = height.coerceAtLeast(0)
        if (expectedContentWidth == nextWidth && expectedContentHeight == nextHeight) {
            return
        }
        expectedContentWidth = nextWidth
        expectedContentHeight = nextHeight
        diagnosticsLog.log(
            "content_hint",
            "reason=$reason expected=${expectedContentWidth}x${expectedContentHeight} decoded=${videoWidth}x${videoHeight}"
        )
    }

    private fun syncExpectedContentSize(panelSnapshot: WindowPanelUiSnapshot, reason: String) {
        val nextSize =
            if (selectionStage != SelectionStage.IDLE) {
                when (pendingSelectionTab) {
                    TargetTab.WINDOWS -> {
                        val pendingId = pendingSelectionId
                        panelSnapshot.items.firstOrNull { it.id == pendingId }?.let { Pair(it.width, it.height) }
                            ?: Pair(expectedContentWidth, expectedContentHeight)
                    }
                    TargetTab.DESKTOP,
                    TargetTab.SETTINGS -> Pair(0, 0)
                }
            } else {
                Pair(panelSnapshot.selectedWidth, panelSnapshot.selectedHeight)
            }
        updateExpectedContentSize(nextSize.first, nextSize.second, reason)
    }

    private fun resolveSelectionHintSize(targetId: Long, tab: TargetTab): Pair<Int, Int> {
        if (tab != TargetTab.WINDOWS) {
            return Pair(0, 0)
        }
        val panelSnapshot = parseWindowPanelSnapshot(NativeSessionBridge.nativeGetWindowPanelJson())
        val item = panelSnapshot.items.firstOrNull { it.id == targetId }
        return if (item != null) Pair(item.width, item.height) else Pair(0, 0)
    }

    private fun requestStreamActive(active: Boolean, reason: String): Boolean {
        val ok = NativeSessionBridge.nativeRequestStreamActive(active)
        if (ok) {
            lastAppliedStreamActive = active
            diagnosticsLog.log("stream_state_request", "active=$active reason=$reason")
        } else {
            diagnosticsLog.log("stream_state_failed", "active=$active reason=$reason")
        }
        return ok
    }

    private fun updateDesktopBackendButtons() {
        val dxgiSelected = requestedDesktopBackend == DesktopCaptureBackendOption.DXGI
        settingsDesktopBackendDxgiButton.text =
            if (dxgiSelected) "[DXGI]" else "DXGI"
        settingsDesktopBackendWgcButton.text =
            if (dxgiSelected) "WGC" else "[WGC]"
    }

    private fun syncConnectedClientPreferences(isConnected: Boolean) {
        if (!isConnected) {
            lastAppliedStreamActive = null
            return
        }

        if (pendingRuntimeConfigSync) {
            val bitrateBps = requestedRuntimeBitrateKbps * 1000
            if (NativeSessionBridge.nativeRequestRuntimeConfig(bitrateBps, requestedRuntimeFps)) {
                pendingRuntimeConfigSync = false
                settingsStatusMessage =
                    "Current request: ${requestedRuntimeBitrateKbps} kbps / ${requestedRuntimeFps} fps / " +
                        "desktop ${requestedDesktopBackend.label}"
                settingsAppliedText.text = settingsStatusMessage
                diagnosticsLog.log(
                    "runtime_config_sync",
                    "bitrateBps=$bitrateBps fps=$requestedRuntimeFps"
                )
            }
        }

        if (pendingDesktopBackendSync) {
            if (NativeSessionBridge.nativeRequestDesktopCaptureBackend(requestedDesktopBackend.code)) {
                pendingDesktopBackendSync = false
                settingsStatusMessage =
                    "Current request: ${requestedRuntimeBitrateKbps} kbps / ${requestedRuntimeFps} fps / " +
                        "desktop ${requestedDesktopBackend.label}"
                settingsAppliedText.text = settingsStatusMessage
                diagnosticsLog.log(
                    "desktop_backend_sync",
                    "backend=${requestedDesktopBackend.label.lowercase()}"
                )
            }
        }

        if (lastAppliedStreamActive != desiredStreamActive) {
            requestStreamActive(desiredStreamActive, "connected_sync")
        }
    }

    private fun moveToTargets(reason: String, abortPendingSwitch: Boolean) {
        diagnosticsLog.log("targets_return", "reason=$reason scene=$currentScene")
        dismissViewerLogDialog()
        releaseViewerModifiers()
        cancelActiveViewerTouch(reason)
        hideViewerKeyboard(reason)
        viewerKeyPanel?.hide()
        desiredStreamActive = false
        requestStreamActive(false, reason)
        if (abortPendingSwitch) {
            NativeSessionBridge.nativeAbortVideoSwitch()
        } else {
            NativeSessionBridge.nativeResetVideoStream()
        }
        clearPendingSelection()
        currentScene = UiScene.TARGETS
        resetViewerObservability()
        // Leaving the viewer releases the orientation lock so the target list is usable in
        // whichever way the phone is held, and so the next selection re-evaluates cleanly.
        resetViewerOrientationState()
        NativeSessionBridge.nativeRequestWindowList()
    }

    private fun startSelectionTransition(targetId: Long, label: String, tab: TargetTab, origin: String): Boolean {
        val selectionGeneration = ++selectionGenerationCounter
        desiredStreamActive = true
        if (!requestStreamActive(true, "selection_$origin")) {
            desiredStreamActive = false
            currentScene = UiScene.TARGETS
            return false
        }
        resetViewerObservability()
        val expectedSize = resolveSelectionHintSize(targetId, tab)
        updateExpectedContentSize(expectedSize.first, expectedSize.second, "selection_request")
        // Rotate now, while the viewer surface does not exist yet. Rotating later would
        // destroy and recreate the TextureView's SurfaceTexture mid-stream.
        applyOrientationForContent(expectedSize.first, expectedSize.second, tab)
        NativeSessionBridge.nativePrepareVideoSwitch(selectionGeneration)
        val ok = when (tab) {
            TargetTab.WINDOWS -> NativeSessionBridge.nativeSelectWindow(targetId)
            TargetTab.DESKTOP -> NativeSessionBridge.nativeSelectDesktopMode()
            TargetTab.SETTINGS -> false
        }
        if (!ok) {
            desiredStreamActive = false
            requestStreamActive(false, "selection_failed")
            NativeSessionBridge.nativeAbortVideoSwitch()
            diagnosticsLog.log(
                "select_request_failed",
                "targetId=$targetId label=$label tab=$tab gen=$selectionGeneration origin=$origin"
            )
            clearPendingSelection()
            currentScene = UiScene.TARGETS
            return false
        }

        selectionStage = SelectionStage.REQUESTING
        pendingSelectionId = targetId
        pendingSelectionLabel = label
        pendingSelectionTab = tab
        pendingSelectionGeneration = selectionGeneration
        pendingSelectionStartedAtMs = SystemClock.elapsedRealtime()
        pendingSelectionAckLogged = false
        currentScene = UiScene.SWITCHING
        diagnosticsLog.log(
            "select_request",
            "targetId=$targetId label=$label tab=$tab gen=$selectionGeneration origin=$origin " +
                "expected=${expectedContentWidth}x${expectedContentHeight}"
        )
        return true
    }

    private fun handleViewerTouch(view: View, event: MotionEvent): Boolean {
        if (currentScene != UiScene.VIEWER) return false
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            showViewerControls()
        }

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_POINTER_DOWN -> {
                val pointerIndex = event.actionIndex
                val mappedOrNull = mapTouchToVideoCoords(
                    event.getX(pointerIndex),
                    event.getY(pointerIndex),
                    clampToContent = false
                )
                if (mappedOrNull == null) {
                    handleMarginPress(view, event.getPointerId(pointerIndex),
                                      event.getY(pointerIndex))
                    return true
                }
                if (activeTouchPointerId != MotionEvent.INVALID_POINTER_ID) return true
                val mapped = mappedOrNull
                // Never take focus away from the IME capture view: tapping the remote screen
                // to place the caret would otherwise tear down the InputConnection and leave
                // the soft keyboard visible but dead.
                if (!viewerImeCaptureView.hasFocus()) view.requestFocus()
                if (isTabletModeActive()) {
                    activeTouchPointerId = event.getPointerId(pointerIndex)
                    activeViewerTouchMode = ViewerTouchMode.SCROLL
                    activeTouchButtons = 0
                    lastTouchVideoX = mapped.first
                    lastTouchVideoY = mapped.second
                    scrollLastTouchY = event.getY(pointerIndex)
                    scrollWheelCarryPx = 0f
                    val queued = NativeSessionBridge.nativeQueueInputEvent(
                        INPUT_KIND_MOUSE_MOVE,
                        mapped.first,
                        mapped.second,
                        0,
                        0,
                        0
                    )
                    if (!queued) {
                        resetViewerTouchState()
                    }
                    return queued
                }
                activeTouchPointerId = event.getPointerId(pointerIndex)
                activeViewerTouchMode = ViewerTouchMode.DIRECT
                activeTouchIsSecondary = isRightClickModeActive()
                activeTouchButtons =
                    if (activeTouchIsSecondary) INPUT_BUTTON_SECONDARY else INPUT_BUTTON_PRIMARY
                lastTouchVideoX = mapped.first
                lastTouchVideoY = mapped.second
                val queued = NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_DOWN,
                    mapped.first,
                    mapped.second,
                    0,
                    if (activeTouchIsSecondary) INPUT_VK_RBUTTON else INPUT_VK_LBUTTON,
                    activeTouchButtons
                )
                if (!queued) {
                    resetViewerTouchState()
                }
                return queued
            }

            MotionEvent.ACTION_MOVE -> {
                // A second finger on the picture while tablet mode is held means zoom, which
                // the remote side has no notion of; Ctrl+wheel is what applications understand.
                if (isTabletModeActive() && handlePinchZoom(event)) {
                    return true
                }
                val pointerId = activeTouchPointerId
                if (pointerId == MotionEvent.INVALID_POINTER_ID) return false
                val pointerIndex = event.findPointerIndex(pointerId)
                if (pointerIndex < 0) return true
                if (activeViewerTouchMode == ViewerTouchMode.SCROLL) {
                    // Natural scrolling: the content follows the finger, as it does everywhere
                    // else on a phone. Dragging up shows what is further down, so it becomes
                    // wheel-down on the remote side.
                    val touchY = event.getY(pointerIndex)
                    scrollWheelCarryPx += touchY - scrollLastTouchY
                    scrollLastTouchY = touchY
                    val direction =
                        when {
                            scrollWheelCarryPx > 0f -> 1
                            scrollWheelCarryPx < 0f -> -1
                            else -> 0
                        }
                    val stepCount =
                        if (direction == 0) {
                            0
                        } else {
                            (abs(scrollWheelCarryPx) / scrollGestureStepPx).toInt()
                        }
                    if (stepCount > 0) {
                        val queued = NativeSessionBridge.nativeQueueInputEvent(
                            INPUT_KIND_MOUSE_WHEEL,
                            lastTouchVideoX,
                            lastTouchVideoY,
                            direction * stepCount * INPUT_WHEEL_DELTA_STEP,
                            0,
                            0
                        )
                        if (!queued) {
                            resetViewerTouchState()
                            return false
                        }
                        scrollWheelCarryPx -= direction * stepCount * scrollGestureStepPx
                    }
                    return true
                }
                val mapped = mapTouchToVideoCoords(
                    event.getX(pointerIndex),
                    event.getY(pointerIndex),
                    clampToContent = true
                ) ?: return true
                if (mapped.first == lastTouchVideoX && mapped.second == lastTouchVideoY) {
                    return true
                }
                lastTouchVideoX = mapped.first
                lastTouchVideoY = mapped.second
                NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_MOVE,
                    mapped.first,
                    mapped.second,
                    0,
                    0,
                    activeTouchButtons
                )
                return true
            }

            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_POINTER_UP -> {
                val pointerIndex = event.actionIndex
                if (event.getPointerId(pointerIndex) == rightClickModifierPointerId) {
                    rightClickModifierPointerId = MotionEvent.INVALID_POINTER_ID
                    renderViewerModeBanner()
                    diagnosticsLog.log("right_click_released", "reason=pointer_up")
                    return true
                }
                if (event.getPointerId(pointerIndex) == tabletModePointerId) {
                    tabletModePointerId = MotionEvent.INVALID_POINTER_ID
                    pinchStartSpan = 0f
                    renderViewerModeBanner()
                    diagnosticsLog.log("tablet_mode_released", "reason=pointer_up")
                    return true
                }
                if (event.getPointerId(pointerIndex) != activeTouchPointerId) return true
                val mapped = mapTouchToVideoCoords(
                    event.getX(pointerIndex),
                    event.getY(pointerIndex),
                    clampToContent = true
                ) ?: Pair(lastTouchVideoX, lastTouchVideoY)
                if (activeViewerTouchMode == ViewerTouchMode.SCROLL) {
                    resetViewerTouchState()
                    view.performClick()
                    return true
                }
                lastTouchVideoX = mapped.first
                lastTouchVideoY = mapped.second
                val queued = NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_UP,
                    mapped.first,
                    mapped.second,
                    0,
                    if (activeTouchIsSecondary) INPUT_VK_RBUTTON else INPUT_VK_LBUTTON,
                    0
                )
                resetViewerTouchState()
                view.performClick()
                return queued
            }

            MotionEvent.ACTION_CANCEL -> {
                rightClickModifierPointerId = MotionEvent.INVALID_POINTER_ID
                tabletModePointerId = MotionEvent.INVALID_POINTER_ID
                pinchStartSpan = 0f
                renderViewerModeBanner()
                cancelActiveViewerTouch("touch_cancel")
                return true
            }
        }

        return false
    }

    /**
     * Wires the on-screen mouse. It reports intent in relative terms; the absolute position is
     * kept here because that is what the wire protocol carries.
     */
    private fun initVirtualMouse() {
        val overlay = findViewById<android.widget.FrameLayout>(R.id.viewerVirtualMouse)
        virtualMouse = ViewerVirtualMouse(overlay, object : ViewerVirtualMouse.Listener {
            override fun onMoveBy(dx: Int, dy: Int) {
                val contentRect = resolveViewerContentRect() ?: return
                if (virtualPointerX < 0) {
                    virtualPointerX = contentRect.contentWidth / 2
                    virtualPointerY = contentRect.contentHeight / 2
                }
                // The widget speaks in view pixels; the pointer lives in remote pixels. A 1920
                // wide screen shown in a 700 wide box means one view pixel is nearly three
                // remote ones, and applying the delta raw made the pointer crawl at a fifth of
                // the intended speed.
                val viewToRemoteX =
                    if (contentRect.width > 0f) contentRect.contentWidth / contentRect.width else 1f
                val viewToRemoteY =
                    if (contentRect.height > 0f) contentRect.contentHeight / contentRect.height else 1f
                virtualPointerX = (virtualPointerX + (dx * viewToRemoteX).roundToInt())
                    .coerceIn(0, contentRect.contentWidth - 1)
                virtualPointerY = (virtualPointerY + (dy * viewToRemoteY).roundToInt())
                    .coerceIn(0, contentRect.contentHeight - 1)
                // Kept in step with direct touches, so releasing a button here lands where the
                // last real touch was if the two are mixed.
                lastTouchVideoX = virtualPointerX
                lastTouchVideoY = virtualPointerY
                NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_MOVE, virtualPointerX, virtualPointerY, 0, 0, activeTouchButtons,
                )
                syncVirtualMouseMarker()
            }

            override fun onRequestInitialPosition() {
                val contentRect = resolveViewerContentRect() ?: return
                if (virtualPointerX < 0) {
                    virtualPointerX = contentRect.contentWidth / 2
                    virtualPointerY = contentRect.contentHeight / 2
                    NativeSessionBridge.nativeQueueInputEvent(
                        INPUT_KIND_MOUSE_MOVE, virtualPointerX, virtualPointerY, 0, 0, 0,
                    )
                }
                syncVirtualMouseMarker()
            }

            override fun onButtonDown(button: ViewerVirtualMouse.Button) {
                val vk = virtualButtonVk(button)
                activeTouchButtons = virtualButtonMask(button)
                NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_DOWN, virtualPointerX.coerceAtLeast(0),
                    virtualPointerY.coerceAtLeast(0), 0, vk, activeTouchButtons,
                )
            }

            override fun onButtonUp(button: ViewerVirtualMouse.Button) {
                val vk = virtualButtonVk(button)
                activeTouchButtons = 0
                NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_UP, virtualPointerX.coerceAtLeast(0),
                    virtualPointerY.coerceAtLeast(0), 0, vk, 0,
                )
            }

            override fun onWheel(notches: Int) {
                NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_WHEEL, virtualPointerX.coerceAtLeast(0),
                    virtualPointerY.coerceAtLeast(0), notches * INPUT_WHEEL_DELTA_STEP, 0, 0,
                )
            }
        })
    }

    /** Places the ring on the pointer, converting remote pixels back into view pixels. */
    private fun syncVirtualMouseMarker() {
        val mouse = virtualMouse ?: return
        val contentRect = resolveViewerContentRect() ?: return
        if (virtualPointerX < 0 || contentRect.contentWidth <= 1 || contentRect.contentHeight <= 1) {
            return
        }
        val viewX = contentRect.left +
            (virtualPointerX.toFloat() / (contentRect.contentWidth - 1)) * contentRect.width
        val viewY = contentRect.top +
            (virtualPointerY.toFloat() / (contentRect.contentHeight - 1)) * contentRect.height
        mouse.moveTo(viewX, viewY)
    }

    private fun virtualButtonVk(button: ViewerVirtualMouse.Button): Int = when (button) {
        ViewerVirtualMouse.Button.LEFT -> INPUT_VK_LBUTTON
        ViewerVirtualMouse.Button.RIGHT -> INPUT_VK_RBUTTON
        ViewerVirtualMouse.Button.MIDDLE -> INPUT_VK_MBUTTON
    }

    private fun virtualButtonMask(button: ViewerVirtualMouse.Button): Int = when (button) {
        ViewerVirtualMouse.Button.LEFT -> INPUT_BUTTON_PRIMARY
        ViewerVirtualMouse.Button.RIGHT -> INPUT_BUTTON_SECONDARY
        ViewerVirtualMouse.Button.MIDDLE -> INPUT_BUTTON_MIDDLE
    }

    /**
     * The margin is split into thirds. Top and middle are held modifiers; the bottom is a tap,
     * because the on-screen mouse has to stay up while both hands are used on it.
     */
    private fun handleMarginPress(view: View, pointerId: Int, y: Float) {
        val height = view.height.toFloat()
        val zone = when {
            height <= 0f -> MarginZone.TOP
            y < height / 3f -> MarginZone.TOP
            y < height * 2f / 3f -> MarginZone.MIDDLE
            else -> MarginZone.BOTTOM
        }
        when (zone) {
            MarginZone.TOP -> {
                if (rightClickModifierPointerId == MotionEvent.INVALID_POINTER_ID) {
                    rightClickModifierPointerId = pointerId
                    showViewerControls(emphasized = true)
                    renderViewerModeBanner()
                    diagnosticsLog.log("right_click_armed", "pointer=$pointerId")
                }
            }
            MarginZone.MIDDLE -> {
                if (tabletModePointerId == MotionEvent.INVALID_POINTER_ID) {
                    tabletModePointerId = pointerId
                    pinchStartSpan = 0f
                    pinchZoomCarry = 0f
                    showViewerControls(emphasized = true)
                    renderViewerModeBanner()
                    diagnosticsLog.log("tablet_mode_armed", "pointer=$pointerId")
                }
            }
            MarginZone.BOTTOM -> {
                toggleVirtualMouse()
            }
        }
    }

    /** A modifier counts whether it came from the strip or from a letterbox margin. */
    private fun isRightClickModeActive(): Boolean =
        zoneRightClickHeld || rightClickModifierPointerId != MotionEvent.INVALID_POINTER_ID

    private fun isTabletModeActive(): Boolean =
        zoneTabletHeld || zoneTabletLocked || tabletModePointerId != MotionEvent.INVALID_POINTER_ID

    /**
     * The left strip. Because it is its own view, the finger holding it never enters the
     * picture's gesture, which is what let a held margin finger masquerade as a pinch.
     */
    private fun initZoneBar() {
        viewerZoneRightClick = findViewById(R.id.viewerZoneRightClick)
        viewerZoneTablet = findViewById(R.id.viewerZoneTablet)
        viewerTabletLockButton = findViewById(R.id.viewerTabletLockButton)
        viewerTabletLockButton.setOnClickListener {
            zoneTabletLocked = !zoneTabletLocked
            diagnosticsLog.log("tablet_mode_lock", "locked=$zoneTabletLocked")
            if (zoneTabletLocked) showViewerControls(emphasized = true)
            renderViewerModeBanner()
        }
        val bar = findViewById<LinearLayout>(R.id.viewerZoneBar)
        bar.setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    // The strip lies along the left edge in landscape and along the top in
                    // portrait, so the thirds are measured along whichever way it runs.
                    val extent =
                        if (bar.orientation == LinearLayout.VERTICAL) view.height.toFloat()
                        else view.width.toFloat()
                    val position =
                        if (bar.orientation == LinearLayout.VERTICAL) event.y else event.x
                    when {
                        extent <= 0f || position < extent / 3f -> {
                            zoneRightClickHeld = true
                            diagnosticsLog.log("right_click_armed", "source=zone_bar")
                        }
                        position < extent * 2f / 3f -> {
                            zoneTabletHeld = true
                            pinchStartSpan = 0f
                            pinchZoomCarry = 0f
                            diagnosticsLog.log("tablet_mode_armed", "source=zone_bar")
                        }
                        else -> {
                            // A tap, not a hold: both hands are needed on the mouse itself.
                            toggleVirtualMouse()
                        }
                    }
                    showViewerControls(emphasized = true)
                    renderViewerModeBanner()
                    true
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_CANCEL -> {
                    releaseZoneBarModifiers()
                    view.performClick()
                    true
                }

                else -> true
            }
        }
    }

    private fun releaseZoneBarModifiers() {
        if (!zoneRightClickHeld && !zoneTabletHeld) return
        zoneRightClickHeld = false
        zoneTabletHeld = false
        pinchStartSpan = 0f
        if (::viewerModeBanner.isInitialized) renderViewerModeBanner()
    }

    /** Shows which modifier is currently held, so the mode is never a guess. */
    private fun renderViewerModeBanner() {
        val text = when {
            zoneTabletLocked -> getString(R.string.viewer_mode_tablet_locked)
            isTabletModeActive() -> getString(R.string.viewer_mode_tablet)
            isRightClickModeActive() -> getString(R.string.viewer_mode_right_click)
            else -> ""
        }
        viewerModeBanner.text = text
        viewerModeBanner.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
        if (::viewerTabletLockButton.isInitialized) {
            viewerTabletLockButton.visibility =
                if (isTabletModeActive()) View.VISIBLE else View.GONE
            viewerTabletLockButton.text = getString(
                if (zoneTabletLocked) R.string.tablet_lock_closed else R.string.tablet_lock_open,
            )
        }
        if (::viewerZoneRightClick.isInitialized) {
            viewerZoneRightClick.alpha = if (isRightClickModeActive()) 1.0f else 0.55f
            viewerZoneRightClick.setBackgroundColor(
                if (isRightClickModeActive()) 0x3345E08C else 0x00000000,
            )
            viewerZoneTablet.alpha = if (isTabletModeActive()) 1.0f else 0.55f
            viewerZoneTablet.setBackgroundColor(
                if (isTabletModeActive()) 0x3345E08C else 0x00000000,
            )
        }
    }

    /**
     * Two fingers on the picture: turn the change in spread into Ctrl+wheel, which is how
     * browsers, editors and image viewers all spell zoom.
     */
    private fun handlePinchZoom(event: MotionEvent): Boolean {
        // Only fingers that are actually on the picture can pinch. When tablet mode is held
        // via a letterbox margin, that margin finger shares this event stream, and counting it
        // turned every one-finger drag into a phantom zoom that ate the scroll gesture.
        val pinchIndices = (0 until event.pointerCount).filter {
            val id = event.getPointerId(it)
            id != tabletModePointerId && id != rightClickModifierPointerId
        }
        if (pinchIndices.size < 2) {
            pinchStartSpan = 0f
            return false
        }
        val a = pinchIndices[0]
        val b = pinchIndices[1]
        val span = kotlin.math.hypot(
            (event.getX(b) - event.getX(a)).toDouble(),
            (event.getY(b) - event.getY(a)).toDouble(),
        ).toFloat()
        if (pinchStartSpan <= 0f) {
            pinchStartSpan = span
            pinchZoomCarry = 0f
            return true
        }
        pinchZoomCarry += span - pinchStartSpan
        pinchStartSpan = span
        val step = scrollGestureStepPx * 2f
        val notches = (abs(pinchZoomCarry) / step).toInt()
        if (notches > 0) {
            val direction = if (pinchZoomCarry > 0f) 1 else -1
            pinchZoomCarry -= direction * notches * step
            queueViewerSpecialKey(INPUT_VK_CONTROL, KeyEvent.ACTION_DOWN)
            NativeSessionBridge.nativeQueueInputEvent(
                INPUT_KIND_MOUSE_WHEEL,
                lastTouchVideoX,
                lastTouchVideoY,
                direction * notches * INPUT_WHEEL_DELTA_STEP,
                0,
                0,
            )
            queueViewerSpecialKey(INPUT_VK_CONTROL, KeyEvent.ACTION_UP)
        }
        return true
    }

    private fun toggleVirtualMouse() {
        val mouse = virtualMouse ?: return
        if (mouse.isOpen) {
            mouse.hide()
            diagnosticsLog.log("virtual_mouse", "state=hidden")
            return
        }
        mouse.show()
        showViewerControls(emphasized = true)
        diagnosticsLog.log("virtual_mouse", "state=shown at=$virtualPointerX,$virtualPointerY")
    }

    // ---------------------------------------------------------------- macro

    private fun showMacroDialog() {
        if (macroDialog?.isShowing == true) return
        val view = layoutInflater.inflate(R.layout.viewer_macro_dialog, null)
        macroStatusText = view.findViewById(R.id.macroStatusText)
        macroStepListContainer = view.findViewById(R.id.macroStepListContainer)
        macroRecordButton = view.findViewById(R.id.macroRecordButton)
        macroPlayButton = view.findViewById(R.id.macroPlayButton)
        macroPauseButton = view.findViewById(R.id.macroPauseButton)
        macroListRenderedSignature = null

        macroRecordButton?.setOnClickListener {
            if (NativeSessionBridge.nativeMacroState() == MACRO_STATE_RECORDING) {
                stopMacroRecording()
            } else {
                startMacroRecording()
            }
        }
        macroPlayButton?.setOnClickListener {
            if (NativeSessionBridge.nativeMacroState() == MACRO_STATE_PLAYING) {
                NativeSessionBridge.nativeMacroStopPlayback()
                diagnosticsLog.log("macro", "playback_stopped")
            } else {
                startMacroPlayback(view)
            }
            renderMacroUi()
        }
        macroPauseButton?.setOnClickListener { toggleMacroPause() }
        view.findViewById<Button>(R.id.macroClearButton).setOnClickListener {
            NativeSessionBridge.nativeMacroStopPlayback()
            NativeSessionBridge.nativeMacroClear()
            renderMacroUi()
        }
        view.findViewById<Button>(R.id.macroSaveButton).setOnClickListener { showMacroSaveDialog() }
        view.findViewById<Button>(R.id.macroLoadButton).setOnClickListener { showMacroLoadDialog() }
        view.findViewById<Button>(R.id.macroCloseButton).setOnClickListener {
            macroDialog?.dismiss()
        }

        macroDialog = AlertDialog.Builder(this)
            .setView(view)
            .setOnDismissListener {
                // Recording and playback deliberately survive the window closing: the actions
                // being recorded happen on the picture this window is covering.
                macroDialog = null
                macroStatusText = null
                macroStepListContainer = null
                macroRecordButton = null
                macroPlayButton = null
                macroPauseButton = null
                applyImmersiveMode()
            }
            .create()
        macroDialog?.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        macroDialog?.show()
        renderMacroUi()
    }

    private fun startMacroRecording() {
        NativeSessionBridge.nativeMacroStopPlayback()
        NativeSessionBridge.nativeMacroStartRecording()
        diagnosticsLog.log("macro", "recording_started")
        // Out of the way, so the actions can be performed on the picture.
        macroDialog?.dismiss()
        scheduleMacroPump()
        renderMacroUi()
    }

    private fun stopMacroRecording() {
        NativeSessionBridge.nativeMacroStopRecording()
        diagnosticsLog.log("macro", "recorded=${NativeSessionBridge.nativeMacroStepCount()}")
        renderMacroUi()
        showMacroDialog()
    }

    private fun startMacroPlayback(view: View) {
        fun field(id: Int, fallback: Int): Int =
            view.findViewById<EditText>(id).text?.toString()?.trim()?.toIntOrNull() ?: fallback

        val started = NativeSessionBridge.nativeMacroStartPlayback(
            field(R.id.macroTimingJitterInput, 0),
            field(R.id.macroPositionJitterInput, 0),
            field(R.id.macroRepeatInput, 1),
            field(R.id.macroGapMinInput, 0),
            field(R.id.macroGapMaxInput, 0),
        )
        diagnosticsLog.log("macro", "playback_started=$started")
        if (started) scheduleMacroPump()
    }

    private fun scheduleMacroPump() {
        if (macroPumpScheduled) return
        macroPumpScheduled = true
        statusHandler.post(macroPumpRunnable)
    }

    private fun toggleMacroPause() {
        val paused = NativeSessionBridge.nativeMacroIsPaused()
        NativeSessionBridge.nativeMacroSetPaused(!paused)
        diagnosticsLog.log("macro", if (paused) "resumed" else "paused")
        renderMacroUi()
    }

    private fun renderMacroUi() {
        val state = NativeSessionBridge.nativeMacroState()
        val count = NativeSessionBridge.nativeMacroStepCount()
        val paused = NativeSessionBridge.nativeMacroIsPaused()
        val pauseLabel = getString(if (paused) R.string.macro_resume else R.string.macro_pause)

        macroRecordBar.visibility =
            if (state == MACRO_STATE_RECORDING) View.VISIBLE else View.GONE
        if (state == MACRO_STATE_RECORDING) {
            macroRecordBarText.text =
                if (paused) getString(R.string.macro_paused_recording, count)
                else getString(R.string.macro_recording, count)
            macroRecordBarPause.text = pauseLabel
        }

        macroRecordButton?.text =
            if (state == MACRO_STATE_RECORDING) getString(R.string.macro_stop)
            else getString(R.string.macro_record)
        macroPlayButton?.text =
            if (state == MACRO_STATE_PLAYING) getString(R.string.macro_stop)
            else getString(R.string.macro_play)
        macroPlayButton?.isEnabled = count > 0
        macroPauseButton?.text = pauseLabel
        macroPauseButton?.isEnabled = state != MACRO_STATE_IDLE

        macroStatusText?.text = when {
            state == MACRO_STATE_RECORDING && paused ->
                getString(R.string.macro_paused_recording, count)
            state == MACRO_STATE_RECORDING -> getString(R.string.macro_recording, count)
            state == MACRO_STATE_PLAYING && paused -> getString(R.string.macro_paused_playing)
            state == MACRO_STATE_PLAYING -> getString(
                R.string.macro_playing_summary,
                NativeSessionBridge.nativeMacroCompletedRepeats(),
            )
            else -> getString(R.string.macro_idle_summary, count)
        }
        renderMacroSteps(state, count)
    }

    /** Rebuilds the row list only when its contents actually changed. */
    private fun renderMacroSteps(state: Int, count: Int) {
        val container = macroStepListContainer ?: return
        val signature = "$state/$count"
        if (signature == macroListRenderedSignature) return
        macroListRenderedSignature = signature

        container.removeAllViews()
        fun addLine(text: String, dim: Boolean = false): TextView {
            val row = TextView(this)
            row.text = text
            row.textSize = 11f
            row.typeface = android.graphics.Typeface.MONOSPACE
            row.setTextColor(if (dim) 0xFF9AA3B2.toInt() else 0xFFD8DEE9.toInt())
            row.setPadding(dp(4f), dp(5f), dp(4f), dp(5f))
            container.addView(row)
            return row
        }

        if (count == 0) {
            addLine(getString(R.string.macro_empty), dim = true)
            return
        }
        addLine(getString(R.string.macro_edit_hint), dim = true)
        val lines = NativeSessionBridge.nativeMacroStepLines().split('\n')
        val editable = state == MACRO_STATE_IDLE
        // A runaway recording can hold thousands of steps; a dialog list stops being a review
        // tool long before that, and building the views would stall the UI thread.
        val shown = lines.take(MACRO_LIST_MAX_ROWS)
        shown.forEachIndexed { index, line ->
            val row = addLine(line)
            if (editable) {
                row.setBackgroundResource(android.R.drawable.list_selector_background)
                row.setOnClickListener { showMacroStepEditDialog(index) }
            }
        }
        if (lines.size > shown.size) {
            addLine(getString(R.string.macro_list_truncated, lines.size - shown.size), dim = true)
        }
    }

    private fun dp(v: Float): Int = (v * resources.displayMetrics.density).toInt()

    /** One action: adjust where it lands and how long it waits, or drop it entirely. */
    private fun showMacroStepEditDialog(index: Int) {
        val fields = NativeSessionBridge.nativeMacroStepFields(index).split(' ')
        if (fields.size < 7) return
        val container = LinearLayout(this)
        container.orientation = LinearLayout.VERTICAL
        container.setPadding(dp(20f), dp(8f), dp(20f), dp(0f))

        fun labelled(labelRes: Int, value: String): EditText {
            val label = TextView(this)
            label.text = getString(labelRes)
            label.textSize = 12f
            container.addView(label)
            val edit = EditText(this)
            edit.inputType = android.text.InputType.TYPE_CLASS_NUMBER
            edit.setText(value)
            container.addView(edit)
            return edit
        }

        val xEdit = labelled(R.string.macro_step_x, fields[1])
        val yEdit = labelled(R.string.macro_step_y, fields[2])
        val delayEdit = labelled(R.string.macro_step_delay, fields[6])

        AlertDialog.Builder(this)
            .setTitle(getString(R.string.macro_step_edit_title, index + 1))
            .setView(container)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                NativeSessionBridge.nativeMacroUpdateStep(
                    index,
                    xEdit.text.toString().toIntOrNull() ?: 0,
                    yEdit.text.toString().toIntOrNull() ?: 0,
                    delayEdit.text.toString().toIntOrNull() ?: 0,
                )
                macroListRenderedSignature = null
                renderMacroUi()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setNeutralButton(R.string.macro_step_delete) { _, _ ->
                NativeSessionBridge.nativeMacroRemoveStep(index)
                macroListRenderedSignature = null
                renderMacroUi()
            }
            .show()
    }

    // ------------------------------------------------------------ macro save/load

    private fun macroDir(): java.io.File =
        java.io.File(filesDir, "macros").apply { mkdirs() }

    private fun showMacroSaveDialog() {
        if (NativeSessionBridge.nativeMacroStepCount() == 0) return
        val nameEdit = EditText(this)
        nameEdit.hint = getString(R.string.macro_save_hint)
        nameEdit.maxLines = 1
        val wrapper = LinearLayout(this)
        wrapper.setPadding(dp(20f), dp(8f), dp(20f), dp(0f))
        wrapper.addView(
            nameEdit,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
        )
        AlertDialog.Builder(this)
            .setTitle(R.string.macro_save_title)
            .setView(wrapper)
            .setPositiveButton(R.string.macro_save) { _, _ ->
                val raw = nameEdit.text.toString().trim()
                // The name becomes a file name; strip anything the filesystem might interpret.
                val name = raw.replace(Regex("[\\\\/:*?\"<>|]"), "_").take(40)
                if (name.isEmpty()) return@setPositiveButton
                java.io.File(macroDir(), "$name.gnmacro")
                    .writeText(NativeSessionBridge.nativeMacroSerialize())
                diagnosticsLog.log("macro", "saved name=$name")
                Toast.makeText(this, getString(R.string.macro_saved_toast, name), Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showMacroLoadDialog() {
        val files = macroDir().listFiles { f -> f.isFile && f.name.endsWith(".gnmacro") }
            ?.sortedByDescending { it.lastModified() }
            .orEmpty()
        if (files.isEmpty()) {
            Toast.makeText(this, R.string.macro_load_empty, Toast.LENGTH_SHORT).show()
            return
        }
        val names = files.map { it.name.removeSuffix(".gnmacro") }
        val list = ListView(this)
        list.adapter = android.widget.ArrayAdapter(
            this, android.R.layout.simple_list_item_1, names,
        )
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.macro_load_title)
            .setView(list)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
        list.setOnItemClickListener { _, _, position, _ ->
            val ok = runCatching { files[position].readText() }
                .map { NativeSessionBridge.nativeMacroLoadSerialized(it) }
                .getOrDefault(false)
            if (ok) {
                diagnosticsLog.log("macro", "loaded name=${names[position]}")
                Toast.makeText(
                    this, getString(R.string.macro_loaded_toast, names[position]), Toast.LENGTH_SHORT,
                ).show()
                macroListRenderedSignature = null
                renderMacroUi()
            } else {
                Toast.makeText(this, R.string.macro_load_failed, Toast.LENGTH_SHORT).show()
            }
            dialog.dismiss()
        }
        list.setOnItemLongClickListener { _, _, position, _ ->
            AlertDialog.Builder(this)
                .setMessage(getString(R.string.macro_delete_confirm, names[position]))
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    files[position].delete()
                    dialog.dismiss()
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
            true
        }
        dialog.show()
    }

    private fun cancelActiveViewerTouch(reason: String) {
        if (activeViewerTouchMode == ViewerTouchMode.SCROLL) {
            resetViewerTouchState()
            return
        }
        if (activeTouchPointerId == MotionEvent.INVALID_POINTER_ID || activeTouchButtons == 0) {
            resetViewerTouchState()
            return
        }

        val queued = NativeSessionBridge.nativeQueueInputEvent(
            INPUT_KIND_MOUSE_UP,
            lastTouchVideoX,
            lastTouchVideoY,
            0,
            if (activeTouchIsSecondary) INPUT_VK_RBUTTON else INPUT_VK_LBUTTON,
            0
        )
        if (!queued) {
            diagnosticsLog.log(
                "touch_release_skipped",
                "reason=$reason x=$lastTouchVideoX y=$lastTouchVideoY"
            )
        }
        resetViewerTouchState()
    }

    /** The margin-hold right click has no visible affordance, so say it once per session. */
    private fun showRightClickHintOnce() {
        if (rightClickHintShown) return
        rightClickHintShown = true
        Toast.makeText(this, R.string.viewer_right_click_hint, Toast.LENGTH_LONG).show()
    }

    private fun resetViewerTouchState() {
        activeTouchIsSecondary = false
        activeTouchPointerId = MotionEvent.INVALID_POINTER_ID
        activeViewerTouchMode = ViewerTouchMode.DIRECT
        activeTouchButtons = 0
        lastTouchVideoX = 0
        lastTouchVideoY = 0
        scrollLastTouchY = 0f
        scrollWheelCarryPx = 0f
    }

    private fun toggleViewerKeyboard() {
        showViewerControls(emphasized = true)
        if (viewerImeCaptureView.hasFocus()) {
            hideViewerKeyboard("toolbar_toggle")
            return
        }
        diagnosticsLog.log("viewer_keyboard_tap", "scene=$currentScene")
        viewerImeCaptureView.requestFocus()
        viewerImeCaptureView.post {
            val imm = getSystemService(InputMethodManager::class.java)
            imm?.showSoftInput(viewerImeCaptureView, InputMethodManager.SHOW_IMPLICIT)
        }
    }

    private fun hideViewerKeyboard(reason: String) {
        if (!::viewerImeCaptureView.isInitialized) return
        val hadFocus = viewerImeCaptureView.hasFocus()
        val imm = getSystemService(InputMethodManager::class.java)
        imm?.hideSoftInputFromWindow(viewerImeCaptureView.windowToken, 0)
        viewerImeCaptureView.clearFocus()
        if (hadFocus) {
            diagnosticsLog.log("viewer_keyboard_hide", "reason=$reason")
        }
    }

    private fun showViewerControls(emphasized: Boolean = false) {
        if (!::viewerControlsBar.isInitialized) return
        val targetAlpha = 1.0f
        viewerControlsBar.animate().alpha(targetAlpha).setDuration(120L).start()
        statusHandler.removeCallbacks(viewerControlsFadeRunnable)
        val delayMs = if (emphasized) 3200L else 2400L
        statusHandler.postDelayed(viewerControlsFadeRunnable, delayMs)
    }

    /** Drops every held modifier, the tablet lock, and any scroll gesture that depended on one. */
    private fun releaseViewerModifiers() {
        zoneTabletLocked = false
        releaseZoneBarModifiers()
        if (::viewerModeBanner.isInitialized) renderViewerModeBanner()
        if (activeViewerTouchMode == ViewerTouchMode.SCROLL) {
            resetViewerTouchState()
        }
    }

    private fun toggleViewerLogDialog() {
        showViewerControls(emphasized = true)
        val existing = viewerLogDialog
        if (existing?.isShowing == true) {
            existing.dismiss()
            return
        }

        val contentView = layoutInflater.inflate(R.layout.viewer_log_dialog, null)
        viewerLogStatusText = contentView.findViewById(R.id.viewerLogStatusText)
        viewerLogTextView = contentView.findViewById(R.id.viewerLogText)
        contentView.findViewById<Button>(R.id.viewerLogRefreshButton).setOnClickListener {
            refreshViewerLogBody()
            renderStatus()
        }
        contentView.findViewById<Button>(R.id.viewerLogCloseButton).setOnClickListener {
            dismissViewerLogDialog()
        }

        val dialog = AlertDialog.Builder(this).setView(contentView).create()
        dialog.setOnDismissListener {
            viewerLogDialog = null
            viewerLogStatusText = null
            viewerLogTextView = null
            applyImmersiveMode()
        }
        dialog.show()
        dialog.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        dialog.window?.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        viewerLogDialog = dialog
        refreshViewerLogBody()
        updateViewerLogHeader(
            NativeSessionBridge.nativeGetStatus(),
            parseWindowPanelSnapshot(NativeSessionBridge.nativeGetWindowPanelJson()),
            NativeSessionBridge.nativeGetVideoDebugStatus(),
            NativeSessionBridge.nativeGetLastError()
        )
    }

    private fun dismissViewerLogDialog() {
        viewerLogDialog?.dismiss()
    }

    private fun refreshViewerLogBody() {
        // Up to 512 KB off the main thread; the callback lands back on it.
        diagnosticsLog.readAllTextAsync { currentLog ->
            viewerLogTextView?.text =
                if (currentLog.isBlank()) {
                    getString(R.string.viewer_log_empty)
                } else {
                    currentLog
                }
        }
    }

    private fun updateViewerLogHeader(
        statusValue: String,
        panelSnapshot: WindowPanelUiSnapshot,
        videoDebugValue: String,
        errorValue: String,
    ) {
        val headerView = viewerLogStatusText ?: return
        headerView.text =
            buildString {
                append("selected: ")
                append(panelSnapshot.selectedTitle)
                append('\n')
                append("status: ")
                append(statusValue)
                append('\n')
                append("panel: ")
                append(panelSnapshot.status)
                append('\n')
                append("video: ")
                append(if (videoDebugValue.isBlank()) "n/a" else videoDebugValue)
                if (errorValue.isNotBlank()) {
                    append('\n')
                    append("error: ")
                    append(errorValue)
                }
                append('\n')
                append("file: ")
                append(diagnosticsLog.filePath())
            }
    }

    private fun queueViewerCommittedText(text: CharSequence) {
        if (currentScene != UiScene.VIEWER || text.isEmpty()) return
        val normalized =
            buildString(text.length) {
                text.forEach { ch ->
                    when (ch) {
                        '\u0000' -> {}
                        '\n' -> append('\r')
                        '\u00A0' -> append(' ')
                        else -> append(ch)
                    }
                }
            }
        if (normalized.isEmpty()) return
        val ok = NativeSessionBridge.nativeQueueInputText(normalized)
        if (!ok) {
            diagnosticsLog.log("viewer_text_queue_failed", "len=${normalized.length}")
        }
    }

    private fun queueViewerSpecialKey(windowsVk: Int, action: Int): Boolean {
        if (currentScene != UiScene.VIEWER) return false
        val inputKind =
            when (action) {
                KeyEvent.ACTION_DOWN -> INPUT_KIND_KEY_DOWN
                KeyEvent.ACTION_UP -> INPUT_KIND_KEY_UP
                else -> return false
            }
        val x = resolveKeyboardVideoCoord(lastTouchVideoX, videoWidth)
        val y = resolveKeyboardVideoCoord(lastTouchVideoY, videoHeight)
        return NativeSessionBridge.nativeQueueInputEvent(
            inputKind,
            x,
            y,
            0,
            windowsVk,
            activeTouchButtons
        )
    }

    private fun resolveKeyboardVideoCoord(lastCoord: Int, extent: Int): Int {
        if (extent <= 0) return 0
        return lastCoord.coerceIn(0, extent - 1)
    }

    private fun mapAndroidKeyCodeToWindowsVk(keyCode: Int): Int? =
        when (keyCode) {
            KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x0D
            KeyEvent.KEYCODE_SPACE -> 0x20
            KeyEvent.KEYCODE_TAB -> 0x09
            KeyEvent.KEYCODE_ESCAPE -> 0x1B
            KeyEvent.KEYCODE_FORWARD_DEL -> 0x2E
            KeyEvent.KEYCODE_DPAD_LEFT -> 0x25
            KeyEvent.KEYCODE_DPAD_UP -> 0x26
            KeyEvent.KEYCODE_DPAD_RIGHT -> 0x27
            KeyEvent.KEYCODE_DPAD_DOWN -> 0x28
            KeyEvent.KEYCODE_MOVE_HOME -> 0x24
            KeyEvent.KEYCODE_MOVE_END -> 0x23
            KeyEvent.KEYCODE_PAGE_UP -> 0x21
            KeyEvent.KEYCODE_PAGE_DOWN -> 0x22
            KeyEvent.KEYCODE_DEL -> 0x08
            KeyEvent.KEYCODE_INSERT -> 0x2D
            // Modifiers, so host-side shortcuts (Ctrl+C, Alt+Tab, Shift-select) work at all.
            KeyEvent.KEYCODE_SHIFT_LEFT -> 0xA0
            KeyEvent.KEYCODE_SHIFT_RIGHT -> 0xA1
            KeyEvent.KEYCODE_CTRL_LEFT -> 0xA2
            KeyEvent.KEYCODE_CTRL_RIGHT -> 0xA3
            KeyEvent.KEYCODE_ALT_LEFT -> 0xA4
            KeyEvent.KEYCODE_ALT_RIGHT -> 0xA5
            KeyEvent.KEYCODE_META_LEFT -> 0x5B
            KeyEvent.KEYCODE_META_RIGHT -> 0x5C
            in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 ->
                0x70 + (keyCode - KeyEvent.KEYCODE_F1)
            in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z ->
                0x41 + (keyCode - KeyEvent.KEYCODE_A)
            in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 ->
                0x30 + (keyCode - KeyEvent.KEYCODE_0)
            else -> null
        }

    // Key events only ever arrived through InputConnection.sendKeyEvent, so a physical or
    // Bluetooth keyboard attached to the tablet was silently ignored in the viewer.
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (currentScene == UiScene.VIEWER && event.deviceId != KeyCharacterMap.VIRTUAL_KEYBOARD) {
            if (event.keyCode == KeyEvent.KEYCODE_BACK) return super.dispatchKeyEvent(event)
            val vk = mapAndroidKeyCodeToWindowsVk(event.keyCode)
            // VK down/up only. The host posts a real WM_KEYDOWN with a scan code, and the
            // target app's own TranslateMessage synthesises WM_CHAR from it — sending the
            // character here as well doubled every keystroke ("hello" -> "hheelllloo").
            if (vk != null && queueViewerSpecialKey(vk, event.action)) {
                return true
            }
        }
        return super.dispatchKeyEvent(event)
    }

    private fun resolveViewerContentRect(): ViewerContentRect? {
        val viewWidth = videoTextureView.width.toFloat()
        val viewHeight = videoTextureView.height.toFloat()
        val contentWidth = if (videoWidth > 0) videoWidth else expectedContentWidth
        val contentHeight = if (videoHeight > 0) videoHeight else expectedContentHeight
        if (viewWidth <= 0f || viewHeight <= 0f || contentWidth <= 0 || contentHeight <= 0) {
            return null
        }

        val viewAspect = viewWidth / viewHeight
        val contentAspect = contentWidth.toFloat() / contentHeight.toFloat()
        return if (contentAspect > viewAspect) {
            val displayHeight = viewWidth / contentAspect
            ViewerContentRect(
                left = 0f,
                top = (viewHeight - displayHeight) * 0.5f,
                width = viewWidth,
                height = displayHeight,
                contentWidth = contentWidth,
                contentHeight = contentHeight
            )
        } else {
            val displayWidth = viewHeight * contentAspect
            ViewerContentRect(
                left = (viewWidth - displayWidth) * 0.5f,
                top = 0f,
                width = displayWidth,
                height = viewHeight,
                contentWidth = contentWidth,
                contentHeight = contentHeight
            )
        }
    }

    private fun mapTouchToVideoCoords(touchX: Float, touchY: Float, clampToContent: Boolean): Pair<Int, Int>? {
        val contentRect = resolveViewerContentRect() ?: return null
        val minX = contentRect.left
        val maxX = contentRect.left + contentRect.width
        val minY = contentRect.top
        val maxY = contentRect.top + contentRect.height
        val mappedX =
            if (clampToContent) {
                touchX.coerceIn(minX, maxX)
            } else {
                if (touchX < minX || touchX > maxX) return null
                touchX
            }
        val mappedY =
            if (clampToContent) {
                touchY.coerceIn(minY, maxY)
            } else {
                if (touchY < minY || touchY > maxY) return null
                touchY
            }

        val relX =
            ((mappedX - contentRect.left) / contentRect.width)
                .coerceIn(0f, 1f)
        val relY =
            ((mappedY - contentRect.top) / contentRect.height)
                .coerceIn(0f, 1f)
        val videoX =
            (relX * (contentRect.contentWidth - 1).toFloat())
                .roundToInt()
                .coerceIn(0, contentRect.contentWidth - 1)
        val videoY =
            (relY * (contentRect.contentHeight - 1).toFloat())
                .roundToInt()
                .coerceIn(0, contentRect.contentHeight - 1)
        return Pair(videoX, videoY)
    }

    private fun renderStatus() {
        val nowMs = SystemClock.elapsedRealtime()
        val statusValue = NativeSessionBridge.nativeGetStatus()
        val errorValue = NativeSessionBridge.nativeGetLastError()
        val panelSnapshot = parseWindowPanelSnapshot(NativeSessionBridge.nativeGetWindowPanelJson())
        syncExpectedContentSize(panelSnapshot, if (selectionStage != SelectionStage.IDLE) "selection_pending" else "panel_snapshot")
        val videoDebugValue = NativeSessionBridge.nativeGetVideoDebugStatus()
        val readySelectionGeneration = NativeSessionBridge.nativeGetReadySelectionGeneration()
        val lastOutputPresentationUs = NativeSessionBridge.nativeGetLastOutputPresentationUs()
        val isConnecting = statusValue.startsWith("connecting")
        val isConnected = statusValue.startsWith("connected")
        connectFlowActive = isConnecting || isConnected

        val idleScene = homeScene()
        val onIdleScreen = currentScene == UiScene.LOGIN || currentScene == UiScene.HOSTS ||
            currentScene == UiScene.CONNECT
        if (!isConnecting && !isConnected) {
            if (!onIdleScreen) {
                currentScene = idleScene
                desiredStreamActive = false
                clearPendingSelection()
                resetViewerObservability()
            }
        } else if (onIdleScreen && connectFlowActive) {
            currentScene = UiScene.TARGETS
        }

        if (selectionStage != SelectionStage.IDLE) {
            if (panelSnapshot.status.startsWith("window_select_failed")) {
                diagnosticsLog.log(
                    "select_failed",
                    "targetId=$pendingSelectionId gen=$pendingSelectionGeneration " +
                        "seq=${panelSnapshot.lastSelectSeq} status=${panelSnapshot.status}"
                )
                moveToTargets("select_failed", abortPendingSwitch = true)
            } else {
                if (!pendingSelectionAckLogged && panelSnapshot.status.startsWith("window_selected")) {
                    selectionStage = SelectionStage.WAITING_FIRST_FRAME
                    pendingSelectionAckLogged = true
                    diagnosticsLog.log(
                        "select_ack",
                        "targetId=$pendingSelectionId gen=$pendingSelectionGeneration " +
                            "streamGen=${panelSnapshot.lastSelectStreamGeneration} " +
                            "hostSendQpcUs=${panelSnapshot.lastSelectHostSendQpcUs} " +
                            "title=${panelSnapshot.selectedTitle}"
                    )
                }
                if (readySelectionGeneration == pendingSelectionGeneration && pendingSelectionGeneration != 0L) {
                    diagnosticsLog.log(
                        "select_ready",
                        "targetId=$pendingSelectionId gen=$pendingSelectionGeneration " +
                            "lastOutUs=$lastOutputPresentationUs title=${panelSnapshot.selectedTitle}"
                    )
                    clearPendingSelection()
                    currentScene = UiScene.VIEWER
                    showViewerControls(emphasized = true)
                    showRightClickHintOnce()
                } else if (pendingSelectionStartedAtMs > 0L && nowMs - pendingSelectionStartedAtMs >= 6000L) {
                    diagnosticsLog.log(
                        "select_timeout",
                        "targetId=$pendingSelectionId gen=$pendingSelectionGeneration " +
                            "scene=$currentScene status=${panelSnapshot.status} debug=$videoDebugValue"
                    )
                    moveToTargets("select_timeout", abortPendingSwitch = true)
                }
            }
        } else if (currentScene == UiScene.SWITCHING) {
            currentScene = UiScene.VIEWER
            showViewerControls(emphasized = true)
        }

        syncConnectedClientPreferences(isConnected)

        connectStatusText.text = statusValue
        // The log path is developer detail; surfacing it as the permanent error line pushed
        // the real message out of the 3-line box. It stays available in the LOG overlay.
        connectErrorText.text = errorValue
        connectErrorText.visibility = if (errorValue.isNotBlank()) View.VISIBLE else View.GONE

        renderTargetsScene(isConnected, panelSnapshot)
        renderViewerScene(statusValue, panelSnapshot, videoDebugValue)
        updateViewerLogHeader(statusValue, panelSnapshot, videoDebugValue, errorValue)
        applySceneVisibility()
        syncVideoSurface(forceRebind = false)
        observeDiagnostics(nowMs, statusValue, panelSnapshot, videoDebugValue, lastOutputPresentationUs)
    }

    private fun renderTargetsScene(isConnected: Boolean, panelSnapshot: WindowPanelUiSnapshot) {
        val selectionPending = selectionStage != SelectionStage.IDLE
        val settingsActive = activeTargetTab == TargetTab.SETTINGS
        listWindowsButton.text =
            if (activeTargetTab == TargetTab.WINDOWS) "[Windows]" else getString(R.string.target_windows_button)
        listDevicesButton.text =
            if (activeTargetTab == TargetTab.DESKTOP) "[Desktop]" else getString(R.string.target_desktop_button)
        listSettingsButton.text =
            if (settingsActive) "[Settings]" else getString(R.string.target_settings_button)
        listWindowsButton.isEnabled = activeTargetTab != TargetTab.WINDOWS && !selectionPending
        listDevicesButton.isEnabled = activeTargetTab != TargetTab.DESKTOP && !selectionPending
        listSettingsButton.isEnabled = !settingsActive && !selectionPending
        listRefreshButton.isEnabled = isConnected && !selectionPending && !settingsActive
        listRefreshButton.visibility = if (settingsActive) View.GONE else View.VISIBLE
        listDisconnectButton.isEnabled = isConnected || connectFlowActive
        targetListView.isEnabled = isConnected && !selectionPending && !settingsActive
        settingsPanel.visibility = if (settingsActive) View.VISIBLE else View.GONE
        settingsBitrateInput.isEnabled = isConnected && !selectionPending
        settingsFpsInput.isEnabled = isConnected && !selectionPending
        settingsDesktopBackendDxgiButton.isEnabled = isConnected && !selectionPending
        settingsDesktopBackendWgcButton.isEnabled = isConnected && !selectionPending
        settingsApplyButton.isEnabled = isConnected && !selectionPending
        targetListView.visibility = if (settingsActive) View.GONE else View.VISIBLE
        updateDesktopBackendButtons()

        listSelectedText.text = "Selected: ${panelSnapshot.selectedTitle}"
        listStatusText.text =
            when (selectionStage) {
                SelectionStage.REQUESTING -> "selecting $pendingSelectionLabel..."
                SelectionStage.WAITING_FIRST_FRAME -> "waiting first frame for $pendingSelectionLabel..."
                SelectionStage.IDLE -> panelSnapshot.status
            }

        val labels = mutableListOf<String>()
        val ids = mutableListOf<Long>()

        when (activeTargetTab) {
            TargetTab.WINDOWS -> {
                panelSnapshot.items.forEach { item ->
                    val minimizedSuffix = if (item.minimized) " • minimized" else ""
                    labels.add(item.title + minimizedSuffix)
                    ids.add(item.id)
                }
                targetListEmptyText.text = getString(R.string.targets_empty)
            }

            TargetTab.DESKTOP -> {
                labels.add(getString(R.string.desktop_mode_button))
                ids.add(0L)
                targetListEmptyText.text = ""
            }
            TargetTab.SETTINGS -> {
                settingsAppliedText.text = settingsStatusMessage
            }
        }

        // renderStatus runs on a 250 ms poll. Rebuilding the adapter unconditionally reset the
        // list four times a second and fought the user's scroll, so only publish real changes.
        if (targetListLabels != labels || targetListIds != ids ||
            targetListSelectedId != panelSnapshot.selectedId
        ) {
            targetListLabels.clear()
            targetListLabels.addAll(labels)
            targetListIds.clear()
            targetListIds.addAll(ids)
            targetListSelectedId = panelSnapshot.selectedId
            targetListAdapter.notifyDataSetChanged()
        }
        if (!settingsActive) refreshThumbnails(panelSnapshot.items)
        targetListEmptyText.visibility =
            if (settingsActive) View.GONE else if (targetListLabels.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun renderViewerScene(statusValue: String, panelSnapshot: WindowPanelUiSnapshot, videoDebugValue: String) {
        if (currentScene == UiScene.SWITCHING) {
            // Before this panel existed the switch showed a plain black screen with no
            // feedback for up to the 6 s selection timeout.
            val target = pendingSelectionLabel.ifBlank { panelSnapshot.selectedTitle }
            viewerLoadingText.text = getString(R.string.viewer_switching_to, target)
            viewerLoadingPanel.visibility = View.VISIBLE
            viewerOverlayStatusText.visibility = View.GONE
            videoTextureView.alpha = 0.0f
            return
        }
        viewerLoadingPanel.visibility = View.GONE
        videoTextureView.alpha = 1.0f
        applyViewerOrientation()
        renderDataUsage()
        val hasFrame = videoWidth > 0 && videoHeight > 0
        // A frozen last frame with no overlay looks like a live desktop. Treat a stalled
        // decoder or a dropped connection as reasons to bring the status text back.
        val lastOutputUs = NativeSessionBridge.nativeGetLastOutputPresentationUs()
        val nowUs = SystemClock.elapsedRealtime() * 1000L
        if (lastOutputUs != lastVideoOutputPtsUs) {
            lastVideoOutputPtsUs = lastOutputUs
            lastVideoOutputSeenUs = nowUs
        }
        val videoStalled = hasFrame && lastVideoOutputSeenUs > 0L &&
            nowUs - lastVideoOutputSeenUs > VIEWER_STALL_OVERLAY_US
        val disconnected = !statusValue.startsWith("connected")
        if (hasFrame && !videoStalled && !disconnected) {
            viewerOverlayStatusText.visibility = View.GONE
        } else {
            viewerOverlayStatusText.visibility = View.VISIBLE
            viewerOverlayStatusText.text =
                panelSnapshot.selectedTitle + " • " + statusValue + "\n" + videoDebugValue
        }
    }

    // ---------------------------------------------------------------- directory sign-in

    /**
     * Starts on the sign-in screen, or straight on the PC list when a stored session is still
     * valid. Connecting by IP stays available for a LAN where no server is running.
     */
    private fun initDirectoryUi() {
        hostListAdapter = HostCardAdapter()
        hostsListView.adapter = hostListAdapter
        hostsListView.setOnItemClickListener { _, _, position, _ ->
            directoryHosts.getOrNull(position)?.let { connectToDirectoryHost(it) }
        }

        loginServerInput.setText(DirectoryClient.savedUrl(this))
        loginIdInput.setText(DirectoryClient.savedAccountId(this))

        loginButton.setOnClickListener { performLogin() }
        loginManualButton.setOnClickListener {
            diagnosticsLog.log("login_manual", "scene=$currentScene")
            manualConnectMode = true
            currentScene = UiScene.CONNECT
            renderStatus()
        }
        hostsRefreshButton.setOnClickListener { loadHosts("refresh") }
        hostsLogoutButton.setOnClickListener {
            manualConnectMode = false
            DirectoryClient.clearSession(this)
            directoryHosts = emptyList()
            hostListAdapter.notifyDataSetChanged()
            currentScene = UiScene.LOGIN
            loginErrorText.text = ""
            renderStatus()
        }

        if (DirectoryClient.savedSessionToken(this).isNotEmpty()) {
            currentScene = UiScene.HOSTS
            loadHosts("resume")
        }
    }

    /** Where the app sits when no session is running. */
    private fun homeScene(): UiScene = when {
        manualConnectMode -> UiScene.CONNECT
        DirectoryClient.savedSessionToken(this).isNotEmpty() -> UiScene.HOSTS
        else -> UiScene.LOGIN
    }

    private fun performLogin() {
        if (directoryBusy) return
        val url = loginServerInput.text?.toString()?.trim().orEmpty()
        val id = loginIdInput.text?.toString()?.trim().orEmpty()
        val password = loginPasswordInput.text?.toString().orEmpty()
        if (url.isEmpty()) {
            loginErrorText.text = getString(R.string.login_needs_server)
            return
        }
        if (id.isEmpty() || password.isEmpty()) {
            loginErrorText.text = getString(R.string.login_needs_credentials)
            return
        }

        setDirectoryBusy(true)
        loginErrorText.text = getString(R.string.login_signing_in)
        // Kept whatever the outcome, so a failed attempt does not wipe the form.
        DirectoryClient.rememberEndpoint(this, url, id)
        diagnosticsLog.log("login_attempt", "server=$url id=$id")
        directoryExecutor.execute {
            try {
                val (token, expiresAt) = DirectoryClient.login(url, id, password)
                DirectoryClient.saveSession(this, url, id, token, expiresAt)
                runOnUiThread {
                    setDirectoryBusy(false)
                    manualConnectMode = false
                    // Nothing keeps the password around once it has been exchanged for a token.
                    loginPasswordInput.setText("")
                    loginErrorText.text = ""
                    currentScene = UiScene.HOSTS
                    loadHosts("login")
                    renderStatus()
                }
            } catch (e: Exception) {
                diagnosticsLog.log("login_failed", e.message.orEmpty())
                runOnUiThread {
                    setDirectoryBusy(false)
                    loginErrorText.text = e.message ?: "로그인에 실패했습니다"
                }
            }
        }
    }

    private fun loadHosts(reason: String) {
        val url = DirectoryClient.savedUrl(this)
        val token = DirectoryClient.savedSessionToken(this)
        if (url.isEmpty() || token.isEmpty()) {
            currentScene = UiScene.LOGIN
            renderStatus()
            return
        }
        setDirectoryBusy(true)
        hostsStatusText.text = getString(R.string.hosts_loading)
        directoryExecutor.execute {
            try {
                val list = DirectoryClient.hosts(url, token)
                runOnUiThread {
                    setDirectoryBusy(false)
                    directoryHosts = list
                    hostListAdapter.notifyDataSetChanged()
                    hostsStatusText.text = DirectoryClient.savedAccountId(this)
                    renderStatus()
                }
            } catch (e: Exception) {
                diagnosticsLog.log("hosts_failed", "reason=$reason error=${e.message}")
                runOnUiThread {
                    setDirectoryBusy(false)
                    // An expired or revoked token is the common case; ask for the password again
                    // rather than leaving an empty list that looks like "no PCs registered".
                    if (e is DirectoryClient.DirectoryException && e.message?.contains("로그인") == true) {
                        DirectoryClient.clearSession(this)
                        currentScene = UiScene.LOGIN
                        loginErrorText.text = e.message
                    } else {
                        hostsStatusText.text = e.message ?: ""
                    }
                    renderStatus()
                }
            }
        }
    }

    /**
     * Address exchange, then punch, then the ordinary session. The address probe has to run on
     * the media socket, so it lives in native code; only the HTTP call happens here.
     */
    private fun connectToDirectoryHost(host: DirectoryClient.Host) {
        if (directoryBusy) return
        if (!host.online) {
            hostsStatusText.text = getString(R.string.hosts_offline_detail)
            return
        }
        val url = DirectoryClient.savedUrl(this)
        val token = DirectoryClient.savedSessionToken(this)
        if (url.isEmpty() || token.isEmpty()) {
            currentScene = UiScene.LOGIN
            renderStatus()
            return
        }

        setDirectoryBusy(true)
        directoryConnectingName = host.hostName
        hostsStatusText.text = getString(R.string.hosts_connecting, host.hostName)
        diagnosticsLog.log("directory_connect", "host=${host.hostName} id=${host.hostId}")

        val observeToken = DirectoryClient.newObserveToken()
        val directoryHost = DirectoryClient.hostFor(url)
        val observePort = DirectoryClient.observePortFor(url)

        directoryExecutor.execute {
            try {
                val observed = NativeSessionBridge.nativeDirectoryObserve(
                    directoryHost, observePort, observeToken
                )
                if (observed.isEmpty()) {
                    throw DirectoryClient.DirectoryException(
                        NativeSessionBridge.nativeDirectoryLastError().ifEmpty { "주소 확인 실패" }
                    )
                }
                diagnosticsLog.log("directory_observed", observed)

                val target = DirectoryClient.connect(url, token, host.hostId, observeToken)
                diagnosticsLog.log("directory_target", "${target.ip}:${target.port}")

                val started = NativeSessionBridge.nativeDirectoryConnect(target.ip, target.port, 4000)
                if (!started) {
                    throw DirectoryClient.DirectoryException(
                        NativeSessionBridge.nativeDirectoryLastError().ifEmpty { "연결을 시작하지 못했습니다" }
                    )
                }
                runOnUiThread {
                    setDirectoryBusy(false)
                    directoryConnectingName = ""
                    connectFlowActive = true
                    pendingRuntimeConfigSync = true
                    pendingDesktopBackendSync = true
                    desiredStreamActive = false
                    lastAppliedStreamActive = null
                    clearPendingSelection()
                    currentScene = UiScene.TARGETS
                    NativeSessionBridge.nativeRequestWindowList()
                    renderStatus()
                }
            } catch (e: Exception) {
                diagnosticsLog.log("directory_connect_failed", e.message.orEmpty())
                runOnUiThread {
                    setDirectoryBusy(false)
                    directoryConnectingName = ""
                    hostsStatusText.text = getString(R.string.hosts_connect_failed, e.message.orEmpty())
                    renderStatus()
                }
            }
        }
    }

    private fun setDirectoryBusy(busy: Boolean) {
        directoryBusy = busy
        loginButton.isEnabled = !busy
        hostsRefreshButton.isEnabled = !busy
        hostsListView.isEnabled = !busy
    }

    private inner class HostCardAdapter : BaseAdapter() {
        override fun getCount(): Int = directoryHosts.size

        override fun getItem(position: Int): Any = directoryHosts[position]

        override fun getItemId(position: Int): Long = position.toLong()

        override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
            val view = convertView ?: layoutInflater.inflate(R.layout.host_card, parent, false)
            val host = directoryHosts[position]
            view.findViewById<TextView>(R.id.hostCardName).text = host.hostName
            view.findViewById<TextView>(R.id.hostCardDetail).text =
                if (host.online) getString(R.string.hosts_title) else getString(R.string.hosts_offline_detail)
            val state = view.findViewById<TextView>(R.id.hostCardState)
            state.text =
                if (host.online) getString(R.string.hosts_online) else getString(R.string.hosts_offline)
            state.setTextColor(if (host.online) Color.parseColor("#1B7F3B") else Color.parseColor("#8A8A8A"))
            view.alpha = if (host.online) 1.0f else 0.55f
            return view
        }
    }

    private fun applySceneVisibility() {
        loginScene.visibility = if (currentScene == UiScene.LOGIN) View.VISIBLE else View.GONE
        hostsScene.visibility = if (currentScene == UiScene.HOSTS) View.VISIBLE else View.GONE
        hostsEmptyText.visibility =
            if (currentScene == UiScene.HOSTS && directoryHosts.isEmpty()) View.VISIBLE else View.GONE
        connectScene.visibility = if (currentScene == UiScene.CONNECT) View.VISIBLE else View.GONE
        targetsScene.visibility = if (currentScene == UiScene.TARGETS) View.VISIBLE else View.GONE
        viewerScene.visibility =
            if (currentScene == UiScene.VIEWER || currentScene == UiScene.SWITCHING) View.VISIBLE else View.GONE
        if (currentScene != UiScene.VIEWER && currentScene != UiScene.SWITCHING) {
            dismissViewerLogDialog()
            releaseViewerModifiers()
        }
        if (currentScene != UiScene.VIEWER && currentScene != UiScene.SWITCHING && videoSurface != null) {
            releaseVideoSurface()
        }
        if (currentScene != UiScene.VIEWER) {
            statusHandler.removeCallbacks(viewerControlsFadeRunnable)
            virtualMouse?.hide()
            renderViewerModeBanner()
            if (NativeSessionBridge.nativeMacroState() != MACRO_STATE_IDLE) {
                NativeSessionBridge.nativeMacroStopPlayback()
                NativeSessionBridge.nativeMacroStopRecording()
                renderMacroUi()
            }
        }
    }

    private fun bindVideoSurface(surfaceTexture: SurfaceTexture?) {
        if (surfaceTexture == null) {
            releaseVideoSurface()
            return
        }

        val targetBufferWidth = resolveSurfaceBufferWidth()
        val targetBufferHeight = resolveSurfaceBufferHeight()
        if (targetBufferWidth <= 0 || targetBufferHeight <= 0) {
            Log.w(LOG_TAG, "skip binding surface without resolved buffer size")
            return
        }

        val bufferSizeChanged =
            surfaceBufferWidth != targetBufferWidth || surfaceBufferHeight != targetBufferHeight
        if (videoSurface == null) {
            surfaceTexture.setDefaultBufferSize(targetBufferWidth, targetBufferHeight)
            surfaceBufferWidth = targetBufferWidth
            surfaceBufferHeight = targetBufferHeight
            val surface = Surface(surfaceTexture)
            videoSurface = surface
            NativeSessionBridge.nativeSetSurface(surface)
            diagnosticsLog.log(
                "viewer_surface_bound",
                "buffer=${surfaceBufferWidth}x${surfaceBufferHeight} video=${videoWidth}x${videoHeight} " +
                    "expected=${expectedContentWidth}x${expectedContentHeight} " +
                    "view=${videoTextureView.width}x${videoTextureView.height}"
            )
        } else if (bufferSizeChanged) {
            surfaceTexture.setDefaultBufferSize(targetBufferWidth, targetBufferHeight)
            surfaceBufferWidth = targetBufferWidth
            surfaceBufferHeight = targetBufferHeight
            diagnosticsLog.log(
                "viewer_surface_buffer_resize",
                "buffer=${surfaceBufferWidth}x${surfaceBufferHeight} video=${videoWidth}x${videoHeight} " +
                    "expected=${expectedContentWidth}x${expectedContentHeight} " +
                    "view=${videoTextureView.width}x${videoTextureView.height}"
            )
        }
        applyVideoTransform()
    }

    private fun releaseVideoSurface() {
        if (videoSurface == null) return
        NativeSessionBridge.nativeSetSurface(null)
        videoSurface?.release()
        videoSurface = null
        surfaceBufferWidth = 0
        surfaceBufferHeight = 0
        diagnosticsLog.log("viewer_surface_released", "scene=$currentScene")
    }

    private fun syncVideoSurface(forceRebind: Boolean) {
        if (currentScene != UiScene.VIEWER && currentScene != UiScene.SWITCHING) {
            if (videoSurface != null) {
                releaseVideoSurface()
            }
            return
        }

        val packedSize = NativeSessionBridge.nativeGetVideoSizePacked()
        val latestVideoWidth = (packedSize ushr 32).toInt()
        val latestVideoHeight = (packedSize and 0xffffffffL).toInt()
        val videoSizeChanged = latestVideoWidth != videoWidth || latestVideoHeight != videoHeight
        if (videoSizeChanged) {
            videoWidth = latestVideoWidth
            videoHeight = latestVideoHeight
            diagnosticsLog.log(
                "video_size",
                "video=${videoWidth}x${videoHeight} expected=${expectedContentWidth}x${expectedContentHeight}"
            )
        }

        if (!videoTextureView.isAvailable) {
            return
        }

        if (currentScene == UiScene.SWITCHING &&
            videoSurface != null &&
            videoSizeChanged &&
            !forceRebind
        ) {
            diagnosticsLog.log(
                "viewer_surface_resize_deferred",
                "video=${videoWidth}x${videoHeight} buffer=${surfaceBufferWidth}x${surfaceBufferHeight}"
            )
            applyVideoTransform()
            return
        }

        val targetBufferWidth = resolveSurfaceBufferWidth()
        val targetBufferHeight = resolveSurfaceBufferHeight()
        if (targetBufferWidth <= 0 || targetBufferHeight <= 0) {
            return
        }

        if (forceRebind ||
            videoSizeChanged ||
            videoSurface == null ||
            surfaceBufferWidth != targetBufferWidth ||
            surfaceBufferHeight != targetBufferHeight) {
            bindVideoSurface(videoTextureView.surfaceTexture)
            return
        }

        applyVideoTransform()
    }

    private fun resolveSurfaceBufferWidth(): Int {
        if (videoWidth > 0) return videoWidth
        if (expectedContentWidth > 0) return expectedContentWidth
        if (videoTextureView.width > 0) return videoTextureView.width
        return 0
    }

    private fun resolveSurfaceBufferHeight(): Int {
        if (videoHeight > 0) return videoHeight
        if (expectedContentHeight > 0) return expectedContentHeight
        if (videoTextureView.height > 0) return videoTextureView.height
        return 0
    }

    private fun applyVideoTransform() {
        val viewWidth = videoTextureView.width.toFloat()
        val viewHeight = videoTextureView.height.toFloat()
        val resolvedContentWidth =
            when {
                videoWidth > 0 -> videoWidth
                expectedContentWidth > 0 -> expectedContentWidth
                else -> surfaceBufferWidth
            }
        val resolvedContentHeight =
            when {
                videoHeight > 0 -> videoHeight
                expectedContentHeight > 0 -> expectedContentHeight
                else -> surfaceBufferHeight
            }
        val contentWidth = resolvedContentWidth.toFloat()
        val contentHeight = resolvedContentHeight.toFloat()
        val matrix = Matrix()
        if (viewWidth <= 0f || viewHeight <= 0f || contentWidth <= 0f || contentHeight <= 0f) {
            videoTextureView.setTransform(matrix)
            return
        }

        val viewAspect = viewWidth / viewHeight
        val contentAspect = contentWidth / contentHeight
        val scaleX: Float
        val scaleY: Float
        if (contentAspect > viewAspect) {
            scaleX = 1f
            scaleY = viewAspect / contentAspect
        } else {
            scaleX = contentAspect / viewAspect
            scaleY = 1f
        }
        matrix.setScale(scaleX, scaleY, viewWidth / 2f, viewHeight / 2f)
        videoTextureView.setTransform(matrix)
    }

    private fun parseWindowPanelSnapshot(rawJson: String): WindowPanelUiSnapshot {
        if (rawJson.isBlank()) return WindowPanelUiSnapshot.EMPTY
        return try {
            val root = JSONObject(rawJson)
            val itemsJson = root.optJSONArray("items")
            val items = buildList {
                if (itemsJson != null) {
                    for (index in 0 until itemsJson.length()) {
                        val item = itemsJson.optJSONObject(index) ?: continue
                        add(
                            WindowPanelItem(
                                id = item.optLong("id"),
                                title = item.optString("title", "window"),
                                width = item.optInt("width"),
                                height = item.optInt("height"),
                                minimized = item.optBoolean("minimized"),
                                thumbVersion = item.optLong("thumbVersion"),
                            )
                        )
                    }
                }
            }
            WindowPanelUiSnapshot(
                selectedId = root.optLong("selectedId"),
                selectedTitle = root.optString("selectedTitle", "desktop"),
                selectedWidth = root.optInt("selectedWidth"),
                selectedHeight = root.optInt("selectedHeight"),
                selectionLocked = root.optBoolean("selectionLocked"),
                status = root.optString("status", "waiting_control"),
                lastSelectSeq = root.optInt("lastSelectSeq"),
                lastSelectOk = root.optBoolean("lastSelectOk"),
                lastSelectWindowId = root.optLong("lastSelectWindowId"),
                lastSelectStreamGeneration = root.optLong("lastSelectStreamGeneration"),
                lastSelectHostSendQpcUs = root.optLong("lastSelectHostSendQpcUs"),
                items = items,
            )
        } catch (_: JSONException) {
            WindowPanelUiSnapshot.EMPTY
        }
    }

    private fun observeDiagnostics(
        nowMs: Long,
        statusValue: String,
        panelSnapshot: WindowPanelUiSnapshot,
        videoDebugValue: String,
        lastOutputPresentationUs: Long,
    ) {
        if (currentScene != lastLoggedScene) {
            diagnosticsLog.log("scene", currentScene.name)
            lastLoggedScene = currentScene
        }
        if (statusValue != lastLoggedStatus) {
            diagnosticsLog.log("status", statusValue)
            lastLoggedStatus = statusValue
        }
        if (panelSnapshot.status != lastLoggedPanelStatus) {
            diagnosticsLog.log(
                "panel_status",
                panelSnapshot.status + " selectedId=" + panelSnapshot.selectedId + " selectedTitle=" + panelSnapshot.selectedTitle
            )
            lastLoggedPanelStatus = panelSnapshot.status
        }
        if (videoDebugValue != lastLoggedVideoDebug &&
            (currentScene == UiScene.VIEWER || currentScene == UiScene.SWITCHING)
        ) {
            diagnosticsLog.log("video_debug", videoDebugValue)
            lastLoggedVideoDebug = videoDebugValue
        }

        if (currentScene == UiScene.VIEWER && statusValue.startsWith("connected")) {
            val outCount = parseVideoCounter(videoDebugValue, "out")
            if (outCount >= 0) {
                if (outCount != lastViewerOutCount) {
                    lastViewerOutCount = outCount
                    lastViewerOutChangeAtMs = nowMs
                    lastViewerStallLogAtMs = 0L
                    lastViewerRecoveryAttempts = 0
                } else {
                    if (lastViewerOutChangeAtMs == 0L) {
                        lastViewerOutChangeAtMs = nowMs
                    }
                    val stalledForMs = nowMs - lastViewerOutChangeAtMs
                    if (stalledForMs >= 5000L && nowMs - lastViewerStallLogAtMs >= 5000L) {
                        diagnosticsLog.log(
                            "viewer_stall",
                            "stalledMs=$stalledForMs out=$outCount scene=$currentScene " +
                                "selectedId=${panelSnapshot.selectedId} selected=${panelSnapshot.selectedTitle} " +
                                "lastOutChangeMs=$lastViewerOutChangeAtMs lastOutPtsUs=$lastOutputPresentationUs " +
                                "hostStatus=${panelSnapshot.status} status=$statusValue debug=$videoDebugValue"
                        )
                        lastViewerStallLogAtMs = nowMs
                        recoverFromViewerStall(nowMs, panelSnapshot, statusValue, videoDebugValue, lastOutputPresentationUs)
                    }
                }
            }
        } else {
            lastViewerOutCount = -1
            lastViewerOutChangeAtMs = 0L
            lastViewerStallLogAtMs = 0L
            lastViewerRecoveryAttempts = 0
        }
    }

    private fun recoverFromViewerStall(
        nowMs: Long,
        panelSnapshot: WindowPanelUiSnapshot,
        statusValue: String,
        videoDebugValue: String,
        lastOutputPresentationUs: Long,
    ) {
        if (selectionStage != SelectionStage.IDLE) return

        val targetId = panelSnapshot.selectedId
        val targetLabel = panelSnapshot.selectedTitle.ifBlank { if (targetId == 0L) "desktop" else "window" }
        val targetTab = if (targetId == 0L) TargetTab.DESKTOP else TargetTab.WINDOWS

        if (lastViewerRecoveryTargetId != targetId || nowMs - lastViewerRecoveryAtMs >= 15000L) {
            lastViewerRecoveryTargetId = targetId
            lastViewerRecoveryAtMs = nowMs
            lastViewerRecoveryAttempts = 1
            diagnosticsLog.log(
                "viewer_stall_recover",
                "action=reselect targetId=$targetId label=$targetLabel status=$statusValue " +
                    "hostStatus=${panelSnapshot.status} lastOutPtsUs=$lastOutputPresentationUs"
            )
            startSelectionTransition(targetId, targetLabel, targetTab, "watchdog")
            return
        }

        lastViewerRecoveryAttempts += 1
        diagnosticsLog.log(
            "viewer_stall_recover",
            "action=targets targetId=$targetId attempts=$lastViewerRecoveryAttempts " +
                "status=$statusValue hostStatus=${panelSnapshot.status} debug=$videoDebugValue"
        )
        moveToTargets("viewer_stall", abortPendingSwitch = false)
    }

    private fun parseVideoCounter(debugLine: String, key: String): Int {
        val needle = "$key="
        val start = debugLine.indexOf(needle)
        if (start < 0) return -1
        val valueStart = start + needle.length
        var valueEnd = valueStart
        while (valueEnd < debugLine.length && debugLine[valueEnd].isDigit()) {
            valueEnd += 1
        }
        if (valueEnd <= valueStart) return -1
        return debugLine.substring(valueStart, valueEnd).toIntOrNull() ?: -1
    }

    private fun saveCurrentEndpoint() {
        val host = hostEdit.text?.toString()?.trim().orEmpty()
        val videoPort = videoPortEdit.text?.toString()?.toIntOrNull() ?: 43000
        val controlPort = controlPortEdit.text?.toString()?.toIntOrNull() ?: 43001
        val bitrateKbps = settingsBitrateInput.text?.toString()?.toIntOrNull() ?: requestedRuntimeBitrateKbps
        val fps = settingsFpsInput.text?.toString()?.toIntOrNull() ?: requestedRuntimeFps
        SessionPersistence.save(
            this,
            host,
            videoPort,
            controlPort,
            bitrateKbps,
            fps,
            requestedDesktopBackend.code
        )
    }
}
