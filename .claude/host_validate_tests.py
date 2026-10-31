import io
p='apps/native_poc/src/directory_session_client_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    // ---- the rule'''
new='''    // ---- the advertised host, which the server does NOT validate
    //
    // It trims the configured value and sends it. A configuration slip therefore arrives here
    // looking like an address, and dialling it produces a timeout that reads as a network fault.
    // The client checks it, falls back to the directory host, and says that it did.
    using directory::observe_host_is_usable;
    check("an ordinary name is usable", observe_host_is_usable("udp.example.com"));
    check("an IPv4 literal is usable", observe_host_is_usable("192.168.0.6"));
    check("a scheme is not a host", !observe_host_is_usable("http://x"));
    check("a host:port is not a host", !observe_host_is_usable("x:1234"));
    check("a path is not a host", !observe_host_is_usable("x/y"));
    check("a space is not allowed", !observe_host_is_usable("a b"));
    check("a control character is not allowed", !observe_host_is_usable(std::string("a\tb")));
    check("empty is not a host", !observe_host_is_usable(""));
    check("an empty label is refused", !observe_host_is_usable("a..b"));
    check("a trailing dot is refused", !observe_host_is_usable("a.b."));
    check("a label may not start with -", !observe_host_is_usable("-a.b"));
    check("a label may not end with -", !observe_host_is_usable("a.b-"));
    check("an absurdly long name is refused", !observe_host_is_usable(std::string(254, 'a')));

    {
      // The port survives a bad host: only the host is dropped, and the drop is recorded.
      ObserveEndpoint bad;
      const bool parsed =
          parse_observe_metadata(R"({"observe":{"port":29181,"host":"http://x"}})", &bad);
      check("a bad host does not throw away a good port",
            parsed && bad.known && bad.port == 29181 && bad.host.empty());
      check("...and the rejection is recorded rather than silent", bad.hostRejected);

      ObserveEndpoint blank;
      parse_observe_metadata(R"({"observe":{"port":29181,"host":"   "}})", &blank);
      check("a whitespace-only host is absent, not rejected",
            blank.host.empty() && !blank.hostRejected);

      ObserveEndpoint good;
      parse_observe_metadata(R"({"observe":{"port":29181,"host":" udp.example "}})", &good);
      check("a host is trimmed", good.host == "udp.example" && !good.hostRejected, good.host);
    }

    // A port that only looks like an integer. The shared getter matches [0-9]+ and would read
    // 29181 out of 29181.5 -- this server rejects such a value, another server is not bound by
    // that, and a client that trusts the server to validate breaks on the first one that does not.
    check("a fractional port is refused",
          !parse_observe_metadata(R"({"observe":{"port":29181.5}})", &got));
    check("a quoted port is refused",
          !parse_observe_metadata(R"({"observe":{"port":"29181"}})", &got));
    check("a negative port is refused",
          !parse_observe_metadata(R"({"observe":{"port":-1}})", &got));

    // ---- the rule'''
assert old in d, 'rule anchor'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('host and port validation regressions')
