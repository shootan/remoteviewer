package com.remote60.androiddirect

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Button
import android.widget.EditText
import android.widget.TextView

class MainActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var hostEdit: EditText
    private lateinit var videoPortEdit: EditText
    private lateinit var controlPortEdit: EditText
    private lateinit var statusText: TextView
    private lateinit var errorText: TextView
    private lateinit var videoSurfaceView: SurfaceView
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
        videoSurfaceView = findViewById(R.id.videoSurfaceView)
        videoSurfaceView.holder.addCallback(this)

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
    }

    override fun onPause() {
        statusHandler.removeCallbacks(statusPollRunnable)
        super.onPause()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        NativeSessionBridge.nativeSetSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        NativeSessionBridge.nativeSetSurface(holder.surface)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        NativeSessionBridge.nativeSetSurface(null)
    }

    private fun renderStatus() {
        statusText.text = NativeSessionBridge.nativeGetStatus()
        errorText.text = NativeSessionBridge.nativeGetLastError()
    }
}
