p = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/DirectoryClient.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
reps = [
("""        val candidates: List<String>,
""", """        val candidates: List<String>,
        /**
         * keepalive-r2 ② e: only for a standby connect (connect(standby = true)) -- "reserved" (the relay holds a
         * host-facing socket for THIS connect's lease: aim at the "relay" candidate), a refusal reason, or "" (an
         * ordinary connect, or a directory that predates it).
         */
        val standbyRelay: String = "",
"""),
("""    fun hosts(url: String, sessionToken: String): List<Host> {
        val response = get(url, "/api/hosts", sessionToken)
        val array = response.optJSONArray("hosts") ?: return emptyList()
""", """    /**
     * keepalive-r2 ② e: whether this directory knows standby connects (and runs a relay), read from /api/hosts --
     * without side effects (a client must not learn it from a first mutating /api/connect). An older directory has no
     * such object: unsupported.
     */
    data class StandbySupport(val supported: Boolean, val relay: Boolean) {
        companion object {
            val NONE = StandbySupport(false, false)
            fun of(response: JSONObject): StandbySupport {
                val o = response.optJSONObject("standby") ?: return NONE
                return StandbySupport(o.optBoolean("supported", false), o.optBoolean("relay", false))
            }
        }
    }

    data class HostList(val hosts: List<Host>, val standby: StandbySupport)

    fun hosts(url: String, sessionToken: String): List<Host> = hostList(url, sessionToken).hosts

    fun hostList(url: String, sessionToken: String): HostList {
        val response = get(url, "/api/hosts", sessionToken)
        return HostList(parseHosts(response), StandbySupport.of(response))
    }

    private fun parseHosts(response: JSONObject): List<Host> {
        val array = response.optJSONArray("hosts") ?: return emptyList()
"""),
("""    fun connect(url: String, sessionToken: String, hostId: String, observeToken: String): ConnectTarget {
        val body = JSONObject().put("hostId", hostId).put("observeToken", observeToken)
        val response = post(url, "/api/connect", body, sessionToken)
""", """    fun connect(url: String, sessionToken: String, hostId: String, observeToken: String,
                standby: Boolean = false): ConnectTarget {
        val body = JSONObject().put("hostId", hostId).put("observeToken", observeToken)
        if (standby) body.put("standby", true)  // keepalive-r2 ② e: a standby lease's capability (additive)
        val response = post(url, "/api/connect", body, sessionToken)
        return parseConnect(response)
    }

    internal fun parseConnect(response: JSONObject): ConnectTarget {
"""),
("""        if (candidates.isEmpty()) candidates.add("$ip:$port|public")
        return ConnectTarget(ip, port, punchToken, candidates)
    }
""", """        if (candidates.isEmpty()) candidates.add("$ip:$port|public")
        return ConnectTarget(ip, port, punchToken, candidates, response.optString("standbyRelay", ""))
    }

    /**
     * keepalive-r2 ② e: where a standby lease's datagrams go for this capability -- the same choice as the native
     * standby_directory_capability: the relay's socket when this connect reserved one, else the host itself (the first
     * private candidate -- the LAN -- else the first other non-relay one). null = nowhere.
     */
    fun standbyAim(target: ConnectTarget): Pair<String, Int>? {
        fun parse(c: String): Triple<String, Int, String>? {
            val bar = c.lastIndexOf('|')
            val addr = if (bar >= 0) c.substring(0, bar) else c
            val kind = if (bar >= 0) c.substring(bar + 1) else ""
            val colon = addr.lastIndexOf(':')
            if (colon <= 0) return null
            val port = addr.substring(colon + 1).toIntOrNull() ?: return null
            if (port <= 0 || port > 65535) return null
            return Triple(addr.substring(0, colon), port, kind)
        }
        val all = target.candidates.mapNotNull(::parse)
        if (target.standbyRelay == "reserved") {
            val relay = all.firstOrNull { it.third == "relay" }
            if (relay != null) return relay.first to relay.second
        }
        val hosts = all.filter { it.third != "relay" }
        val pick = hosts.firstOrNull { it.third == "private" } ?: hosts.firstOrNull() ?: return null
        return pick.first to pick.second
    }
"""),
]
for a, b in reps:
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
