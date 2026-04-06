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
import android.widget.Button
import android.widget.EditText
import android.widget.TextView

class MainActivity : Activity(), TextureView.SurfaceTextureListener {
    companion object {
        private const val LOG_TAG = "remote60_android_direct"
    }

    private lateinit var hostEdit: EditText
    private lateinit var videoPortEdit: EditText
    private lateinit var controlPortEdit: EditText
    private lateinit var statusText: TextView
    private lateinit var errorText: TextView
    private lateinit var videoDebugText: TextView
    private lateinit var videoTextureView: TextureView
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
        videoTextureView.surfaceTextureListener = this
        videoTextureView.isOpaque = true
        videoTextureView.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            syncVideoSurface(forceRebind = false)
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
        statusText.text = NativeSessionBridge.nativeGetStatus()
        errorText.text = NativeSessionBridge.nativeGetLastError()
        syncVideoSurface(forceRebind = false)
        videoDebugText.text =
            NativeSessionBridge.nativeGetVideoDebugStatus() +
                " view=${videoTextureView.width}x${videoTextureView.height}" +
                " buffer=${surfaceBufferWidth}x${surfaceBufferHeight}" +
                " video=${videoWidth}x${videoHeight}"
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
