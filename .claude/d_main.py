import io

p = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'
d = io.open(p, encoding='utf-8').read()

def sub(old, new):
    global d
    assert d.count(old) == 1, old[:80]
    d = d.replace(old, new, 1)

sub('''                val (token, expiresAt) = DirectoryClient.login(url, id, password)
                DirectoryClient.saveSession(this, url, id, token, expiresAt)''',
    '''                val result = DirectoryClient.login(url, id, password)
                val token = result.sessionToken
                // Where observations go rides on the login response. Kept for this run so the
                // connect below does not have to ask again; a run that starts from a stored
                // session asks /healthz instead, because it never sees a login response.
                directoryObserveEndpoint = result.advertised
                DirectoryClient.saveSession(this, url, id, token, result.expiresAt)''')

sub('''        val observeToken = DirectoryClient.newObserveToken()
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
                diagnosticsLog.log("directory_target", target.candidates.joinToString(" "))''',
    '''        val observeToken = DirectoryClient.newObserveToken()

        directoryExecutor.execute {
            try {
                // A run that resumed from a stored session never saw a login response, so it asks
                // the health route -- which needs no session and carries the same value.
                if (!directoryObserveEndpoint.known) {
                    directoryObserveEndpoint = DirectoryClient.observeFromHealth(url)
                }
                val advertised = directoryObserveEndpoint
                val directoryHost = DirectoryClient.observeHostFor(url, advertised)
                val observePort = DirectoryClient.observePortFor(url, advertised)
                if (observePort == 0) {
                    // Only reachable on https against a server that has not been told its observe
                    // port. Said plainly: dialling 444 instead would fail as silence, and silence
                    // reads like a broken PC rather than a server that needs one setting.
                    throw DirectoryClient.DirectoryException(
                        "이 디렉터리 서버에 주소 확인 포트가 설정되어 있지 않습니다"
                    )
                }
                if (advertised.hostRejected) {
                    diagnosticsLog.log("directory_observe_host_rejected", "using $directoryHost")
                }

                val observe = {
                    val seen = NativeSessionBridge.nativeDirectoryObserve(
                        directoryHost, observePort, observeToken
                    )
                    if (seen.isEmpty()) {
                        throw DirectoryClient.DirectoryException(
                            NativeSessionBridge.nativeDirectoryLastError().ifEmpty { "주소 확인 실패" }
                        )
                    }
                    diagnosticsLog.log("directory_observed", seen)
                }
                observe()

                // One retry, and only when the directory says it has no observation for us. That
                // is a state this end can repair -- send another from the same socket -- and it is
                // not an authentication failure, so the session stays and nothing signs out. A
                // loop here would spin against a server refusing for a reason the phone cannot
                // fix, with the user waiting on it.
                val target = try {
                    DirectoryClient.connect(url, token, host.hostId, observeToken)
                } catch (e: DirectoryClient.DirectoryException) {
                    if (e.status != 409 || !e.serverError.startsWith("observation")) throw e
                    diagnosticsLog.log("directory_reobserve", e.serverError)
                    observe()
                    DirectoryClient.connect(url, token, host.hostId, observeToken)
                }
                diagnosticsLog.log("directory_target", target.candidates.joinToString(" "))''')

sub('''    private var directoryBusy = false''',
    '''    private var directoryBusy = false

    /** What the directory advertised at login, if anything. Absence has its own rule. */
    private var directoryObserveEndpoint = DirectoryClient.ObserveEndpoint()''')

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok main')
