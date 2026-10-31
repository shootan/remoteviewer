import io

p = 'apps/native_poc/src/directory_session_client_test.cpp'
d = io.open(p, encoding='utf-8').read()
anchor = '''  // ------------------------------------------------------------- the envelope, parsed by hand'''
added = '''  // ------------------------------------------------- the owner, which is not the destination
  //
  // The check used to be "same url, same origin, and there is a token" -- which asks whether the
  // SERVER changed. Sign out and sign back in on the same server and all three still hold, while
  // the token is a different one. An attempt authorised by one sign-in would have been handed the
  // credential of the next.
  {
    using directory::update_endpoint_for;

    const auto atLaunch = update_endpoint_for("", "https://rem.example", "windows",
                                              "Authorization: Bearer aaa", "alice", 7);

    // The same account signing out and back in: same account name, same server, new token, and
    // the epoch is what says it is a different sign-in.
    const auto afterRelogin = update_endpoint_for("", "https://rem.example", "windows",
                                                  "Authorization: Bearer bbb", "alice", 8);
    check("a re-login is a different owner", !afterRelogin.same_owner_as(atLaunch));
    check("...even though the url is identical", afterRelogin.url == atLaunch.url);
    check("...and the origin is identical", afterRelogin.origin == atLaunch.origin);
    check("...and the new token is not empty", !afterRelogin.credentialHeader.empty());

    // A different account on the same server, at the same epoch.
    const auto otherAccount = update_endpoint_for("", "https://rem.example", "windows",
                                                  "Authorization: Bearer ccc", "bob", 7);
    check("another account is a different owner", !otherAccount.same_owner_as(atLaunch));

    // Unchanged is unchanged.
    const auto unchanged = update_endpoint_for("", "https://rem.example", "windows",
                                               "Authorization: Bearer aaa", "alice", 7);
    check("the same owner at the same epoch is the same owner",
          unchanged.same_owner_as(atLaunch));
  }

  // ------------------------------------------------- what travels with the credential, and why
  //
  // The credential used to be sent alone, and the receiver rebuilt the origin from its command
  // line. So the value arrived over a channel that had proved who was listening, and the origin it
  // would be sent to arrived in argv, which had proved nothing.
  {
    using update::decode_update_descriptor;
    using update::encode_update_descriptor;

    auto endpoint = directory::update_endpoint_for("", "https://rem.example", "windows",
                                                   "Authorization: Bearer aaa", "alice", 7);
    const std::string frame = encode_update_descriptor(endpoint);
    check("the frame carries the url", frame.find("url=https://rem.example") != std::string::npos,
          frame);
    check("...the origin", frame.find("origin=https://rem.example:443") != std::string::npos);
    check("...the owner and the epoch",
          frame.find("owner=alice") != std::string::npos &&
              frame.find("epoch=7") != std::string::npos);
    check("...the wire shape", frame.find("envelope=1") != std::string::npos);
    check("...and the credential", frame.find("cred=Authorization: Bearer aaa") != std::string::npos);

    update::UpdateEndpoint back;
    std::string error;
    check("it decodes", decode_update_descriptor(frame, &back, &error), error);
    check("...to the same endpoint",
          back.url == endpoint.url && back.origin == endpoint.origin &&
              back.ownerKey == endpoint.ownerKey && back.ownerEpoch == endpoint.ownerEpoch &&
              back.derived == endpoint.derived &&
              back.credentialHeader == endpoint.credentialHeader);

    // A field nobody knows means the two ends disagree about what this frame is. Guessing which
    // parts still apply is how that disagreement gets buried.
    check("an unknown field is refused, not ignored",
          !decode_update_descriptor(frame + "extra=1\\n", &back, &error), error);
    check("a frame with no version is refused",
          !decode_update_descriptor("url=x\\nenvelope=1\\n", &back, &error), error);
    check("a frame with no url is refused",
          !decode_update_descriptor("v=1\\nenvelope=1\\n", &back, &error), error);
    check("a version this does not understand is refused",
          !decode_update_descriptor("v=2\\nurl=x\\nenvelope=1\\n", &back, &error), error);

    // A newline in a value would forge a field, so it is refused at the sending end.
    endpoint.ownerKey = "alice\\nurl=https://evil.example";
    check("a value carrying a newline is not encoded at all",
          encode_update_descriptor(endpoint).empty());
  }

''' + anchor
assert d.count(anchor) == 1, 'anchor'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok cpp test')
