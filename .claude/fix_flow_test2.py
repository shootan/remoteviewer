# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/client_update_flow_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // ---------------------------------------------------------------- the fetch, for real
  {
    upd::UpdateEndpoint endpoint;
    endpoint.url = server.url() + "/api/update/manifest?platform=windows";
    endpoint.derived = true;
    endpoint.credentialHeader = "Authorization: Bearer session-token-xyz";
    endpoint.origin = remote60::native_poc::directory::directory_origin_key(server.url());
    upd::CheckResult r;
    const bool answered = run_check(endpoint, "0.2.100", keyHex, &r);
    check("a check with an endpoint really asks the directory",
          answered && server.requests() >= 1,
          std::to_string(server.requests()) + " request(s)");
    check("...carrying the session credential", server.sawAuthorization());
    check("...and finds the newer version", r.availableVersion == "0.2.105",
          std::string(upd::check_outcome_name(r.outcome)) + " " + r.availableVersion + " " +
              r.detail);
    const ShellUpdateNotice notice = shell_update_notice(upd::check_outcome_name(r.outcome),
                                                         r.availableVersion, r.detail);
    check("...and that is what the user is shown", notice.show, notice.logLine);
    check("...with the version in the text",
          notice.text.find("0.2.105") != std::string::npos, notice.text);
  }
'''
new = '''  // ---------------------------------------------------------------- the transport refuses http
  //
  // ⚠ This is where the intended "ask the server and read the answer" assertion stops, and it is
  // worth being exact about why rather than dressing it up.
  //
  // The update path refuses http at TWO layers: update_endpoint_for derives nothing from an http
  // directory (asserted above), and the fetch itself refuses an http url even when handed one
  // directly. Both are deliberate -- the credential travels on that request. So a loopback server
  // cannot be asked at all without TLS, and standing up TLS is not something this test can do.
  //
  // What is asserted instead is the refusal, with its reason, because that refusal is the thing
  // standing between a session token and a plaintext socket. The signed-manifest fetch over a
  // real https endpoint remains UNCOVERED here and is not claimed.
  {
    upd::UpdateEndpoint endpoint;
    endpoint.url = server.url() + "/api/update/manifest?platform=windows";
    endpoint.derived = true;
    endpoint.credentialHeader = "Authorization: Bearer session-token-xyz";
    endpoint.origin = remote60::native_poc::directory::directory_origin_key(server.url());
    upd::CheckResult r;
    const bool answered = run_check(endpoint, "0.2.100", keyHex, &r);
    check("an http endpoint is refused by the transport too", answered && server.requests() == 0,
          std::to_string(server.requests()) + " request(s)");
    check("...and the reason names https", r.detail.find("https") != std::string::npos, r.detail);
    const ShellUpdateNotice notice = shell_update_notice(upd::check_outcome_name(r.outcome),
                                                         r.availableVersion, r.detail);
    check("...and the user is not told a version is available", !notice.show, notice.logLine);
  }

  // ---------------------------------------------------------------- the notice the user would see
  //
  // The outcome-to-notice step, driven with the outcome a real check produces. Pure, so it needs
  // no server -- and it is the last step before the WebView, which is the boundary this file
  // cannot cross.
  {
    const ShellUpdateNotice notice = shell_update_notice("UpdateAvailable", "0.2.105", "newer");
    check("a newer version is shown to the user", notice.show, notice.logLine);
    check("...with the version in the text", notice.text.find("0.2.105") != std::string::npos,
          notice.text);
    const ShellUpdateNotice same = shell_update_notice("UpToDate", "", "");
    check("...and being up to date is silent", !same.show, same.logLine);
  }
'''
assert d.count(old) == 1, 'fetch block'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
