import io

A = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/'

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit(A + 'DirectoryClient.kt', [
 ('''    /**
     * Asks /healthz where observations go, for the path that never logs in.''',
  '''    /** Where the probe should go, or why it cannot go anywhere. */
    sealed class ObserveTarget {
        data class Ready(
            val host: String,
            val port: Int,
            val advertised: ObserveEndpoint,
        ) : ObserveTarget()

        data class Refused(val message: String) : ObserveTarget()
    }

    /**
     * Everything between "the user tapped a PC" and "send the probe here".
     *
     * A function rather than a stretch of the activity, because this is where the mistakes are:
     * three states arrive here -- already known, learned from the health route, and not learned
     * at all -- and two of them used to end up looking identical. [probe] is a parameter so the
     * decision can be exercised without a server, which is the only way the not-learned-at-all
     * branches get tested at all.
     *
     * Unreachable and silent are answered differently on purpose. On https, silent means "this
     * server needs one setting"; saying that about a server that is simply down sends whoever
     * reads it to configure a machine that is not the problem. On http neither matters -- the
     * documented default still applies -- so a failed probe there is not fatal.
     */
    fun resolveObserveTarget(
        url: String,
        cached: ObserveEndpoint = ObserveEndpoint(),
        probe: (String) -> HealthObserve = ::observeFromHealth,
    ): ObserveTarget {
        var advertised = cached
        if (!advertised.known) {
            val health = probe(url)
            if (!health.reached && urlIsSecure(url)) {
                return ObserveTarget.Refused(
                    "디렉터리 서버에 연결할 수 없습니다" +
                        if (health.error.isEmpty()) "" else " (${health.error})"
                )
            }
            advertised = health.advertised
        }
        val port = observePortFor(url, advertised)
        if (port == 0) {
            return ObserveTarget.Refused("이 디렉터리 서버에 주소 확인 포트가 설정되어 있지 않습니다")
        }
        return ObserveTarget.Ready(observeHostFor(url, advertised), port, advertised)
    }

    /**
     * Asks /healthz where observations go, for the path that never logs in.'''),
])

edit(A + 'MainActivity.kt', [
 ('''                if (!directoryObserveEndpoint.known) {
                    val health = DirectoryClient.observeFromHealth(url)
                    // A server that could not be reached and a server that said nothing are not
                    // the same thing. On https the second one means "this server needs a setting",
                    // and reporting that about a server that is simply down would send whoever
                    // reads it to the wrong place entirely.
                    if (!health.reached && DirectoryClient.urlIsSecure(url)) {
                        throw DirectoryClient.DirectoryException(
                            "디렉터리 서버에 연결할 수 없습니다" +
                                if (health.error.isEmpty()) "" else " (${health.error})"
                        )
                    }
                    directoryObserveEndpoint = health.advertised
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
                }''',
  '''                // The decision lives in DirectoryClient, where it can be exercised without a
                // server. Here it is only applied.
                val target = DirectoryClient.resolveObserveTarget(url, directoryObserveEndpoint)
                if (target is DirectoryClient.ObserveTarget.Refused) {
                    throw DirectoryClient.DirectoryException(target.message)
                }
                val ready = target as DirectoryClient.ObserveTarget.Ready
                directoryObserveEndpoint = ready.advertised
                val directoryHost = ready.host
                val observePort = ready.port
                if (ready.advertised.hostRejected) {
                    diagnosticsLog.log("directory_observe_host_rejected", "using $directoryHost")
                }'''),
 ('''                val target = try {
                    DirectoryClient.connect(url, token, host.hostId, observeToken)''',
  '''                val connected = try {
                    DirectoryClient.connect(url, token, host.hostId, observeToken)'''),
 ('''                    observe()
                    DirectoryClient.connect(url, token, host.hostId, observeToken)
                }
                diagnosticsLog.log("directory_target", target.candidates.joinToString(" "))''',
  '''                    observe()
                    DirectoryClient.connect(url, token, host.hostId, observeToken)
                }
                diagnosticsLog.log("directory_target", connected.candidates.joinToString(" "))'''),
])

# The remaining uses of the old `target` name in that block belong to the connect result.
p = A + 'MainActivity.kt'
d = io.open(p, encoding='utf-8').read()
for old, new in [
    ('                    target.candidates.toTypedArray(), 4000, target.punchToken',
     '                    connected.candidates.toTypedArray(), 4000, connected.punchToken'),
]:
    assert d.count(old) == 1, old[:50]
    d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok main names')
