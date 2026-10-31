import io

# ---------------------------------------------------------------- one source of truth
p = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/UpdateManifest.kt'
d = io.open(p, encoding='utf-8').read()
old = '''    /**
     * The key this build trusts, as raw X||Y hex.
     *
     * Empty, exactly as on the Windows side. No release key is compiled in, so nothing verifies
     * until one is deliberately put here -- and a placeholder that happened to verify something
     * would be worse than no key at all. Choosing and storing a real key is a separate approved
     * decision (design 4.5).
     */
    @JvmStatic
    fun trustedPublicKeyHex(): String = ""'''
new = '''    /**
     * The key this build trusts, as raw X||Y hex.
     *
     * Delegates to BuildConfig rather than holding a second copy. It used to return a hard-coded
     * "" while the value the app actually verifies with came from BuildConfig -- two answers to
     * one question, and the one the tests asked was not the one the product used. Embedding the
     * release key made them disagree, which is how that was noticed.
     *
     * Empty is still a legitimate answer: a build made without the key verifies nothing, which is
     * the fail-closed direction.
     */
    @JvmStatic
    fun trustedPublicKeyHex(): String = BuildConfig.UPDATE_PUBLIC_KEY_HEX'''
assert d.count(old) == 1, 'accessor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok main')

# ---------------------------------------------------------------- the assertion follows the key
p = 'apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/UpdateManifestTest.kt'
d = io.open(p, encoding='utf-8').read()
old2 = '''    /**
     * No release key is compiled in, and the Android side must be fail-closed exactly as Windows
     * is. A placeholder that happened to verify something would be worse than no key at all, so
     * this fails loudly if one appears.
     */
    @Test
    fun noReleaseKeyIsCompiledIn() {
        assertEquals("", UpdateManifest.trustedPublicKeyHex())
        assertFalse("the empty trusted key must verify nothing",
            UpdateManifest.verifySignature(document, signatureHex,
                UpdateManifest.trustedPublicKeyHex()))
    }'''
new2 = '''    /**
     * The release key is compiled in now, and the same safety property is asserted from the other
     * side: the shipped key must refuse a document signed by any other key. The shared vectors are
     * signed by exactly such a key, so they are the right thing to hand it.
     *
     * This used to assert the key was empty. That was a statement about a temporary state, and
     * keeping it would now mean asserting the app cannot check updates at all.
     *
     * The two encodings are easy to swap and the mistake is silent: the SPKI form decodes and then
     * fails a length check inside the verifier, so every update is refused with no error anywhere.
     */
    @Test
    fun theShippedKeyIsRawXyAndRefusesOtherKeys() {
        val key = UpdateManifest.trustedPublicKeyHex()
        assertTrue("a release key is compiled in", key.isNotEmpty())
        assertEquals("raw X||Y is 64 bytes", 128, key.length)
        assertTrue("lower-case hex and nothing else", key.all { it in "0123456789abcdef" })
        // 182 would be the 91-byte SPKI DER form -- the value that silently disables every check.
        assertTrue("not the SPKI form", key.length != 182)

        assertFalse("the shipped key must not verify a document signed by another key",
            UpdateManifest.verifySignature(document, signatureHex, key))
    }'''
assert d.count(old2) == 1, 'assertion'
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok test')
