package com.remote60.androiddirect

import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {
    private lateinit var hostEdit: EditText
    private lateinit var videoPortEdit: EditText
    private lateinit var controlPortEdit: EditText
    private lateinit var statusText: TextView
    private lateinit var errorText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        hostEdit = findViewById(R.id.hostInput)
        videoPortEdit = findViewById(R.id.videoPortInput)
        controlPortEdit = findViewById(R.id.controlPortInput)
        statusText = findViewById(R.id.statusText)
        errorText = findViewById(R.id.errorText)

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

    private fun renderStatus() {
        statusText.text = NativeSessionBridge.nativeGetStatus()
        if (errorText.text.isNullOrBlank()) {
            errorText.text = NativeSessionBridge.nativeGetLastError()
        }
    }
}
