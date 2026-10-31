import io

p = 'apps/native_poc/src/directory_session_client_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // The https half of the same branch is NOT reachable yet -- parse_directory_url still rejects
  // https before the port rule runs -- so it is not asserted here and must not be read as covered.
  // It becomes reachable in (C), and the assertion belongs with that change.
  {'''
new = '''  // The https half is asserted below now that the parser accepts https; the note that said it was
  // unreachable is gone with the rejection it described.
  {'''
assert d.count(old) == 1, 'note'
d = d.replace(old, new, 1)

anchor = '''    check("...and the reason names the port, not the network",
          error.find("65535") != std::string::npos, error);
  }
'''
assert d.count(anchor) == 1, 'anchor'

added = anchor + '''
  // ------------------------------------------------- the https refusal, now that it is reachable
  //
  // This is the defect the observe work exists for: on an https directory the clients computed
  // 443 + 1 and sent their observation to 444. Nothing answers there, so the host never completed
  // a heartbeat and never appeared in anyone's list. Refusing with a reason is the difference
  // between a server that needs one setting and a product that does not work.
  //
  // Every case below returns before any socket, or fails at name resolution against a .invalid
  // name (RFC 2606, guaranteed not to resolve) -- so what is asserted is the decision, not a
  // network. The endpoint the code decided on is in that resolution error, which is how the port
  // it picked is visible at all.
  {
    DirectorySessionRequest base{};
    base.url = "https://directory.invalid";
    base.sessionToken = "t";
    base.hostId = "h";

    {
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(base, &session, &error);
      check("https with no advertisement is refused, not dialled on 444", !opened, error);
      check("...and the reason points at the server's configuration",
            error.find("observations") != std::string::npos, error);
      check("...and it never got as far as a socket",
            error.find("resolve") == std::string::npos, error);
      check("...and 444 is nowhere in it", error.find("444") == std::string::npos, error);
    }

    // request.advertised is what the login response carried. A caller that fills it must get past
    // the refusal, and on the port the server named -- not on some default that happens to work.
    {
      DirectorySessionRequest request = base;
      request.advertised.known = true;
      request.advertised.port = 29181;
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("an advertised port gets past the refusal", !opened && error.find("observations") == std::string::npos, error);
      check("...and it is the port that was advertised",
            error.find(":29181") != std::string::npos, error);
    }

    // The advertised host too, which is a separate field and was separately ignored.
    {
      DirectorySessionRequest request = base;
      request.advertised.known = true;
      request.advertised.port = 29181;
      request.advertised.host = "observe.invalid";
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("the advertised host is the one dialled",
            !opened && error.find("observe.invalid:29181") != std::string::npos, error);
    }

    // A host the client refused (the server does not validate this field) falls back to the
    // directory's own host rather than to nothing.
    {
      DirectorySessionRequest request = base;
      request.advertised.known = true;
      request.advertised.port = 29181;
      request.advertised.hostRejected = true;  // parse_observe_metadata left the host empty
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("a rejected observe host falls back to the directory host",
            !opened && error.find("directory.invalid:29181") != std::string::npos, error);
    }

    // The pin is the operator's decision and outranks both the advertisement and the default.
    {
      DirectorySessionRequest request = base;
      request.directoryUdpPort = 40000;
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("a pinned port carries https past the refusal",
            !opened && error.find(":40000") != std::string::npos, error);
    }
    {
      DirectorySessionRequest request = base;
      request.directoryUdpPort = 40000;
      request.advertised.known = true;
      request.advertised.port = 29181;
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("the pin outranks the advertisement",
            !opened && error.find(":40000") != std::string::npos, error);
      check("...and the advertised port is not the one used",
            error.find(":29181") == std::string::npos, error);
    }

    // http is untouched by all of this: the documented default is still one above the http port.
    {
      DirectorySessionRequest request{};
      request.url = "http://directory.invalid:29180";
      request.sessionToken = "t";
      request.hostId = "h";
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("http still derives the port as http + 1",
            !opened && error.find("directory.invalid:29181") != std::string::npos, error);
    }
  }

  // --------------------------------------------------------------- what the parser now accepts
  //
  // https used to be refused outright, which is why the 444 defect could not even be reproduced
  // through this path. The http branch was matched case-sensitively while the https branch was
  // not, inside the same function, so `HTTP://host` was "unsupported url scheme" -- a correctly
  // typed url refused for its capitals.
  {
    using directory::parse_directory_url;
    std::string host;
    uint16_t port = 0;
    bool secure = false;
    std::string error;

    check("https parses", parse_directory_url("https://rem.example", &host, &port, &error, &secure),
          error);
    check("...on 443 by default", port == 443, std::to_string(port));
    check("...and says so", secure);

    check("an explicit https port wins",
          parse_directory_url("https://rem.example:8443", &host, &port, &error, &secure) &&
              port == 8443 && secure,
          std::to_string(port));

    check("http parses", parse_directory_url("http://rem.example", &host, &port, &error, &secure),
          error);
    check("...on 80 by default", port == 80, std::to_string(port));
    check("...and says it is not secure", !secure);

    check("HTTP:// is the same scheme in capitals",
          parse_directory_url("HTTP://rem.example", &host, &port, &error, &secure), error);
    check("...with the same host", host == "rem.example", host);
    check("...the same port", port == 80, std::to_string(port));
    check("...and still not secure", !secure);

    check("HTTPS:// likewise",
          parse_directory_url("HTTPS://rem.example", &host, &port, &error, &secure) && secure &&
              port == 443,
          std::to_string(port));

    check("a scheme nobody serves is still refused",
          !parse_directory_url("ftp://rem.example", &host, &port, &error, &secure), error);
    // The out parameter is optional; older call sites pass four arguments and must still compile
    // and behave.
    check("the scheme is optional to ask for",
          parse_directory_url("http://rem.example:29180", &host, &port, &error) && port == 29180,
          error);
  }
'''

d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('bootstrap + parser regressions')
