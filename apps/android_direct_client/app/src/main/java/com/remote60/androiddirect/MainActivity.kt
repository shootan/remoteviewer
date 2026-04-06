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
        VIEWER,
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
    private lateinit var listRefreshButton: Button
    private lateinit var viewerBackButton: Button
    private lateinit var viewerOverlayStatusText: TextView
    private lateinit var videoTextureView: TextureView
    private lateinit var targetListAdapter: ArrayAdapter<String>
    private val targetListLabels = mutableListOf<String>()
    private val targetListIds = mutableListOf<Long>()
    private var currentScene = UiScene.CONNECT
    private var activeTargetTab = TargetTab.WINDOWS
    private var connectFlowActive = false
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
        listRefreshButton = findViewById(R.id.listRefreshButton)
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
            val ok = when (activeTargetTab) {
                TargetTab.WINDOWS -> NativeSessionBridge.nativeSelectWindow(targetListIds[position])
                TargetTab.DEVICES -> NativeSessionBridge.nativeSelectDesktopMode()
            }
            if (!ok) {
                renderStatus()
                return@setOnItemClickListener
            }
            currentScene = UiScene.VIEWER
            renderStatus()
        }

        intent.getStringExtra("host")?.trim()?.takeIf { it.isNotEmpty() }?.let(hostEdit::setText)
        intent.getIntExtra("videoPort", 0).takeIf { it > 0 }?.let { videoPortEdit.setText(it.toString()) }
        intent.getIntExtra("controlPort", 0).takeIf { it > 0 }?.let { controlPortEdit.setText(it.toString()) }

        findViewById<Button>(R.id.connectButton).setOnClickListener {
            val host = hostEdit.text?.toString()?.trim().orEmpty()
            val videoPort = videoPortEdit.text?.toString()?.toIntOrNull() ?: 0
            val controlPort = controlPortEdit.text?.toString()?.toIntOrNull() ?: 0
            val ok = NativeSessionBridge.nativeConnect(host, videoPort, controlPort)
            if (ok) {
                connectFlowActive = true
                currentScene = UiScene.TARGETS
                NativeSessionBridge.nativeRequestWindowList()
            }
            renderStatus()
            if (!ok) {
                connectErrorText.text = NativeSessionBridge.nativeGetLastError()
            }
        }

        listDisconnectButton.setOnClickListener {
            NativeSessionBridge.nativeDisconnect()
            connectFlowActive = false
            currentScene = UiScene.CONNECT
            renderStatus()
        }

        listRefreshButton.setOnClickListener {
            NativeSessionBridge.nativeRequestWindowList()
            renderStatus()
        }

        listWindowsButton.setOnClickListener {
            activeTargetTab = TargetTab.WINDOWS
            renderStatus()
        }

        listDevicesButton.setOnClickListener {
            activeTargetTab = TargetTab.DEVICES
            renderStatus()
        }

        viewerBackButton.setOnClickListener {
            currentScene = UiScene.TARGETS
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

    private fun renderStatus() {
        val statusValue = NativeSessionBridge.nativeGetStatus()
        val errorValue = NativeSessionBridge.nativeGetLastError()
        val panelSnapshot = parseWindowPanelSnapshot(NativeSessionBridge.nativeGetWindowPanelJson())
        val isConnecting = statusValue.startsWith("connecting")
        val isConnected = statusValue.startsWith("connected")

        if (!isConnecting && !isConnected && currentScene != UiScene.CONNECT) {
            currentScene = UiScene.CONNECT
        } else if (connectFlowActive && currentScene == UiScene.CONNECT && (isConnecting || isConnected)) {
            currentScene = UiScene.TARGETS
        }

        connectStatusText.text = statusValue
        connectErrorText.text = errorValue
        renderTargetsScene(isConnected, panelSnapshot)
        renderViewerScene(statusValue, panelSnapshot)
        applySceneVisibility()
        syncVideoSurface(forceRebind = false)
    }

    private fun renderTargetsScene(isConnected: Boolean, panelSnapshot: WindowPanelUiSnapshot) {
        listWindowsButton.text =
            if (activeTargetTab == TargetTab.WINDOWS) "[Windows]" else getString(R.string.target_windows_button)
        listDevicesButton.text =
            if (activeTargetTab == TargetTab.DEVICES) "[Devices]" else getString(R.string.target_devices_button)
        listWindowsButton.isEnabled = activeTargetTab != TargetTab.WINDOWS
        listDevicesButton.isEnabled = activeTargetTab != TargetTab.DEVICES
        listRefreshButton.isEnabled = isConnected
        listDisconnectButton.isEnabled = isConnected || connectFlowActive

        listSelectedText.text = "Selected: ${panelSnapshot.selectedTitle}"
        listStatusText.text = panelSnapshot.status

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

            TargetTab.DEVICES -> {
                val prefix = if (panelSnapshot.selectedId == 0L) "* " else ""
                labels.add(prefix + getString(R.string.desktop_mode_button) + " • full desktop capture")
                ids.add(0L)
                targetListEmptyText.text = ""
            }
        }

        targetListLabels.clear()
        targetListLabels.addAll(labels)
        targetListIds.clear()
        targetListIds.addAll(ids)
        targetListAdapter.notifyDataSetChanged()
    }

    private fun renderViewerScene(statusValue: String, panelSnapshot: WindowPanelUiSnapshot) {
        viewerOverlayStatusText.text = panelSnapshot.selectedTitle + " • " + statusValue
    }

    private fun applySceneVisibility() {
        connectScene.visibility = if (currentScene == UiScene.CONNECT) View.VISIBLE else View.GONE
        targetsScene.visibility = if (currentScene == UiScene.TARGETS) View.VISIBLE else View.GONE
        viewerScene.visibility = if (currentScene == UiScene.VIEWER) View.VISIBLE else View.GONE
        if (currentScene != UiScene.VIEWER) {
            releaseVideoSurface()
        }
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

    private fun syncVideoSurface(forceRebind: Boolean) {
        if (currentScene != UiScene.VIEWER) {
            releaseVideoSurface()
            return
        }

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
                items = items,
            )
        } catch (_: JSONException) {
            WindowPanelUiSnapshot.EMPTY
        }
    }
}
