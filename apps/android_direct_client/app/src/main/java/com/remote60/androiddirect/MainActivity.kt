package com.remote60.androiddirect

import android.app.Activity
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import android.widget.TextView
import org.json.JSONException
import org.json.JSONObject

class MainActivity : Activity(), TextureView.SurfaceTextureListener {
    companion object {
        private const val LOG_TAG = "remote60_android_direct"
    }

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

    private data class WindowPanelItem(
        val id: Long,
        val title: String,
        val width: Int,
        val height: Int,
        val minimized: Boolean,
    )

    private data class WindowPanelUiSnapshot(
        val selectedId: Long,
        val selectedTitle: String,
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
    private lateinit var targetListView: ListView
    private lateinit var listDisconnectButton: Button
    private lateinit var listWindowsButton: Button
    private lateinit var listDevicesButton: Button
    private lateinit var listSettingsButton: Button
    private lateinit var listRefreshButton: Button
    private lateinit var settingsPanel: View
    private lateinit var settingsBitrateInput: EditText
    private lateinit var settingsFpsInput: EditText
    private lateinit var settingsApplyButton: Button
    private lateinit var settingsAppliedText: TextView
    private lateinit var viewerBackButton: Button
    private lateinit var viewerOverlayStatusText: TextView
    private lateinit var videoTextureView: TextureView
    private lateinit var targetListAdapter: ArrayAdapter<String>
    private val targetListLabels = mutableListOf<String>()
    private val targetListIds = mutableListOf<Long>()
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
    private var settingsStatusMessage = "Current request: 8000 kbps / 30 fps"
    private var pendingRuntimeConfigSync = false
    private var desiredStreamActive = false
    private var lastAppliedStreamActive: Boolean? = null
    private var videoSurface: Surface? = null
    private var videoWidth = 0
    private var videoHeight = 0
    private var surfaceBufferWidth = 0
    private var surfaceBufferHeight = 0

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
        settingsApplyButton = findViewById(R.id.settingsApplyButton)
        settingsAppliedText = findViewById(R.id.settingsAppliedText)
        viewerBackButton = findViewById(R.id.viewerBackButton)
        viewerOverlayStatusText = findViewById(R.id.viewerOverlayStatusText)
        videoTextureView = findViewById(R.id.videoTextureView)

        videoTextureView.surfaceTextureListener = this
        videoTextureView.isOpaque = true
        videoTextureView.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            syncVideoSurface(forceRebind = false)
        }

        targetListAdapter =
            ArrayAdapter(this, android.R.layout.simple_list_item_1, targetListLabels).also {
                targetListView.adapter = it
            }
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
        settingsStatusMessage = "Current request: ${requestedRuntimeBitrateKbps} kbps / ${requestedRuntimeFps} fps"
        settingsBitrateInput.setText(requestedRuntimeBitrateKbps.toString())
        settingsFpsInput.setText(requestedRuntimeFps.toString())
        settingsAppliedText.text = settingsStatusMessage

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

        settingsApplyButton.setOnClickListener {
            val bitrateKbps = settingsBitrateInput.text?.toString()?.trim()?.toIntOrNull() ?: 0
            val fps = settingsFpsInput.text?.toString()?.trim()?.toIntOrNull() ?: 0
            if (bitrateKbps < 300 || fps !in 1..120) {
                settingsStatusMessage = "Use bitrate >= 300 kbps and fps between 1 and 120."
                settingsAppliedText.text = settingsStatusMessage
                diagnosticsLog.log("runtime_config_invalid", "bitrateKbps=$bitrateKbps fps=$fps")
                return@setOnClickListener
            }
            val bitrateBps = bitrateKbps * 1000
            val ok = NativeSessionBridge.nativeRequestRuntimeConfig(bitrateBps, fps)
            if (ok) {
                requestedRuntimeBitrateKbps = bitrateKbps
                requestedRuntimeFps = fps
                settingsStatusMessage = "Requested: ${bitrateKbps} kbps / ${fps} fps"
                settingsAppliedText.text = settingsStatusMessage
                saveCurrentEndpoint()
                diagnosticsLog.log("runtime_config_request", "bitrateBps=$bitrateBps fps=$fps")
            } else {
                settingsStatusMessage = "Runtime config request failed."
                settingsAppliedText.text = settingsStatusMessage
                diagnosticsLog.log("runtime_config_failed", "bitrateBps=$bitrateBps fps=$fps")
            }
            renderStatus()
        }

