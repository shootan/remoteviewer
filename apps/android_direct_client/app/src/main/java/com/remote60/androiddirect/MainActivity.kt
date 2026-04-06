package com.remote60.androiddirect

import android.app.Activity
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import org.json.JSONException
import org.json.JSONObject

class MainActivity : Activity(), TextureView.SurfaceTextureListener {
    companion object {
        private const val LOG_TAG = "remote60_android_direct"
    }

    private enum class TargetTab {
        WINDOWS,
        DEVICES,
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
        val items: List<WindowPanelItem>,
    ) {
        companion object {
            val EMPTY = WindowPanelUiSnapshot(
                selectedId = 0L,
                selectedTitle = "desktop",
                selectionLocked = false,
                status = "waiting_control",
                items = emptyList(),
            )
        }
    }

    private lateinit var hostEdit: EditText
    private lateinit var videoPortEdit: EditText
    private lateinit var controlPortEdit: EditText
    private lateinit var statusText: TextView
    private lateinit var errorText: TextView
    private lateinit var videoDebugText: TextView
    private lateinit var videoTextureView: TextureView
    private lateinit var controlsPanel: LinearLayout
    private lateinit var compactToolbar: LinearLayout
    private lateinit var targetWindowsButton: Button
    private lateinit var targetDevicesButton: Button
    private lateinit var desktopModeButton: Button
    private lateinit var panelToggleButton: Button
    private lateinit var disconnectCompactButton: Button
    private lateinit var refreshCompactButton: Button
    private lateinit var desktopModeCompactButton: Button
    private lateinit var targetSpinner: Spinner
    private lateinit var targetSpinnerAdapter: ArrayAdapter<String>
    private val targetSpinnerLabels = mutableListOf<String>()
    private val targetSpinnerIds = mutableListOf<Long>()
    private var suppressTargetSpinnerSelection = false
    private var activeTargetTab = TargetTab.WINDOWS
    private var controlsExpanded = true
    private var wasConnected = false
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

        hostEdit = findViewById(R.id.hostInput)
        videoPortEdit = findViewById(R.id.videoPortInput)
        controlPortEdit = findViewById(R.id.controlPortInput)
        statusText = findViewById(R.id.statusText)
        errorText = findViewById(R.id.errorText)
        videoDebugText = findViewById(R.id.videoDebugText)
        videoTextureView = findViewById(R.id.videoTextureView)
        controlsPanel = findViewById(R.id.controlsPanel)
        compactToolbar = findViewById(R.id.compactToolbar)
        targetWindowsButton = findViewById(R.id.targetWindowsButton)
        targetDevicesButton = findViewById(R.id.targetDevicesButton)
        desktopModeButton = findViewById(R.id.desktopModeButton)
        panelToggleButton = findViewById(R.id.panelToggleButton)
        disconnectCompactButton = findViewById(R.id.disconnectCompactButton)
        refreshCompactButton = findViewById(R.id.refreshCompactButton)
        desktopModeCompactButton = findViewById(R.id.desktopModeCompactButton)
        targetSpinner = findViewById(R.id.targetSpinner)

