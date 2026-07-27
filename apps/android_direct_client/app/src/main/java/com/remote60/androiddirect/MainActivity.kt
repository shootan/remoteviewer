package com.remote60.androiddirect

import android.app.Activity
import android.app.AlertDialog
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
import android.widget.TextView
import org.json.JSONException
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
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
        private const val INPUT_VK_LBUTTON = 0x01
        private const val INPUT_VK_BACK = 0x08
        private const val INPUT_WHEEL_DELTA_STEP = 120
        // Authored in dp; converted per-device below. As a raw pixel constant one wheel
        // notch needed 4x more finger travel on a 4x-density phone than on a 1x tablet.
        private const val SCROLL_GESTURE_STEP_DP = 28f
        private const val VIEWER_STALL_OVERLAY_US = 3_000_000L
    }

    private var lastVideoOutputPtsUs = 0L
    private var lastVideoOutputSeenUs = 0L

    private val scrollGestureStepPx: Float
        get() = SCROLL_GESTURE_STEP_DP * resources.displayMetrics.density

    private enum class UiScene {
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
    private lateinit var connectScene: View
    private lateinit var targetsScene: View
    private lateinit var viewerScene: View
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
    private lateinit var viewerControlsBar: View
    private lateinit var viewerBackButton: Button
    private lateinit var viewerKeyboardButton: Button
    private lateinit var viewerScrollButton: Button
    private lateinit var viewerLogButton: Button
    private lateinit var viewerOverlayStatusText: TextView
    private lateinit var viewerLoadingPanel: View
    private lateinit var viewerLoadingText: TextView
    private lateinit var viewerImeCaptureView: ImeCaptureView
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
            val buf = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            val w = buf.int
            val h = buf.int
            if (w <= 0 || h <= 0 || raw.size < 8 + w * h * 4) continue
            val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
            bmp.copyPixelsFromBuffer(buf)
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
    private var currentScene = UiScene.CONNECT
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
    private var activeTouchButtons = 0
    private var lastTouchVideoX = 0
    private var lastTouchVideoY = 0
    private var viewerScrollModeArmed = false
    private var scrollLastTouchY = 0f
    private var scrollWheelCarryPx = 0f
    private val viewerControlsDimAlpha = 0.34f
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        applyImmersiveMode()

        diagnosticsLog = SessionDiagnosticsLog(this)
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
        viewerScrollButton = findViewById(R.id.viewerScrollButton)
        viewerLogButton = findViewById(R.id.viewerLogButton)
        viewerOverlayStatusText = findViewById(R.id.viewerOverlayStatusText)
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
            currentScene = UiScene.CONNECT
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
        viewerScrollButton.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    showViewerControls(emphasized = true)
                    setViewerScrollModeArmed(true)
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_CANCEL -> {
                    setViewerScrollModeArmed(false)
                }
            }
            false
        }
        viewerLogButton.setOnClickListener {
            toggleViewerLogDialog()
        }
        updateViewerControlButtons()

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
        setViewerScrollModeArmed(false)
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
        exitDialog?.dismiss()
        exitDialog = null
        dismissViewerLogDialog()
        setViewerScrollModeArmed(false)
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
            UiScene.TARGETS, UiScene.CONNECT -> showExitConfirmDialog()
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
        setViewerScrollModeArmed(false)
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
        setViewerScrollModeArmed(false)
        cancelActiveViewerTouch(reason)
        hideViewerKeyboard(reason)
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
                if (activeTouchPointerId != MotionEvent.INVALID_POINTER_ID) return true
                val pointerIndex = event.actionIndex
                val mapped = mapTouchToVideoCoords(
                    event.getX(pointerIndex),
                    event.getY(pointerIndex),
                    clampToContent = false
                ) ?: return false
                view.requestFocus()
                if (viewerScrollModeArmed) {
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
                activeTouchButtons = INPUT_BUTTON_PRIMARY
                lastTouchVideoX = mapped.first
                lastTouchVideoY = mapped.second
                val queued = NativeSessionBridge.nativeQueueInputEvent(
                    INPUT_KIND_MOUSE_DOWN,
                    mapped.first,
                    mapped.second,
                    0,
                    INPUT_VK_LBUTTON,
                    activeTouchButtons
                )
                if (!queued) {
                    resetViewerTouchState()
                }
                return queued
            }

            MotionEvent.ACTION_MOVE -> {
                val pointerId = activeTouchPointerId
                if (pointerId == MotionEvent.INVALID_POINTER_ID) return false
                val pointerIndex = event.findPointerIndex(pointerId)
                if (pointerIndex < 0) return true
                if (activeViewerTouchMode == ViewerTouchMode.SCROLL) {
                    val touchY = event.getY(pointerIndex)
                    scrollWheelCarryPx += scrollLastTouchY - touchY
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
                    INPUT_VK_LBUTTON,
                    0
                )
                resetViewerTouchState()
                view.performClick()
                return queued
            }

            MotionEvent.ACTION_CANCEL -> {
                cancelActiveViewerTouch("touch_cancel")
                return true
            }
        }

        return false
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
            INPUT_VK_LBUTTON,
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

    private fun resetViewerTouchState() {
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
        val targetAlpha = if (emphasized) 0.96f else 0.88f
        viewerControlsBar.animate().alpha(targetAlpha).setDuration(120L).start()
        statusHandler.removeCallbacks(viewerControlsFadeRunnable)
        val delayMs = if (emphasized) 3200L else 2400L
        statusHandler.postDelayed(viewerControlsFadeRunnable, delayMs)
    }

    private fun setViewerScrollModeArmed(armed: Boolean) {
        if (viewerScrollModeArmed == armed) return
        viewerScrollModeArmed = armed
        if (!armed && activeViewerTouchMode == ViewerTouchMode.SCROLL) {
            resetViewerTouchState()
        }
        updateViewerControlButtons()
    }

    private fun updateViewerControlButtons() {
        if (::viewerScrollButton.isInitialized) {
            viewerScrollButton.text =
                if (viewerScrollModeArmed) {
                    "[${getString(R.string.viewer_scroll_button)}]"
                } else {
                    getString(R.string.viewer_scroll_button)
                }
        }
        if (::viewerLogButton.isInitialized) {
            val logOpen = viewerLogDialog?.isShowing == true
            viewerLogButton.text =
                if (logOpen) {
                    "[${getString(R.string.viewer_log_button)}]"
                } else {
                    getString(R.string.viewer_log_button)
                }
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
            updateViewerControlButtons()
            applyImmersiveMode()
        }
        dialog.show()
        dialog.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        dialog.window?.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        viewerLogDialog = dialog
        updateViewerControlButtons()
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

        if (!isConnecting && !isConnected && currentScene != UiScene.CONNECT) {
            currentScene = UiScene.CONNECT
            desiredStreamActive = false
            clearPendingSelection()
            resetViewerObservability()
        } else if (currentScene == UiScene.CONNECT && connectFlowActive) {
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

    private fun applySceneVisibility() {
        connectScene.visibility = if (currentScene == UiScene.CONNECT) View.VISIBLE else View.GONE
        targetsScene.visibility = if (currentScene == UiScene.TARGETS) View.VISIBLE else View.GONE
        viewerScene.visibility =
            if (currentScene == UiScene.VIEWER || currentScene == UiScene.SWITCHING) View.VISIBLE else View.GONE
        if (currentScene != UiScene.VIEWER && currentScene != UiScene.SWITCHING) {
            dismissViewerLogDialog()
            setViewerScrollModeArmed(false)
        }
        if (currentScene != UiScene.VIEWER && currentScene != UiScene.SWITCHING && videoSurface != null) {
            releaseVideoSurface()
        }
        if (currentScene != UiScene.VIEWER) {
            statusHandler.removeCallbacks(viewerControlsFadeRunnable)
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
