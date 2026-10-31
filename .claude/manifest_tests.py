import io

# ---------------------------------------------------------------- C++ (host + PC client rule)
p = 'apps/native_poc/src/directory_session_client_test.cpp'
d = io.open(p, encoding='utf-8').read()
anchor = '''  // --------------------------------------------------------------- what the parser now accepts'''
added = '''  // ------------------------------------------------- where updates come from, with one address
  //
  // The manifest url used to come only from an environment variable, so a machine configured with
  // nothing but a server address could never check for updates: the address was there, the route
  // was there, and nothing joined them. It is derived from the directory origin now -- which
  // means the derivation must not invent a server, and must not move a deployment that set the
  // variable.
  {
    using directory::directory_update_manifest_url;
    using directory::update_manifest_url_for;

    check("an https directory names its own update route",
          directory_update_manifest_url("https://rem.example", "windows") ==
              "https://rem.example:443/api/update/manifest?platform=windows",
          directory_update_manifest_url("https://rem.example", "windows"));
    check("...on whatever port it is really on",
          directory_update_manifest_url("https://rem.example:8443", "windows") ==
              "https://rem.example:8443/api/update/manifest?platform=windows",
          directory_update_manifest_url("https://rem.example:8443", "windows"));
    check("...and the platform is the one asked for",
          directory_update_manifest_url("https://rem.example", "android") ==
              "https://rem.example:443/api/update/manifest?platform=android",
          directory_update_manifest_url("https://rem.example", "android"));

    // Built from the origin, so the spellings that mean one server produce one url. A string
    // paste would have carried the path and the trailing slash into the middle of this.
    check("a trailing slash does not end up inside the url",
          directory_update_manifest_url("https://rem.example/", "windows") ==
              directory_update_manifest_url("https://rem.example", "windows"),
          directory_update_manifest_url("https://rem.example/", "windows"));
    check("neither does a path",
          directory_update_manifest_url("https://REM.example/api/", "windows") ==
              directory_update_manifest_url("https://rem.example", "windows"),
          directory_update_manifest_url("https://REM.example/api/", "windows"));

    // The two refusals. Guessing https would invent a server nobody configured; following the
    // directory down to http would fetch an administrator-privileged artifact over a hop anyone
    // can rewrite.
    check("an http directory derives nothing",
          directory_update_manifest_url("http://rem.example", "windows").empty(),
          directory_update_manifest_url("http://rem.example", "windows"));
    check("...and is not quietly upgraded to https",
          directory_update_manifest_url("http://rem.example", "windows").find("https") ==
              std::string::npos);
    check("no directory means nothing to derive from",
          directory_update_manifest_url("", "windows").empty());

    // The override keeps working, including for the http deployment that has to use one.
    check("an explicit override wins over the derivation",
          update_manifest_url_for("https://updates.example/m?platform=windows",
                                  "https://rem.example", "windows") ==
              "https://updates.example/m?platform=windows");
    check("...and is the only answer an http directory has",
          update_manifest_url_for("https://updates.example/m", "http://rem.example", "windows") ==
              "https://updates.example/m");
    check("without either, there is no url and that is not an error",
          update_manifest_url_for("", "http://rem.example", "windows").empty());
    check("with only an address, there is one",
          update_manifest_url_for("", "https://rem.example", "windows") ==
              "https://rem.example:443/api/update/manifest?platform=windows");
  }

''' + anchor
assert d.count(anchor) == 1, 'cpp anchor'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok cpp test')

# ---------------------------------------------------------------- Kotlin
p = 'apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/DirectoryObserveTest.kt'
d = io.open(p, encoding='utf-8').read()
anchor = '''    // ---------------------------------------------------------------- the url, normalised once'''
added = '''    // ---------------------------------------------------------------- where updates come from

    @Test
    fun `an https directory names its own update route`() {
        assertEquals(
            "https://rem.example:443/api/update/manifest?platform=android",
            DirectoryClient.updateManifestUrlFor("https://rem.example")
        )
        assertEquals(
            "https://rem.example:8443/api/update/manifest?platform=android",
            DirectoryClient.updateManifestUrlFor("https://rem.example:8443")
        )
        // Built from the origin, so a trailing slash or a path cannot end up inside the url.
        assertEquals(
            DirectoryClient.updateManifestUrlFor("https://rem.example"),
            DirectoryClient.updateManifestUrlFor("https://REM.example/api/")
        )
    }

    @Test
    fun `an http directory derives nothing and is not upgraded`() {
        assertEquals("", DirectoryClient.updateManifestUrlFor("http://rem.example"))
        assertEquals("", DirectoryClient.updateManifestUrlFor("rem.example"))
        assertEquals("", DirectoryClient.updateManifestUrlFor(""))
    }

    @Test
    fun `the build constant wins, and one address is enough without it`() {
        assertEquals(
            "https://updates.example/m",
            DirectoryClient.updateManifestUrl("https://updates.example/m", "https://rem.example")
        )
        assertEquals(
            "https://rem.example:443/api/update/manifest?platform=android",
            DirectoryClient.updateManifestUrl("", "https://rem.example")
        )
        // An http directory with no override has nowhere safe to fetch an installable APK from.
        assertEquals("", DirectoryClient.updateManifestUrl("", "http://rem.example"))
    }

    @Test
    fun `the manifest and its signature come out of one response`() {
        val body = """{"manifest":"schema=2\\nversion=0.2.105\\n","signature":"abcd"}"""
        assertEquals("schema=2\\nversion=0.2.105\\n", UpdateFlow.jsonStringField(body, "manifest"))
        assertEquals("abcd", UpdateFlow.jsonStringField(body, "signature"))
        assertEquals(null, UpdateFlow.jsonStringField(body, "nothing"))
        // A truncated response is not a field.
        assertEquals(null, UpdateFlow.jsonStringField("""{"manifest":"schema=2""", "manifest"))
    }

''' + anchor
assert d.count(anchor) == 1, 'kt anchor'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok kotlin test')