        videoTextureView.surfaceTextureListener = this
        videoTextureView.isOpaque = true
        videoTextureView.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            syncVideoSurface(forceRebind = false)
        }

        targetSpinnerAdapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_item, targetSpinnerLabels).also {
                it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
                targetSpinner.adapter = it
            }
        targetSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                if (suppressTargetSpinnerSelection) return
                if (position < 0 || position >= targetSpinnerIds.size) return
                val ok = when (activeTargetTab) {
                    TargetTab.WINDOWS -> NativeSessionBridge.nativeSelectWindow(targetSpinnerIds[position])
                    TargetTab.DEVICES -> NativeSessionBridge.nativeSelectDesktopMode()
                }
                if (!ok) {
                    renderStatus()
                    return
                }
                renderStatus()
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {
            }
        }

        intent.getStringExtra("host")?.trim()?.takeIf { it.isNotEmpty() }?.let {
            hostEdit.setText(it)
        }
        val launchVideoPort = intent.getIntExtra("videoPort", 0)
        if (launchVideoPort > 0) {
            videoPortEdit.setText(launchVideoPort.toString())
        }
        val launchControlPort = intent.getIntExtra("controlPort", 0)
        if (launchControlPort > 0) {
            controlPortEdit.setText(launchControlPort.toString())
        }

        val connectButton: Button = findViewById(R.id.connectButton)
        val disconnectButton: Button = findViewById(R.id.disconnectButton)
        val refreshButton: Button = findViewById(R.id.refreshButton)

        renderStatus()

        connectButton.setOnClickListener {
            val host = hostEdit.text?.toString()?.trim().orEmpty()
            val videoPort = videoPortEdit.text?.toString()?.toIntOrNull() ?: 0
            val controlPort = controlPortEdit.text?.toString()?.toIntOrNull() ?: 0
            val ok = NativeSessionBridge.nativeConnect(host, videoPort, controlPort)
            if (ok) {
                NativeSessionBridge.nativeRequestWindowList()
            }
            renderStatus()
            if (!ok) {
                errorText.text = NativeSessionBridge.nativeGetLastError()
            }
        }

        disconnectButton.setOnClickListener {
            NativeSessionBridge.nativeDisconnect()
            renderStatus()
        }

        refreshButton.setOnClickListener {
            NativeSessionBridge.nativeRequestWindowList()
            renderStatus()
        }

        targetWindowsButton.setOnClickListener {
            activeTargetTab = TargetTab.WINDOWS
            renderStatus()
        }

        targetDevicesButton.setOnClickListener {
            activeTargetTab = TargetTab.DEVICES
            renderStatus()
        }

        desktopModeButton.setOnClickListener {
            NativeSessionBridge.nativeSelectDesktopMode()
            renderStatus()
        }

        panelToggleButton.setOnClickListener {
            controlsExpanded = !controlsExpanded
            renderStatus()
        }

        disconnectCompactButton.setOnClickListener {
            NativeSessionBridge.nativeDisconnect()
            renderStatus()
        }

        refreshCompactButton.setOnClickListener {
            NativeSessionBridge.nativeRequestWindowList()
            renderStatus()
        }

        desktopModeCompactButton.setOnClickListener {
            NativeSessionBridge.nativeSelectDesktopMode()
            renderStatus()
        }
    }

    override fun onResume() {
        super.onResume()
        statusHandler.removeCallbacks(statusPollRunnable)
        statusHandler.post(statusPollRunnable)
        syncVideoSurface(forceRebind = false)
    }

    override fun onPause() {
        statusHandler.removeCallbacks(statusPollRunnable)
        super.onPause()
    }

    override fun onDestroy() {
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

    private fun bindVideoSurface(surfaceTexture: SurfaceTexture?) {
        releaseVideoSurface()
        if (surfaceTexture == null) {
            surfaceBufferWidth = 0
            surfaceBufferHeight = 0
            NativeSessionBridge.nativeSetSurface(null)
            return
        }

        val targetBufferWidth = resolveSurfaceBufferWidth()
        val targetBufferHeight = resolveSurfaceBufferHeight()
        if (targetBufferWidth <= 0 || targetBufferHeight <= 0) {
            Log.w(LOG_TAG, "skip binding surface without resolved buffer size")
            return
        }

        surfaceTexture.setDefaultBufferSize(targetBufferWidth, targetBufferHeight)
        surfaceBufferWidth = targetBufferWidth
        surfaceBufferHeight = targetBufferHeight

        val surface = Surface(surfaceTexture)
        videoSurface = surface
        NativeSessionBridge.nativeSetSurface(surface)
        Log.i(
            LOG_TAG,
            "bind video surface buffer=${surfaceBufferWidth}x${surfaceBufferHeight} " +
                "video=${videoWidth}x${videoHeight} view=${videoTextureView.width}x${videoTextureView.height}"
        )
        applyVideoTransform()
    }

    private fun releaseVideoSurface() {
        NativeSessionBridge.nativeSetSurface(null)
        videoSurface?.release()
        videoSurface = null
    }

    private fun renderStatus() {
        val statusValue = NativeSessionBridge.nativeGetStatus()
        val lastErrorValue = NativeSessionBridge.nativeGetLastError()
        val panelSnapshot = parseWindowPanelSnapshot(NativeSessionBridge.nativeGetWindowPanelJson())
        val isConnected = statusValue.startsWith("connected")

        if (isConnected != wasConnected) {
            controlsExpanded = !isConnected
            wasConnected = isConnected
        }

        statusText.text = statusValue
        errorText.text = lastErrorValue
        syncVideoSurface(forceRebind = false)
        renderTargetPanel(isConnected, panelSnapshot)
        renderCompactControls(isConnected, panelSnapshot)
        videoDebugText.text =
            NativeSessionBridge.nativeGetVideoDebugStatus() +
                " view=${videoTextureView.width}x${videoTextureView.height}" +
                " buffer=${surfaceBufferWidth}x${surfaceBufferHeight}" +
                " video=${videoWidth}x${videoHeight}"
    }

    private fun renderTargetPanel(isConnected: Boolean, panelSnapshot: WindowPanelUiSnapshot) {
        controlsPanel.visibility = if (isConnected && !controlsExpanded) View.GONE else View.VISIBLE
        targetWindowsButton.text =
            if (activeTargetTab == TargetTab.WINDOWS) "[LDPlayer]" else getString(R.string.target_windows_button)
        targetDevicesButton.text =
            if (activeTargetTab == TargetTab.DEVICES) "[Devices]" else getString(R.string.target_devices_button)
        targetWindowsButton.isEnabled = activeTargetTab != TargetTab.WINDOWS
        targetDevicesButton.isEnabled = activeTargetTab != TargetTab.DEVICES
        desktopModeButton.isEnabled = isConnected && !panelSnapshot.selectionLocked

        val labels = mutableListOf<String>()
        val ids = mutableListOf<Long>()
        var selectedIndex = 0

        when (activeTargetTab) {
            TargetTab.WINDOWS -> {
                if (panelSnapshot.items.isEmpty()) {
                    labels.add("No shareable windows. Tap Refresh.")
                } else {
                    panelSnapshot.items.forEach { item ->
                        val prefix = if (item.id == panelSnapshot.selectedId) "* " else ""
                        val minimizedSuffix = if (item.minimized) " • minimized" else ""
                        labels.add(prefix + item.title + " • " + item.width + "x" + item.height + minimizedSuffix)
                        ids.add(item.id)
                    }
                    selectedIndex = ids.indexOf(panelSnapshot.selectedId).coerceAtLeast(0)
                }
            }

            TargetTab.DEVICES -> {
                labels.add(
                    if (panelSnapshot.selectedId == 0L) {
                        "* Desktop Mode • overview capture"
                    } else {
                        "Desktop Mode • overview capture"
                    }
                )
                ids.add(0L)
            }
        }

        suppressTargetSpinnerSelection = true
        targetSpinnerLabels.clear()
        targetSpinnerLabels.addAll(labels)
        targetSpinnerIds.clear()
        targetSpinnerIds.addAll(ids)
        targetSpinnerAdapter.notifyDataSetChanged()
        if (targetSpinnerLabels.isNotEmpty()) {
            targetSpinner.setSelection(selectedIndex, false)
        }
        suppressTargetSpinnerSelection = false
        targetSpinner.isEnabled = isConnected && targetSpinnerIds.isNotEmpty() && !panelSnapshot.selectionLocked
    }

    private fun renderCompactControls(isConnected: Boolean, panelSnapshot: WindowPanelUiSnapshot) {
        compactToolbar.visibility = if (isConnected) View.VISIBLE else View.GONE
        panelToggleButton.text =
            if (controlsExpanded) getString(R.string.panel_hide_button) else getString(R.string.panel_show_button)
        disconnectCompactButton.isEnabled = isConnected
        refreshCompactButton.isEnabled = isConnected
        desktopModeCompactButton.isEnabled = isConnected && !panelSnapshot.selectionLocked
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
                items = items,
            )
        } catch (_: JSONException) {
            WindowPanelUiSnapshot.EMPTY
        }
    }

    private fun syncVideoSurface(forceRebind: Boolean) {
        val packedSize = NativeSessionBridge.nativeGetVideoSizePacked()
        val latestVideoWidth = (packedSize ushr 32).toInt()
        val latestVideoHeight = (packedSize and 0xffffffffL).toInt()
        val videoSizeChanged = latestVideoWidth != videoWidth || latestVideoHeight != videoHeight
        if (videoSizeChanged) {
            videoWidth = latestVideoWidth
            videoHeight = latestVideoHeight
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
            surfaceBufferWidth != targetBufferWidth ||
            surfaceBufferHeight != targetBufferHeight) {
            bindVideoSurface(videoTextureView.surfaceTexture)
            return
        }

        applyVideoTransform()
    }

    private fun resolveSurfaceBufferWidth(): Int {
        if (videoWidth > 0) {
            return videoWidth
        }
        if (videoTextureView.width > 0) {
            return videoTextureView.width
        }
        return 0
    }

    private fun resolveSurfaceBufferHeight(): Int {
        if (videoHeight > 0) {
            return videoHeight
        }
        if (videoTextureView.height > 0) {
            return videoTextureView.height
        }
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
}
