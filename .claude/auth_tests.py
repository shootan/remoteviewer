import io

p = 'apps/native_poc/src/directory_session_client_test.cpp'
d = io.open(p, encoding='utf-8').read()
anchor = '''  // --------------------------------------------------------------- what the parser now accepts'''
added = '''  // ------------------------------------------------- what may be sent, and where it may be sent
  //
  // The route needs a session or a host token, so a derived url has to carry one. The danger is
  // not forgetting to send it -- that fails loudly with a 401 -- it is sending it somewhere it
  // does not belong. A manifest names artifact urls and is free to name them on any host; a
  // signature over that manifest says the bytes are the right bytes, and says nothing at all
  // about whether our directory's credential belongs to the host serving them.
  {
    using directory::update_endpoint_for;
    using update::credential_allowed;

    const auto derived = update_endpoint_for("", "https://rem.example", "windows",
                                             "x-host-token: secret");
    check("a derived endpoint carries the credential", !derived.credentialHeader.empty());
    check("...and names the one origin it may reach",
          derived.origin == "https://rem.example:443", derived.origin);
    check("...and selects the envelope shape", derived.derived);
    check("the credential goes to that url", credential_allowed(derived, derived.url));
    check("...and to another path on the same origin",
          credential_allowed(derived, "https://rem.example/api/update/artifact"));
    check("...and to the same origin written differently",
          credential_allowed(derived, "https://REM.example:443/x"));

    check("but not to another host", !credential_allowed(derived, "https://cdn.example/a.zip"));
    check("...not to another port", !credential_allowed(derived, "https://rem.example:8443/a"));
    check("...not to the same host over http",
          !credential_allowed(derived, "http://rem.example/a"));
    check("...and not to nothing at all", !credential_allowed(derived, ""));

    // An override is somebody else's server by definition. Pointing it at our own host does not
    // make it ours: what decides is where the url came from, and a rule about what it looks like
    // is a comparison that can be wrong on the day it matters.
    const auto override_ = update_endpoint_for("https://rem.example/m", "https://rem.example",
                                               "windows", "x-host-token: secret");
    check("an override carries no credential", override_.credentialHeader.empty());
    check("...and no origin to send one to", override_.origin.empty(), override_.origin);
    check("...and selects the detached shape", !override_.derived);
    check("...even when it points at our own directory",
          !credential_allowed(override_, "https://rem.example/m"));

    // A client with no session yet. Normal, not an error: the server says 401 and that is an
    // update-check failure, not a reason to sign the user out.
    const auto anonymous = update_endpoint_for("", "https://rem.example", "windows", "");
    check("no credential means none is sent", !credential_allowed(anonymous, anonymous.url));
    check("...but the endpoint is still configured", anonymous.configured() && anonymous.derived);

    // An http directory derives nothing, so there is nothing to attach anything to.
    const auto cleartext = update_endpoint_for("", "http://rem.example", "windows",
                                               "x-host-token: secret");
    check("an http directory yields no endpoint", !cleartext.configured());
    check("...and no credential to leak", !credential_allowed(cleartext, "http://rem.example/m"));
  }

  // ------------------------------------------------------------- the envelope, parsed by hand
  {
    std::string document;
    std::string signature;
    std::string error;
    check("the two fields come out of one body",
          update::parse_manifest_envelope(
              R"({"manifest":"schema=2\\nversion=0.2.105\\n","signature":"abcd"})", &document,
              &signature, &error),
          error);
    check("...with the escapes a manifest contains decoded",
          document == "schema=2\\nversion=0.2.105\\n", document);
    check("...and the signature beside it", signature == "abcd", signature);

    check("a body with neither is refused",
          !update::parse_manifest_envelope(R"({"ok":true})", &document, &signature, &error));
    check("...and says so", error.find("manifest") != std::string::npos, error);
    check("a truncated body is not a field",
          !update::parse_manifest_envelope(R"({"manifest":"schema=2)", &document, &signature,
                                           &error));
  }

''' + anchor
assert d.count(anchor) == 1, 'anchor'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok cpp test')
