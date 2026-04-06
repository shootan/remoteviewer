package com.remote60.androiddirect

import android.app.Activity
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Surface
import android.view.TextureView
import android.widget.Button
import android.widget.EditText
import android.widget.TextView

class MainActivity : Activity(), TextureView.SurfaceTextureListener {
    private lateinit var hostEdit: EditText
    private lateinit var videoPortEdit: EditText
    private lateinit var controlPortEdit: EditText
    private lateinit var statusText: TextView
    private lateinit var errorText: TextView
    private lateinit var videoDebugText: TextView
    private lateinit var videoTextureView: TextureView
    private var videoSurface: Surface? = null

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
        if (videoTextureView.isAvailable) {
            bindVideoSurface(videoTextureView.surfaceTexture)
        }
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
        bindVideoSurface(surface)
    }

    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
        bindVideoSurface(surface)
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
            NativeSessionBridge.nativeSetSurface(null)
            return
        }
        val surface = Surface(surfaceTexture)
        videoSurface = surface
        NativeSessionBridge.nativeSetSurface(surface)
    }

    private fun releaseVideoSurface() {
        NativeSessionBridge.nativeSetSurface(null)
        videoSurface?.release()
        videoSurface = null
    }

    private fun renderStatus() {
        statusText.text = NativeSessionBridge.nativeGetStatus()
        errorText.text = NativeSessionBridge.nativeGetLastError()
        videoDebugText.text = NativeSessionBridge.nativeGetVideoDebugStatus()
    }
}
