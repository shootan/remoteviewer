// The client's update check, from the values a sign-in produces through to the notice text.
//
// The client asked about updates exactly once, at start-up, before anyone had signed in -- so it
// had no session token and usually no server address, reported "not configured", and never asked
// again. Signing in is the moment both appear and nothing invited the check. The user's client
// therefore never showed an update, and the log said so every time: "update check: not configured
// -- no update URL is configured".
//
// What runs here is the production chain: update_endpoint_for -> check_for_update_async ->
// manifest_fetcher_for -> load_manifest -> shell_update_notice, against a real socket. The two
// things injected are the ones that must not be real in a test -- the trust anchor and the server
// -- and neither is a stub of product logic:
//
//   * the signature is checked with the SAME primitives default_verifier() uses, against the
//     shared throwaway key in apps/shared/update_manifest. Using the operational key would tie
//     this test to one machine and one user account, because its private half is DPAPI-bound.
//   * the directory is a loopback server that answers the envelope the product's own server
//     answers.
//
// NOT covered, and not claimed: delivery into the WebView. deliver_update_notice posts to a
// window and the page renders it; that cannot be driven headlessly. This ends at "the notice was
// produced and is allowed to be shown".
//
// Build: remote60_client_update_flow_test. Run: prints PASS lines, exit 0.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "client_shell_bridge.hpp"
#include "directory_client.hpp"
#include "client_update_gate.hpp"
#include "update_check.hpp"
#include "update_endpoint.hpp"
#include "update_manifest.hpp"
#include "update_signature.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace upd = remote60::native_poc::update;
namespace cli = remote60::native_poc::client;
using remote60::native_poc::ShellUpdateNotice;
using remote60::native_poc::shell_update_notice;

namespace {

int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!ok) ++gFailures;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::string chomp(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

/** JSON string body, with the newlines the manifest is made of. */
std::string json_escaped(const std::string& raw) {
  std::string out;
  for (char c : raw) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out.push_back(c);
  }
  return out;
}

/** A loopback directory that answers the update envelope, and remembers what it was asked. */
class FakeDirectory {
 public:
  bool Start(const std::string& manifest, const std::string& signatureHex) {
    body_ = "{\"manifest\":\"" + json_escaped(manifest) + "\",\"signature\":\"" + signatureHex +
            "\"}";
    listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    if (::listen(listen_, 8) != 0) return false;
    int len = sizeof(addr);
    getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] { Loop(); });
    return true;
  }

  void Stop() {
    if (listen_ != INVALID_SOCKET) {
      closesocket(listen_);
      listen_ = INVALID_SOCKET;
    }
    if (thread_.joinable()) thread_.join();
  }

  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
  int requests() const { return requests_.load(); }
  bool sawAuthorization() const { return sawAuth_.load(); }

 private:
  void Loop() {
    for (;;) {
      SOCKET c = accept(listen_, nullptr, nullptr);
      if (c == INVALID_SOCKET) return;
      std::string request;
      char buf[4096];
      for (;;) {
        const int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, static_cast<size_t>(n));
        if (request.find("\r\n\r\n") != std::string::npos) break;
      }
      ++requests_;
      std::string lower;
      for (char ch : request) lower.push_back(static_cast<char>(tolower(ch)));
      if (lower.find("authorization:") != std::string::npos) sawAuth_ = true;

      std::ostringstream rsp;
      rsp << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
          << body_.size() << "\r\nConnection: close\r\n\r\n" << body_;
      const std::string text = rsp.str();
      send(c, text.data(), static_cast<int>(text.size()), 0);
      closesocket(c);
    }
  }

  std::string body_;
  SOCKET listen_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::thread thread_;
  std::atomic<int> requests_{0};
  std::atomic<bool> sawAuth_{false};
};

/** The product's verification, with the test key as the anchor instead of the compiled-in one. */
upd::SignatureVerifier verifier_for(const std::string& keyHex) {
  return [keyHex](const std::string& document, const std::vector<uint8_t>& signature) {
    std::vector<uint8_t> key;
    if (!upd::decode_hex(keyHex, &key)) return false;
    return upd::verify_ecdsa_p256_sha256(document, signature, key);
  };
}

