package com.remote60.androiddirect

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/**
 * keepalive r2 (Codex codex_keepalive_r2.md section 1): the PCs this phone used most recently, per account -- the
 * ones it keeps ready. At most [MAX] per (directory origin, account), most recent first.
 *
 *  - The key is the directory origin + the account + the hostId. Not a token, an address or a display name: a renamed
 *    PC or a new IP is the same PC, and nothing secret is stored.
 *  - Only a user's SUCCESSFUL use reorders it ([recordUse]: a connection that came up and was shown, or a reuse of a
 *    kept PC). An automatic preconnect, a heartbeat, a list refresh or a failed tap never does -- so a failed attempt
 *    at a fourth PC does not push one of the three out.
 *  - Persisted (SharedPreferences, one small JSON document), so the three survive an app restart; the pure core
 *    ([Book]) is what the unit tests exercise.
 *
 * The account is the directory's account key as the session knows it (DirectoryClient.sessionAccount(): the id the
 * account signs in with -- the server keys its sessions by it). The login answer carries no separate opaque account
 * id; if one is added later, this key should move to it (recorded for the verifier).
 */
object RecentHosts {
    const val MAX = 3

    data class Key(val directoryOrigin: String, val accountId: String, val hostId: String)

    /** The pure book: per (origin, account), hostIds most recent first. */
    class Book(private val lists: MutableMap<Pair<String, String>, MutableList<String>> = LinkedHashMap()) {
        fun recent(directoryOrigin: String, accountId: String): List<String> =
            lists[directoryOrigin to accountId]?.toList() ?: emptyList()

        /** A successful use by the user: to the front; the oldest beyond [MAX] drops off. true if anything changed. */
        fun recordUse(key: Key): Boolean {
            if (key.directoryOrigin.isEmpty() || key.accountId.isEmpty() || key.hostId.isEmpty()) return false
            val list = lists.getOrPut(key.directoryOrigin to key.accountId) { mutableListOf() }
            if (list.firstOrNull() == key.hostId) return false
            list.remove(key.hostId)
            list.add(0, key.hostId)
            while (list.size > MAX) list.removeAt(list.size - 1)
            return true
        }

        fun toJson(): String {
            val arr = JSONArray()
            for ((k, v) in lists) {
                arr.put(JSONObject().put("origin", k.first).put("account", k.second).put("hosts", JSONArray(v)))
            }
            return JSONObject().put("v", 1).put("lists", arr).toString()
        }

        companion object {
            /** A document from an older or broken write is an empty book, not a crash. */
            fun fromJson(text: String?): Book {
                val book = Book()
                if (text.isNullOrEmpty()) return book
                try {
                    val root = JSONObject(text)
                    if (root.optInt("v") != 1) return book
                    val arr = root.optJSONArray("lists") ?: return book
                    for (i in 0 until arr.length()) {
                        val o = arr.optJSONObject(i) ?: continue
                        val origin = o.optString("origin")
                        val account = o.optString("account")
                        val hosts = o.optJSONArray("hosts") ?: continue
                        if (origin.isEmpty() || account.isEmpty()) continue
                        val list = mutableListOf<String>()
                        for (j in 0 until hosts.length()) {
                            val h = hosts.optString(j)
                            if (h.isNotEmpty() && h !in list && list.size < MAX) list.add(h)
                        }
                        book.lists[origin to account] = list
                    }
                } catch (_: Exception) {
                    return Book()
                }
                return book
            }
        }
    }

    /**
     * "이 PC 연결 해제" means that PC is not kept automatically again -- for as long as the app / sign-in lasts. The next
     * list refresh or online event must not reconnect it behind the user's back. Released by the user's own explicit
     * connect to it; everything is released by a sign-out (a new sign-in starts clean). Not persisted.
     */
    class Suppression {
        private val keys = HashSet<Key>()
        @Synchronized fun suppress(key: Key) { keys.add(key) }
        @Synchronized fun release(key: Key) { keys.remove(key) }
        @Synchronized fun isSuppressed(key: Key): Boolean = key in keys
        @Synchronized fun clear() { keys.clear() }
        @Synchronized fun size(): Int = keys.size
    }

    private const val PREFS = "gnlink_recent_hosts"
    private const val DOC = "v1"

    fun load(context: Context): Book =
        Book.fromJson(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(DOC, null))

    fun save(context: Context, book: Book) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(DOC, book.toJson()).apply()
    }
}
