import io

p = 'apps/native_poc/src/update_manifest_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // ---------------------------------------------------------------- the shipped key rejects all

  {
    // No release key is compiled in, on purpose. Until one is deliberately put there, the default
    // verifier must accept nothing -- including a signature that is genuinely valid under the
    // test key. This is the safe direction and it should fail loudly if someone drops in a
    // placeholder that happens to verify.
    const ManifestResult r = load_manifest(document, sigHex, "windows", default_verifier());
    check("default verifier rejects while no release key is compiled in",
          r.status == ManifestStatus::SignatureInvalid, status_name(r.status));
    check("trusted key is empty in this build",
          std::string(trusted_public_key_hex()).empty(), trusted_public_key_hex());
  }'''

new = '''  // ---------------------------------------------------------------- the shipped key

  {
    // The release key is compiled in now. What this asserts is the same safety property as
    // before, from the other side: the shipped verifier must refuse a document signed by any
    // OTHER key -- and the shared test vectors are signed by exactly such a key, so they are the
    // right thing to hand it.
    //
    // Before the key existed, this block asserted the key was empty. That assertion was about a
    // temporary state, and keeping it would now mean asserting the product cannot check updates.
    const ManifestResult r = load_manifest(document, sigHex, "windows", default_verifier());
    check("the shipped verifier rejects a document signed by another key",
          r.status == ManifestStatus::SignatureInvalid, status_name(r.status));

    // The two encodings of the same key are easy to swap and the mistake is silent: the SPKI form
    // decodes fine and then fails the length check inside the verifier, so every update is
    // refused with no error anywhere.
    const std::string key = trusted_public_key_hex();
    check("a release key is compiled in", !key.empty());
    check("...as raw X||Y, which is 64 bytes", key.size() == 128, std::to_string(key.size()));
    check("...in lower-case hex and nothing else",
          key.find_first_not_of("0123456789abcdef") == std::string::npos, key);
    // The SPKI DER form of a P-256 key is 91 bytes: this is the length that would silently
    // disable every check if it were pasted here instead.
    check("...and not the 91-byte SPKI form", key.size() != 182, std::to_string(key.size()));
  }'''
assert d.count(old) == 1, 'block'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