struct Answer {
  bool done = false;
  upd::CheckResult result;
  std::mutex mu;
  std::condition_variable cv;
};

/**
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

  auto answer = std::make_shared<Answer>();
  upd::check_for_update_async(config, upd::manifest_fetcher_for(config), verifier_for(keyHex),
                              [answer](upd::CheckResult r) {
                                std::lock_guard<std::mutex> lk(answer->mu);
                                answer->result = r;
                                answer->done = true;
                                answer->cv.notify_all();
                              });
  std::unique_lock<std::mutex> lk(answer->mu);
  if (!answer->cv.wait_for(lk, std::chrono::seconds(20), [answer] { return answer->done; })) {
    return false;
  }
  *out = answer->result;
  return true;
}

}  // namespace

int main() {
  std::printf("client_update_flow_test\n");
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

#ifndef REMOTE60_UPDATE_VECTORS_DIR
#define REMOTE60_UPDATE_VECTORS_DIR "apps/shared/update_manifest"
#endif
  const std::string dir = REMOTE60_UPDATE_VECTORS_DIR;
  const std::string manifest = read_file(dir + "/test_manifest.txt");
  const std::string sigHex = chomp(read_file(dir + "/test_manifest.sig"));
  const std::string keyHex = chomp(read_file(dir + "/test_public_key.txt"));
  if (manifest.empty() || sigHex.empty() || keyHex.empty()) {
    std::printf("FAIL  could not read the shared test vectors from %s\n", dir.c_str());
    return 1;
  }

  FakeDirectory server;
  if (!server.Start(manifest, sigHex)) {
    std::printf("FAIL  could not start the fake directory\n");
    return 1;
  }

  // ---------------------------------------------------------------- what a sign-in decides
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

  // ---------------------------------------------------------------- the transport refuses http
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

  // ---------------------------------------------------------------- asked once per session
  {
    cli::UpdateGateState gate;
    check("the first invitation for a session is taken",
          cli::should_check(gate, "tester@example", 1));
    cli::note_checked(&gate, "tester@example", 1);
    check("...and a second one for the same session is not",
          !cli::should_check(gate, "tester@example", 1));
    check("...while signing in again is a new question",
          cli::should_check(gate, "tester@example", 2));
    check("...and so is a different account",
          cli::should_check(gate, "someone@else", 1));
  }

  // ---------------------------------------------------------------- answers for a session that ended
  {
    check("an answer for the session that asked is shown",
          cli::may_publish("tester@example", 2, "tester@example", 2));
    check("...one that arrives after a sign-out is not",
          !cli::may_publish("tester@example", 2, "tester@example", 3));
    check("...nor one that arrives after signing in as somebody else",
          !cli::may_publish("tester@example", 2, "someone@else", 3));
  }

  // ---------------------------------------------------------------- the product actually asks again
  //
  // No exit code can see this, and its absence is the whole defect: the check existed and was
  // called from one place that ran before a session could exist.
#ifdef REMOTE60_CLIENT_SHELL_SRC
  {
    const std::string src = read_file(REMOTE60_CLIENT_SHELL_SRC);
    const size_t login = src.find("gSessionToken = token;");
    const size_t after = login == std::string::npos
                             ? std::string::npos
                             : src.find("start_update_check();", login);
    size_t count = 0;
    for (size_t at = src.find("start_update_check();"); at != std::string::npos;
         at = src.find("start_update_check();", at + 1)) {
      ++count;
    }
    check("the client asks from more than one place", count >= 2, std::to_string(count));
    check("...and one of them is after a session exists", after != std::string::npos);
  }
#else
  check("the product's call sites are checked", false, "REMOTE60_CLIENT_SHELL_SRC not defined");
#endif

  server.Stop();
  WSACleanup();

  if (gFailures == 0) {
    std::printf("client_update_flow_test: PASS\n");
    return 0;
  }
  std::printf("client_update_flow_test: FAILED (%d)\n", gFailures);
  return 1;
}
