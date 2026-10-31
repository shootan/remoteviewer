import io

p = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'
d = io.open(p, encoding='utf-8').read()

# --- start the check where the app already logs its start
old = '''        findViewById<Button>(R.id.connectButton).setOnClickListener { beginConnect("connect_tap") }'''
new = '''        startUpdateCheck()

        findViewById<Button>(R.id.connectButton).setOnClickListener { beginConnect("connect_tap") }'''
assert old in d, 'onCreate anchor'
d = d.replace(old, new, 1)

# --- the flow itself, placed before onDestroy
old = '''    override fun onDestroy() {
        saveCurrentEndpoint()'''
new = '''    // ---------------------------------------------------------------- updates

    private var updateStatusReceiver: UpdateInstaller.StatusReceiver? = null
    private var pendingUpdateArtifact: UpdateManifest.Artifact? = null

    /**
     * Asks whether there is a newer build, on a thread nothing waits for.
     *
     * Not waiting is the requirement rather than an optimisation. This runs on a phone that is
     * often somewhere with no route to the server, and an app that will not show its sign-in
     * screen until an update server answers is an app that cannot be used on a train.
     *
     * Only "there is a newer version" reaches the user. A start-up check that could not reach the
     * server is the ordinary case, and a dialog for it teaches people to dismiss dialogs --
     * including the next one, which matters. Everything else goes to the diagnostics log.
     */
    private fun startUpdateCheck() {
        val url = updateManifestUrl()
        val key = trustedUpdateKeyHex()
        UpdateFlow.checkAsync(url, key, installedVersionCode()) { outcome ->
            diagnosticsLog.log("update_check",
                "verdict=${outcome.verdict} version=${outcome.version} ${outcome.detail}")
            if (outcome.verdict == UpdateDecision.Verdict.Install && outcome.artifact != null) {
                pendingUpdateArtifact = outcome.artifact
                offerUpdate(outcome.version)
            }
        }
    }

    private fun offerUpdate(version: String) {
        if (isFinishing || isDestroyed) return
        AlertDialog.Builder(this)
            .setTitle("업데이트")
            .setMessage("새 버전 $version 이 있습니다. 지금 설치하시겠습니까?")
            .setPositiveButton("설치") { _, _ -> beginUpdateDownload() }
            // Later is an answer. Nothing happens, and it is not asked again this run.
            .setNegativeButton("나중에") { _, _ ->
                diagnosticsLog.log("update_check", "the user chose later")
            }
            .show()
    }

    private fun beginUpdateDownload() {
        val artifact = pendingUpdateArtifact ?: return
        // Asking before downloading: without the permission the download would be wasted, and the
        // user would have spent it before finding out they had to grant something.
        if (!UpdateInstaller.canInstallPackages(this)) {
            diagnosticsLog.log("update_install", "asking for permission to install packages")
            try {
                startActivity(UpdateInstaller.manageUnknownSourcesIntent(this))
            } catch (_: Exception) {
                Toast.makeText(this, "설치 권한 화면을 열지 못했습니다.", Toast.LENGTH_SHORT).show()
            }
            // Deliberately not retried automatically. The user may grant it and tap again, or not
            // -- refusing is an answer, and chasing them for it is not.
            return
        }

        Toast.makeText(this, "업데이트를 내려받는 중입니다.", Toast.LENGTH_SHORT).show()
        Thread {
            val file = UpdateFlow.download(this, artifact)
            runOnUiThread {
                if (file == null) {
                    diagnosticsLog.log("update_install", "download failed or did not verify")
                    Toast.makeText(this, "업데이트 파일을 확인하지 못했습니다.", Toast.LENGTH_LONG).show()
                    return@runOnUiThread
                }
                registerUpdateStatusReceiver()
                if (!UpdateInstaller.install(this, file)) {
                    diagnosticsLog.log("update_install", "the install session could not be opened")
                    Toast.makeText(this, "설치를 시작하지 못했습니다.", Toast.LENGTH_LONG).show()
                }
            }
        }.apply { isDaemon = true; name = "gnlink-update-download"; start() }
    }

    private fun registerUpdateStatusReceiver() {
        if (updateStatusReceiver != null) return
        val receiver = UpdateInstaller.StatusReceiver(
            onOutcome = { outcome, message ->
                diagnosticsLog.log("update_install", "outcome=$outcome $message")
                // A refusal is not an error. The user still has a working app, which is exactly
                // why they were free to say no -- telling them it failed would be arguing with a
                // decision they just made.
                if (!UpdateDecision.isUserDecision(outcome) &&
                    outcome != UpdateDecision.InstallOutcome.Installed) {
                    Toast.makeText(this, "설치하지 못했습니다.", Toast.LENGTH_LONG).show()
                }
            },
            onUserActionRequired = { confirm ->
                // The system asking for its own confirmation screen. Not an outcome -- reporting
                // it as one would announce a failure before the user had been asked anything.
                try {
                    startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                } catch (_: Exception) {
                    diagnosticsLog.log("update_install", "could not show the confirmation screen")
                }
            })
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, receiver.filter(), Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(receiver, receiver.filter())
        }
        updateStatusReceiver = receiver
    }

    private fun installedVersionCode(): Long = try {
        val info = packageManager.getPackageInfo(packageName, 0)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            info.longVersionCode
        } else {
            @Suppress("DEPRECATION")
            info.versionCode.toLong()
        }
    } catch (_: Exception) {
        0L
    }

    /** Empty until a release endpoint exists, which reports NotConfigured rather than failing. */
    private fun updateManifestUrl(): String = BuildConfig.UPDATE_MANIFEST_URL

    /** Empty until an operational key exists. Empty means this build cannot check, not that it failed. */
    private fun trustedUpdateKeyHex(): String = BuildConfig.UPDATE_PUBLIC_KEY_HEX

    override fun onDestroy() {
        updateStatusReceiver?.let {
            try { unregisterReceiver(it) } catch (_: Exception) { }
        }
        updateStatusReceiver = null
        saveCurrentEndpoint()'''
assert old in d, 'onDestroy anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('MainActivity wired')
