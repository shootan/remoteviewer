# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/client_update_flow_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''/** One production check, start to finish. Returns false when the callback never arrived. */
bool run_check(const std::string& directoryUrl, const std::string& session,
               const std::string& owner, uint64_t epoch, const std::string& installedVersion,
               const std::string& keyHex, upd::CheckResult* out) {
  const upd::UpdateEndpoint endpoint = remote60::native_poc::directory::update_endpoint_for(
      std::string(), directoryUrl, "windows",
      session.empty() ? std::string() : "Authorization: Bearer " + session, owner, epoch);

  upd::CheckConfig config;
  config.manifestUrl = endpoint.url;
  config.derivedEndpoint = endpoint.derived;
  config.credentialHeader = endpoint.credentialHeader;
  config.credentialOrigin = endpoint.origin;
  config.trustedPublicKeyHex = keyHex;
  config.platform = "windows";
  config.installedVersion = installedVersion;
'''
new = '''/**
 * One production check, start to finish. Returns false when the callback never arrived.
 *
 * `endpoint` is built by the caller, because there are two different things worth driving: what
 * update_endpoint_for() decides from a directory url, and what the fetch chain does with an
 * endpoint. They are asserted separately -- see the note in main() about https.
 */
bool run_check(const upd::UpdateEndpoint& endpoint, const std::string& installedVersion,
               const std::string& keyHex, upd::CheckResult* out) {
  upd::CheckConfig config;
  config.manifestUrl = endpoint.url;
  config.derivedEndpoint = endpoint.derived;
  config.credentialHeader = endpoint.credentialHeader;
  config.credentialOrigin = endpoint.origin;
  config.trustedPublicKeyHex = keyHex;
  config.platform = "windows";
  config.installedVersion = installedVersion;
'''
assert d.count(old) == 1, 'run_check head'
d = d.replace(old, new, 1)

old2 = '''  // ---------------------------------------------------------------- before anyone has signed in
  {
    upd::CheckResult r;
    const bool answered = run_check(/*directoryUrl=*/"", /*session=*/"", /*owner=*/"", 0,
                                    "0.2.100", keyHex, &r);
    check("a check before sign-in answers without asking anyone", answered);
    const ShellUpdateNotice notice = shell_update_notice(upd::check_outcome_name(r.outcome),
                                                         r.availableVersion, r.detail);
    check("...and shows the user nothing", !notice.show, notice.logLine);
    check("...because there was nothing to ask", server.requests() == 0,
          std::to_string(server.requests()) + " request(s)");
  }

  // ---------------------------------------------------------------- after signing in
  {
    upd::CheckResult r;
    const bool answered = run_check(server.url(), "session-token-xyz", "tester@example", 1,
                                    "0.2.100", keyHex, &r);
    check("a check after sign-in really asks the directory", answered && server.requests() >= 1,
          std::to_string(server.requests()) + " request(s)");
    check("...carrying the session credential", server.sawAuthorization());
    check("...and finds the newer version", r.availableVersion == "0.2.105",
          std::string(upd::check_outcome_name(r.outcome)) + " " + r.availableVersion + " " + r.detail);
    const ShellUpdateNotice notice = shell_update_notice(upd::check_outcome_name(r.outcome),
                                                         r.availableVersion, r.detail);
    check("...and that is what the user is shown", notice.show, notice.logLine);
    check("...with the version in the text",
          notice.text.find("0.2.105") != std::string::npos, notice.text);
  }
'''
new2 = '''  // ---------------------------------------------------------------- what a sign-in decides
  //
  // The endpoint is derived from the directory url and the credential the session produced. This
  // is the step that never ran with a token in it: the only call site was before sign-in, so the
  // values below were always empty and the answer was always "not configured".
  //
  // Note the https requirement: the derived endpoint exists only for an https directory, because
  // the credential travels on it. That is policy, and it is asserted here rather than worked
  // around -- the fetch below therefore drives the chain from an endpoint directly, since a TLS
  // server is not something this test can stand up. The https transport itself is NOT covered.
  {
    const upd::UpdateEndpoint before = remote60::native_poc::directory::update_endpoint_for(
        std::string(), std::string(), "windows", std::string(), std::string(), 0);
    check("before sign-in there is nothing to ask", before.url.empty() && !before.derived);

    const upd::UpdateEndpoint after = remote60::native_poc::directory::update_endpoint_for(
        std::string(), "https://rem.example", "windows",
        "Authorization: Bearer session-token-xyz", "tester@example", 1);
    check("after sign-in an endpoint is derived", after.derived && !after.url.empty(), after.url);
    check("...carrying the session credential",
          after.credentialHeader.find("session-token-xyz") != std::string::npos,
          after.credentialHeader.empty() ? "(none)" : "present");
    check("...bound to the directory's origin", !after.origin.empty(), after.origin);

    const upd::UpdateEndpoint overHttp = remote60::native_poc::directory::update_endpoint_for(
        std::string(), "http://rem.example", "windows",
        "Authorization: Bearer session-token-xyz", "tester@example", 1);
    check("...and an http directory gets none, so no credential can leak",
          overHttp.url.empty() && !overHttp.derived);
  }

  // ---------------------------------------------------------------- nothing to ask, nothing asked
  {
    upd::UpdateEndpoint none;  // exactly what the pre-sign-in derivation produces
    upd::CheckResult r;
    const bool answered = run_check(none, "0.2.100", keyHex, &r);
    check("a check with no endpoint answers without asking anyone", answered);
    const ShellUpdateNotice notice = shell_update_notice(upd::check_outcome_name(r.outcome),
                                                         r.availableVersion, r.detail);
    check("...and shows the user nothing", !notice.show, notice.logLine);
    check("...because there was nothing to ask", server.requests() == 0,
          std::to_string(server.requests()) + " request(s)");
  }

  // ---------------------------------------------------------------- the fetch, for real
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
assert d.count(old2) == 1, 'cases'
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
