import io

p = 'apps/native_poc/src/update_manifest_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = """  // ---------------------------------------------------------------- the shipped key rejects all"""
new = '''  // ---------------------------------------------------------------- the lock on the door

  // The payload-name rules exist to stop a name from becoming a path outside the install
  // directory. Until now nothing connected them to a manifest, so they were a good lock not yet
  // fitted to a door. These cases are that connection: an unsafe name in a SIGNED manifest must
  // still be refused, and no VerifiedManifest carrying one can exist.
  {
    const std::string base =
        "schema=1\\nplatform=windows\\nversion=1\\nartifact=a\\nsize=1\\n"
        "sha256=0000000000000000000000000000000000000000000000000000000000000000\\n";

    struct Case {
      const char* name;
      const char* payloadLines;
      ManifestStatus expect;
    };
    const Case cases[] = {
        {"no payload lines is fine", "", ManifestStatus::Ok},
        {"ordinary names are accepted",
         "payload=GNLinkHost.exe\\npayload=ui\\\\shell.html\\npayload=GNLinkSetup.exe\\n",
         ManifestStatus::Ok},
        // Each of these is signed by the accepting verifier, so the ONLY thing rejecting them is
        // the name check.
        {"a traversal in a signed manifest is refused", "payload=..\\\\evil.exe\\n",
         ManifestStatus::Malformed},
        {"a mixed-separator traversal is refused", "payload=ui/../../evil.exe\\n",
         ManifestStatus::Malformed},
        {"an absolute path is refused", "payload=C:\\\\Windows\\\\System32\\\\evil.dll\\n",
         ManifestStatus::Malformed},
        {"a UNC path is refused", "payload=\\\\\\\\server\\\\share\\\\evil.exe\\n",
         ManifestStatus::Malformed},
        {"an alternate data stream is refused", "payload=GNLinkHost.exe:hidden\\n",
         ManifestStatus::Malformed},
        {"a reserved device name is refused", "payload=NUL\\n", ManifestStatus::Malformed},
        {"a trailing dot is refused", "payload=GNLinkHost.exe.\\n", ManifestStatus::Malformed},
        {"a duplicate is refused",
         "payload=GNLinkHost.exe\\npayload=gnlinkhost.EXE\\n", ManifestStatus::Malformed},
        {"a non-ASCII name is refused", "payload=\\xed\\x95\\x9c.exe\\n", ManifestStatus::Malformed},
    };

    for (const Case& c : cases) {
      const std::string doc = base + c.payloadLines;
      const ManifestResult r = load_manifest(doc, sigHex, "windows", acceptingVerifier);
      check(std::string("payload: ") + c.name, r.status == c.expect,
            std::string("expected ") + status_name(c.expect) + " got " + status_name(r.status) +
                " " + r.detail);
      if (c.expect != ManifestStatus::Ok) {
        // The point of the connection: nothing carrying an unsafe name can exist.
        check(std::string("payload: ") + c.name + " -> no VerifiedManifest", !r.manifest.has_value());
      }
    }
  }

  {
    // And the names that DO come through are the ones that were written, in order -- the swap
    // moves files aside in this order, so a reordering would change which backup a rollback used.
    const std::string doc =
        "schema=1\\nplatform=windows\\nversion=1\\nartifact=a\\nsize=1\\n"
        "sha256=0000000000000000000000000000000000000000000000000000000000000000\\n"
        "payload=GNLinkHost.exe\\npayload=GNLinkStream.exe\\npayload=ui\\\\shell.html\\n";
    const ManifestResult r = load_manifest(doc, sigHex, "windows", acceptingVerifier);
    check("payload names survive verification", r.status == ManifestStatus::Ok, r.detail);
    if (r.manifest) {
      const auto& names = r.manifest->fields().payloadNames;
      check("three names", names.size() == 3, std::to_string(names.size()));
      check("in the order written",
            names.size() == 3 && names[0] == "GNLinkHost.exe" &&
                names[1] == "GNLinkStream.exe" && names[2] == "ui\\\\shell.html");
    }
  }

  {
    // A bad payload name with a BAD signature must still report SignatureInvalid -- the ordering
    // holds even for the newest check.
    const std::string doc =
        "schema=1\\nplatform=windows\\nversion=1\\nartifact=a\\nsize=1\\n"
        "sha256=0000000000000000000000000000000000000000000000000000000000000000\\n"
        "payload=..\\\\evil.exe\\n";
    const ManifestResult r = load_manifest(doc, sigHex, "windows", rejectingVerifier);
    check("unsafe payload + bad signature -> SignatureInvalid (not Malformed)",
          r.status == ManifestStatus::SignatureInvalid, status_name(r.status));
  }

  // ---------------------------------------------------------------- the shipped key rejects all'''
assert old in d, 'anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('lock-on-door tests added')
