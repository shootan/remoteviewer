import io

p = 'apps/native_poc/src/directory_session_client_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''int main() {'''
new = '''int main() {
  // ---------------------------------------------------------------- where observations go
  //
  // The clients used to derive this as httpPort + 1. Behind TLS on 443 that is 444, nothing
  // listens there, and a host whose observation fails skips its heartbeat -- so it never appears
  // in the list at all, and the relay cannot cover for it because the relay address only arrives
  // in a /api/connect response the viewer never reaches.
  {
    using directory::ObserveEndpoint;
    using directory::observe_port_for;
    using directory::parse_observe_metadata;

    ObserveEndpoint got;
    check("the server's port is read",
          parse_observe_metadata(R"({"sessionToken":"t","observe":{"port":29181}})", &got) &&
              got.known && got.port == 29181 && got.host.empty(),
          std::to_string(got.port));

    check("and a host with it",
          parse_observe_metadata(R"({"observe":{"port":29181,"host":"udp.example"}})", &got) &&
              got.host == "udp.example",
          got.host);

    // Absent is the ordinary case for a server that has not been updated.
    check("no metadata is not an error, just absent",
          !parse_observe_metadata(R"({"sessionToken":"t"})", &got) && !got.known);

    // Out of range is treated as absent rather than clamped. A wrong port is worse than none:
    // none leaves the caller on a documented fallback, wrong sends it somewhere that will never
    // answer and looks like a network fault.
    check("port 0 is refused", !parse_observe_metadata(R"({"observe":{"port":0}})", &got));
    check("port 65536 is refused", !parse_observe_metadata(R"({"observe":{"port":65536}})", &got));
    check("a malformed observe object is refused",
          !parse_observe_metadata(R"({"observe":{"nope":1}})", &got));

    // The nested read matters: a flat search would find a "port" belonging to something else.
    // It happens to be unambiguous in today's responses, and would not stay that way.
    check("a port outside the observe object is not mistaken for it",
          !parse_observe_metadata(R"({"port":29181,"sessionToken":"t"})", &got));
    check("the observe object wins over an unrelated port",
          parse_observe_metadata(R"({"port":1,"observe":{"port":29181}})", &got) &&
              got.port == 29181,
          std::to_string(got.port));

    // ---- the rule
    ObserveEndpoint said;
    said.known = true;
    said.port = 29181;
    check("what the server said is used, on https",
          observe_port_for(said, 443, true) == 29181);
    check("...and on http too", observe_port_for(said, 8080, false) == 29181);

    // Plain http with nothing said keeps working exactly as every existing deployment does.
    check("http with no metadata keeps the +1 default",
          observe_port_for(ObserveEndpoint{}, 8080, false) == 8081);
    check("http on 65535 has nowhere to add one",
          observe_port_for(ObserveEndpoint{}, 65535, false) == 0);

    // THE case this exists for. 443 + 1 is not a fallback; it is a guess that cannot be right.
    check("https with no metadata refuses to guess 444",
          observe_port_for(ObserveEndpoint{}, 443, true) == 0);
    check("...and does not fall back to a fixed 29181 either",
          observe_port_for(ObserveEndpoint{}, 443, true) != 29181);
  }
'''
assert old in d, 'main'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('observe endpoint regressions')