        viewerBackButton.setOnClickListener {
            diagnosticsLog.log(
                "viewer_back",
                "selected=${pendingSelectionLabel.ifBlank { "none" }} currentScene=$currentScene stage=$selectionStage"
            )
            moveToTargets("viewer_back", abortPendingSwitch = selectionStage != SelectionStage.IDLE)
            renderStatus()
        }

        renderStatus()
    }

    override fun onResume() {
        super.onResume()
        statusHandler.removeCallbacks(statusPollRunnable)
        statusHandler.post(statusPollRunnable)
        syncVideoSurface(forceRebind = false)
    }

    override fun onPause() {
        saveCurrentEndpoint()
        statusHandler.removeCallbacks(statusPollRunnable)
        super.onPause()
    }

    override fun onDestroy() {
        saveCurrentEndpoint()
        releaseVideoSurface()
        super.onDestroy()
    }

    override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
        syncVideoSurface(forceRebind = true)
    }

    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
        syncVideoSurface(forceRebind = true)
    }

    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        releaseVideoSurface()
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
    }

    private fun resetViewerObservability() {
        videoWidth = 0
        videoHeight = 0
        lastViewerOutCount = -1
        lastViewerOutChangeAtMs = 0L
        lastViewerStallLogAtMs = 0L
        lastViewerRecoveryTargetId = Long.MIN_VALUE
        lastViewerRecoveryAtMs = 0L
        lastViewerRecoveryAttempts = 0
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
                    "Current request: ${requestedRuntimeBitrateKbps} kbps / ${requestedRuntimeFps} fps"
                settingsAppliedText.text = settingsStatusMessage
                diagnosticsLog.log(
                    "runtime_config_sync",
                    "bitrateBps=$bitrateBps fps=$requestedRuntimeFps"
                )
            }
        }

        if (lastAppliedStreamActive != desiredStreamActive) {
            requestStreamActive(desiredStreamActive, "connected_sync")
        }
    }

    private fun moveToTargets(reason: String, abortPendingSwitch: Boolean) {
        diagnosticsLog.log("targets_return", "reason=$reason scene=$currentScene")
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
            "targetId=$targetId label=$label tab=$tab gen=$selectionGeneration origin=$origin"
        )
        return true
    }

    private fun renderStatus() {
        val nowMs = SystemClock.elapsedRealtime()
        val statusValue = NativeSessionBridge.nativeGetStatus()
        val errorValue = NativeSessionBridge.nativeGetLastError()
        val panelSnapshot = parseWindowPanelSnapshot(NativeSessionBridge.nativeGetWindowPanelJson())
        val videoDebugValue = NativeSessionBridge.nativeGetVideoDebugStatus()
        val readySelectionGeneration = NativeSessionBridge.nativeGetReadySelectionGeneration()
        val lastOutputPresentationUs = NativeSessionBridge.nativeGetLastOutputPresentationUs()
        val isConnecting = statusValue.startsWith("connecting")
        val isConnected = statusValue.startsWith("connected")

        if (!isConnecting && !isConnected && currentScene != UiScene.CONNECT) {
            currentScene = UiScene.CONNECT
            desiredStreamActive = false
            clearPendingSelection()
            resetViewerObservability()
        } else if (connectFlowActive && currentScene == UiScene.CONNECT && (isConnecting || isConnected)) {
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
        }

        syncConnectedClientPreferences(isConnected)

        connectStatusText.text = statusValue
        connectErrorText.text =
            if (errorValue.isNotBlank()) {
                errorValue + "\nlog: " + diagnosticsLog.filePath()
            } else {
                "log: " + diagnosticsLog.filePath()
            }

        renderTargetsScene(isConnected, panelSnapshot)
        renderViewerScene(statusValue, panelSnapshot, videoDebugValue)
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
        settingsApplyButton.isEnabled = isConnected && !selectionPending
        targetListView.visibility = if (settingsActive) View.GONE else View.VISIBLE

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
                    val prefix = if (item.id == panelSnapshot.selectedId) "* " else ""
                    val minimizedSuffix = if (item.minimized) " • minimized" else ""
                    labels.add(prefix + item.title + " • " + item.width + "x" + item.height + minimizedSuffix)
                    ids.add(item.id)
                }
                targetListEmptyText.text = "No shareable windows yet. Tap Refresh."
            }

            TargetTab.DESKTOP -> {
                val prefix = if (panelSnapshot.selectedId == 0L) "* " else ""
                labels.add(prefix + getString(R.string.desktop_mode_button) + " • full desktop capture")
                ids.add(0L)
                targetListEmptyText.text = ""
            }
            TargetTab.SETTINGS -> {
                settingsAppliedText.text = settingsStatusMessage
            }
        }

        targetListLabels.clear()
        targetListLabels.addAll(labels)
        targetListIds.clear()
        targetListIds.addAll(ids)
        targetListAdapter.notifyDataSetChanged()
        targetListEmptyText.visibility =
            if (settingsActive) View.GONE else if (targetListLabels.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun renderViewerScene(statusValue: String, panelSnapshot: WindowPanelUiSnapshot, videoDebugValue: String) {
        if (currentScene == UiScene.SWITCHING) {
            viewerOverlayStatusText.text =
                "Switching to ${pendingSelectionLabel.ifBlank { panelSnapshot.selectedTitle }}...\n" +
                    panelSnapshot.status + "\n" + videoDebugValue
            videoTextureView.alpha = 0.0f
            return
        }
        videoTextureView.alpha = 1.0f
        viewerOverlayStatusText.text = panelSnapshot.selectedTitle + " • " + statusValue + "\n" + videoDebugValue
    }

    private fun applySceneVisibility() {
        connectScene.visibility = if (currentScene == UiScene.CONNECT) View.VISIBLE else View.GONE
        targetsScene.visibility = if (currentScene == UiScene.TARGETS) View.VISIBLE else View.GONE
        viewerScene.visibility =
            if (currentScene == UiScene.VIEWER || currentScene == UiScene.SWITCHING) View.VISIBLE else View.GONE
        if (currentScene != UiScene.VIEWER && currentScene != UiScene.SWITCHING && videoSurface != null) {
            releaseVideoSurface()
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

        if (videoSurface == null || surfaceBufferWidth != targetBufferWidth || surfaceBufferHeight != targetBufferHeight) {
            releaseVideoSurface()
            surfaceTexture.setDefaultBufferSize(targetBufferWidth, targetBufferHeight)
            surfaceBufferWidth = targetBufferWidth
            surfaceBufferHeight = targetBufferHeight
            val surface = Surface(surfaceTexture)
            videoSurface = surface
            NativeSessionBridge.nativeSetSurface(surface)
            diagnosticsLog.log(
                "viewer_surface_bound",
                "buffer=${surfaceBufferWidth}x${surfaceBufferHeight} video=${videoWidth}x${videoHeight} " +
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
            diagnosticsLog.log("video_size", "video=${videoWidth}x${videoHeight}")
        }

        if (!videoTextureView.isAvailable) {
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
        if (videoTextureView.width > 0) return videoTextureView.width
        return 0
    }

    private fun resolveSurfaceBufferHeight(): Int {
        if (videoHeight > 0) return videoHeight
        if (videoTextureView.height > 0) return videoTextureView.height
        return 0
    }

    private fun applyVideoTransform() {
        val viewWidth = videoTextureView.width.toFloat()
        val viewHeight = videoTextureView.height.toFloat()
        val contentWidth = if (videoWidth > 0) videoWidth.toFloat() else surfaceBufferWidth.toFloat()
        val contentHeight = if (videoHeight > 0) videoHeight.toFloat() else surfaceBufferHeight.toFloat()
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
                            )
                        )
                    }
                }
            }
            WindowPanelUiSnapshot(
                selectedId = root.optLong("selectedId"),
                selectedTitle = root.optString("selectedTitle", "desktop"),
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
        SessionPersistence.save(this, host, videoPort, controlPort, bitrateKbps, fps)
    }
}
