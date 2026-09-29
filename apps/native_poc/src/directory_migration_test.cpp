// What happens to a host token that was cached under another address.
//
// The product has one directory server now, and an install made before that has a token in
// host.json beside whatever address it was given then. Three things can be true of that address,
// and each has one right outcome:
//
//   the server itself        the token is used, as it always was
//   a listed former name     the token is presented to the server, and the cache is rewritten
//                            to name the server only after the server has accepted it
//   anything else            the token is not sent anywhere and not erased
//
// Two of these are about where a credential goes, which no passing sign-in shows. So this stands
// up directories -- http and udp, in this process, on ports the OS picks -- scripts what they
// answer, and reads back what each one was actually sent. The assertions are about requests
// received and bytes on disk.
//
// The HostAgent here is the product's, driven through Start() the way the streaming host drives
// it. What this cannot show: TLS (the fixtures are http on loopback), the real server, and an
// installed host going through an update -- that last one is a field check.
//
// EACH AGENT CASE SITS THROUGH AT LEAST ONE HEARTBEAT CYCLE, which the product floors at five
// seconds. The whole file takes about a minute; every wait is bounded.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"
#include "directory_fake_server.hpp"
#include "fixed_directory.hpp"
#include "winhttp_transport.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

namespace directory = remote60::native_poc::directory;
using directory::CachedOrigin;
using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;

int gFailures = 0;
int gChecks = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}

// Not a real token: nothing issued it. Distinctive, so it can be looked for in what arrived.
const char kToken[] = "fixture-host-token-7f3a9c";

std::string exe_directory() {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string text(path);
  const size_t slash = text.find_last_of("\\/");
  return slash == std::string::npos ? std::string(".") : text.substr(0, slash);
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/** A directory that accepts observations and answers heartbeats with `heartbeat`. */
void script_directory(FakeDirectory& dir, const Reply& heartbeat) {
  dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                         std::to_string(dir.udpPort()) + "}}"}});
  dir.Script("/api/host/heartbeat", {heartbeat});
  // A registration would be a way round the question, so it is answered but never expected:
  // the agent is given no password.
  dir.Script("/api/host/register", {Reply{401, "{\"error\":\"not in this test\"}"}});
}

struct AgentRun {
  bool started = false;
  std::string startError;
  std::string status;
  std::string cachePath;
  std::string cacheBefore;
  std::string cacheAfter;
  directory::HostCache after;
};

/**
 * Runs the product's HostAgent against `serverUrl` with a cache that names `cachedUrl`.
 *
 * `until` says when the case has seen what it came for; the run ends then, or after the bound.
 */
AgentRun run_agent(const char* label, const std::string& serverUrl, const std::string& cachedUrl,
                   const std::vector<std::string>& migratable,
                   const std::function<bool()>& until, int boundMs = 15000,
                   bool expectStart = true) {
  AgentRun run;
  run.cachePath = exe_directory() + "\\migration-fixture-" + label + ".json";

  directory::HostCache seed;
  seed.directoryUrl = cachedUrl;
  seed.accountId = "tester";
  seed.machineId = directory::machine_id();
  seed.hostName = "Cached PC";
  seed.hostId = "h-cached";
  seed.hostToken = kToken;
  directory::save_host_cache(run.cachePath, seed);
  run.cacheBefore = read_file(run.cachePath);

  SOCKET media = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in bindAddr{};
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(media, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
  DWORD timeout = 200;
  setsockopt(media, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));

  directory::HostAgent agent;
  std::atomic<bool> pumping{true};
  std::thread pump([&] {
    char buf[2048];
    while (pumping.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(media, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                             &fromLen);
      if (n > 0) agent.ConsumeUdpPacket(buf, static_cast<size_t>(n), from);
    }
  });

  directory::HostAgentConfig cfg;
  cfg.url = serverUrl;
  cfg.accountId = "tester";
  cfg.hostName = "Fixture PC";
  cfg.cachePath = run.cachePath;
  cfg.heartbeatSeconds = 5;
  cfg.migratableOrigins = migratable;
  // No password. Registering is then impossible, so a heartbeat can only carry the cached token.

  std::string error;
  const bool started = agent.Start(cfg, [&](const void* data, size_t len, const sockaddr_in& to) {
    // Loopback only: the fixtures live there, and nothing this test sends may leave the machine.
    if (to.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return;
    sendto(media, static_cast<const char*>(data), static_cast<int>(len), 0,
           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  }, &error);
  run.started = started;
  run.startError = error;
  if (expectStart) {
    check((std::string("the host agent starts (") + label + ")").c_str(), started, error);
  }

  for (int waited = 0; waited < boundMs && !until(); waited += 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  run.status = agent.StatusLine();
  agent.Stop();
  pumping = false;
  closesocket(media);
  if (pump.joinable()) pump.join();

  run.cacheAfter = read_file(run.cachePath);
  directory::load_host_cache(run.cachePath, &run.after);
  DeleteFileA(run.cachePath.c_str());
  return run;
}

// ------------------------------------------------------------------------------ the rule

void test_classification() {
  const std::string server = "https://gnlink.example";
  const std::vector<std::string> former = {"https://rem.example"};
  const auto is = [&](const char* cached, CachedOrigin want) {
    return directory::classify_cached_origin(cached, server, former) == want;
  };
  check("[rule] the server's own address is Same", is("https://gnlink.example", CachedOrigin::Same));
  check("[rule] ...however it is spelt",
        is("https://GNLINK.example:443/", CachedOrigin::Same));
  check("[rule] a listed former name is Migratable",
        is("https://rem.example", CachedOrigin::Migratable) &&
            is("https://rem.example:443/", CachedOrigin::Migratable));
  check("[rule] the former name over http is not the former name",
        is("http://rem.example", CachedOrigin::Unlisted));
  check("[rule] ...nor on another port", is("https://rem.example:8443", CachedOrigin::Unlisted));
  check("[rule] ...nor a name that contains it",
        is("https://rem.example.evil.test", CachedOrigin::Unlisted) &&
            is("https://xrem.example", CachedOrigin::Unlisted));
  check("[rule] the server over http is not the server",
        is("http://gnlink.example", CachedOrigin::Unlisted));
  check("[rule] an address nobody listed is Unlisted",
        is("http://127.0.0.1:1", CachedOrigin::Unlisted) &&
            is("https://192.168.0.6", CachedOrigin::Unlisted));
  check("[rule] no address at all is Unlisted, not the server",
        is("", CachedOrigin::Unlisted) && is("   ", CachedOrigin::Unlisted));
  check("[rule] with nothing listed, only the server itself is usable",
        directory::classify_cached_origin("https://rem.example", server, {}) ==
            CachedOrigin::Unlisted);
}

void test_product_list() {
  using remote60::native_poc::kFixedDirectoryUrl;
  using remote60::native_poc::kMigratableDirectoryOrigins;
  check("[product] the fixed address is an https origin and nothing more",
        directory::directory_url_is_secure(kFixedDirectoryUrl) &&
            directory::directory_origin_key(kFixedDirectoryUrl) ==
                std::string(kFixedDirectoryUrl) + ":443",
        kFixedDirectoryUrl);
  bool exact = true;
  std::string listed;
  for (const char* origin : kMigratableDirectoryOrigins) {
    listed += std::string(listed.empty() ? "" : ", ") + origin;
    exact = exact && directory::directory_url_is_secure(origin) &&
            directory::directory_origin_key(origin) == std::string(origin) + ":443" &&
            directory::directory_origin_key(origin) !=
                directory::directory_origin_key(kFixedDirectoryUrl);
  }
  check("[product] every former name is an exact https origin, and none is the server", exact,
        listed);
  check("[product] the list is https://rem.shotan.net and nothing else",
        listed == "https://rem.shotan.net", listed);
  check("[product] the list is handed out for the fixed address",
        directory::product_migratable_origins_for(kFixedDirectoryUrl).size() ==
            std::size(kMigratableDirectoryOrigins) &&
            directory::product_migratable_origins_for("https://GNLink.shotan.net:443/").size() ==
                std::size(kMigratableDirectoryOrigins));
  check("[product] ...and for no other server",
        directory::product_migratable_origins_for("http://127.0.0.1:8080").empty() &&
            directory::product_migratable_origins_for("https://rem.shotan.net").empty() &&
            directory::product_migratable_origins_for("http://gnlink.shotan.net").empty() &&
            directory::product_migratable_origins_for("").empty());
}

// ------------------------------------------------------------------------------ updates
//
// A machine that installed an earlier build asks the address it has stored -- the former name --
// for its manifest, and the manifest a release publishes now names artifacts under the fixed
// address. Manifest on one origin, artifacts on another: the credential belongs to the first
// and must not travel to the second. The artifacts are served without one.
//
// This is the decision the updater's fetch functions make before every request
// (production_updater_deps, through credential_allowed). That an update then runs to the end
// with artifacts on a host of their own is update_release_test; that the two real hosts answer
// as expected is a check against the real hosts, which no test here makes.
void test_update_across_the_two_names() {
  namespace update = remote60::native_poc::update;
  const std::string credential = std::string("x-host-token: ") + kToken;
  const char* former = remote60::native_poc::kMigratableDirectoryOrigins[0];
  const std::string fixed = remote60::native_poc::kFixedDirectoryUrl;

  const update::UpdateEndpoint installed =
      directory::update_endpoint_for("", former, "windows", credential, "owner", 1);
  check("[update] an earlier install asks the former name for its manifest",
        installed.derived &&
            installed.url == std::string(former) + ":443/api/update/manifest?platform=windows",
        installed.url);
  check("[update] ...and sends its credential with that request",
        update::credential_allowed(installed, installed.url));
  check("[update] ...BUT NOT WITH AN ARTIFACT UNDER THE FIXED ADDRESS",
        !update::credential_allowed(installed, fixed + "/updates/0.2.144/GNLinkHost.exe") &&
            !update::credential_allowed(installed, fixed + ":443/updates/0.2.144/ui/shell.html"));

  const update::UpdateEndpoint current =
      directory::update_endpoint_for("", fixed, "windows", credential, "owner", 1);
  check("[update] this build asks the fixed address",
        current.derived && current.url == fixed + ":443/api/update/manifest?platform=windows",
        current.url);
  check("[update] ...and an artifact still published under the former name gets no credential",
        update::credential_allowed(current, current.url) &&
            !update::credential_allowed(current, std::string(former) +
                                                     "/updates/0.2.143/GNLinkHost.exe"));
}

// ------------------------------------------------------------------------------ the agent

void test_former_name_is_accepted() {
  FakeDirectory server, former;
  if (!server.Start() || !former.Start()) { check("[accepted] fixtures start", false); return; }
  script_directory(server, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});

  const AgentRun run = run_agent("accepted", server.url(), former.url(), {former.url()},
                                 [&] { return server.Count("/api/host/heartbeat") >= 1; });
  check("[accepted] the cached token is presented to the server",
        server.RequestsContaining(kToken) >= 1,
        "requests carrying it: " + std::to_string(server.RequestsContaining(kToken)));
  check("[accepted] the host is online without registering again",
        run.status.rfind("online", 0) == 0 && server.Count("/api/host/register") == 0, run.status);
  check("[accepted] the cache now names the server",
        directory::directory_origin_key(run.after.directoryUrl) ==
            directory::directory_origin_key(server.url()),
        run.after.directoryUrl);
  check("[accepted] ...with the same token, account and host id",
        run.after.hostToken == kToken && run.after.accountId == "tester" &&
            run.after.hostId == "h-cached");
  check("[accepted] the former name received nothing", former.Requests() == 0,
        "requests: " + std::to_string(former.Requests()));
}

void test_unlisted_is_neither_sent_nor_erased() {
  FakeDirectory server, former, elsewhere;
  if (!server.Start() || !former.Start() || !elsewhere.Start()) {
    check("[unlisted] fixtures start", false);
    return;
  }
  script_directory(server, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});
  script_directory(elsewhere, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});

  // Long enough for a whole cycle: an agent that was going to send would have by then.
  const auto started = std::chrono::steady_clock::now();
  const AgentRun run = run_agent("unlisted", server.url(), elsewhere.url(), {former.url()}, [&] {
    return std::chrono::steady_clock::now() - started > std::chrono::seconds(7);
  }, 15000, /*expectStart=*/false);
  check("[unlisted] NO REQUEST CARRYING THE TOKEN REACHED THE SERVER",
        server.RequestsContaining(kToken) == 0,
        "requests carrying it: " + std::to_string(server.RequestsContaining(kToken)) +
            " of " + std::to_string(server.Requests()));
  check("[unlisted] no heartbeat was sent at all", server.Count("/api/host/heartbeat") == 0);
  check("[unlisted] the address it was cached under received nothing",
        elsewhere.Requests() == 0 && former.Requests() == 0);
  check("[unlisted] the cache is byte for byte what it was", run.cacheAfter == run.cacheBefore);
  check("[unlisted] ...so the token is still in it", run.after.hostToken == kToken);
  // With no usable token and no password the agent does not start at all, and says why. That
  // is what a machine with no cache does, which is the point: the token might as well not be
  // there. The host's window shows its sign-in form in this state (host_login_ui_runner.js).
  check("[unlisted] the agent does not start, and says a sign-in is needed",
        !run.started && run.startError.find("no cached host token") != std::string::npos,
        run.startError);
}

void test_rejected_needs_signing_in() {
  FakeDirectory server, former;
  if (!server.Start() || !former.Start()) { check("[401] fixtures start", false); return; }
  script_directory(server, Reply{401, "{\"error\":\"unknown host token\"}"});
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});

  const AgentRun run = run_agent("rejected", server.url(), former.url(), {former.url()},
                                 [&] { return server.Count("/api/host/heartbeat") >= 1; });
  check("[401] the token was presented, once",
        server.RequestsContaining(kToken) == 1 && server.Count("/api/host/heartbeat") == 1,
        "heartbeats: " + std::to_string(server.Count("/api/host/heartbeat")));
  check("[401] the host is not online and says it has to sign in again",
        run.status.find("rejected") != std::string::npos ||
            run.status.find("registration needs id/pw") != std::string::npos,
        run.status);
  check("[401] the cache was not rewritten to name the server",
        run.cacheAfter == run.cacheBefore, run.after.directoryUrl);
  check("[401] the former name received nothing", former.Requests() == 0);
}

void test_not_an_answer_about_the_token(const char* label, int statusCode) {
  FakeDirectory server, former;
  if (!server.Start() || !former.Start()) { check("fixtures start", false); return; }
  script_directory(server, Reply{statusCode, "{\"error\":\"try later\"}"});
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});

  // Two heartbeats: the second is the evidence that the token was kept and tried again.
  const AgentRun run = run_agent(label, server.url(), former.url(), {former.url()},
                                 [&] { return server.Count("/api/host/heartbeat") >= 2; });
  const std::string tag = std::string("[") + label + "] ";
  check((tag + "the token is tried again on the next cycle").c_str(),
        server.Count("/api/host/heartbeat") >= 2 && server.RequestsContaining(kToken) >= 2,
        "heartbeats carrying it: " + std::to_string(server.RequestsContaining(kToken)));
  check((tag + "the cache keeps the token and the address it was found with").c_str(),
        run.cacheAfter == run.cacheBefore, run.after.directoryUrl);
  check((tag + "it is reported as a failed heartbeat, not as a sign-out").c_str(),
        run.status.find("http " + std::to_string(statusCode)) != std::string::npos &&
            run.status.find("registration needs") == std::string::npos,
        run.status);
  check((tag + "the former name received nothing").c_str(), former.Requests() == 0);
}

void test_server_unreachable() {
  FakeDirectory former;
  if (!former.Start()) { check("[down] fixtures start", false); return; }
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});
  std::string deadUrl;
  {
    // An address that was just in use and is not any more: nothing listens there.
    FakeDirectory gone;
    if (!gone.Start()) { check("[down] fixtures start", false); return; }
    deadUrl = gone.url();
  }
  const auto started = std::chrono::steady_clock::now();
  const AgentRun run = run_agent("down", deadUrl, former.url(), {former.url()}, [&] {
    return std::chrono::steady_clock::now() - started > std::chrono::seconds(7);
  });
  check("[down] the cache keeps the token and the address it was found with",
        run.cacheAfter == run.cacheBefore, run.after.directoryUrl);
  check("[down] it is not reported as a sign-out",
        run.status.find("registration needs") == std::string::npos &&
            run.status.find("rejected") == std::string::npos,
        run.status);
  check("[down] the former name was not tried instead", former.Requests() == 0);
}

// ------------------------------------------------------------------------------ redirects

/**
 * A server that answers a credentialed request with "ask over there instead".
 *
 * Both transports the directory client has. The WinHTTP one is what an https directory is
 * reached through; it is exercised here over http, which is the same request object with the
 * same options and no TLS. That the option holds under TLS is not shown by this.
 */
void test_redirect_is_not_followed(int statusCode) {
  FakeDirectory server, elsewhere;
  if (!server.Start() || !elsewhere.Start()) { check("[redirect] fixtures start", false); return; }
  const std::string tag = "[redirect " + std::to_string(statusCode) + "] ";
  for (const char* path : {"/api/host/heartbeat", "/api/hosts", "/api/log"}) {
    server.Script(path, {Reply{statusCode, ""}});
    server.Headers(path, "Location: " + elsewhere.url() + path + "\r\n");
    elsewhere.Script(path, {Reply{200, "{\"ok\":true,\"hosts\":[]}"}});
  }
  const std::string body = std::string("{\"hostToken\":\"") + kToken + "\"}";
  const std::string credentials = std::string("Authorization: Bearer ") + kToken +
                                  "\r\nx-host-token: " + kToken + "\r\n";
  const uint16_t port = static_cast<uint16_t>(std::stoi(server.url().substr(17)));

  remote60::native_poc::net::HttpResult posted;
  const bool postOk = remote60::native_poc::net::http_exchange(
      "127.0.0.1", port, false, "POST", "/api/host/heartbeat", credentials, body,
      "application/json", 5000, &posted);
  check((tag + "WinHTTP POST: the answer is the redirect itself").c_str(),
        postOk && posted.status == static_cast<uint32_t>(statusCode),
        "status " + std::to_string(posted.status) + " " + posted.error);

  remote60::native_poc::net::HttpResult got;
  const bool getOk = remote60::native_poc::net::http_exchange(
      "127.0.0.1", port, false, "GET", "/api/hosts", credentials, std::string(), nullptr, 5000,
      &got);
  check((tag + "WinHTTP GET: the answer is the redirect itself").c_str(),
        getOk && got.status == static_cast<uint32_t>(statusCode),
        "status " + std::to_string(got.status) + " " + got.error);

  uint32_t socketStatus = 0;
  std::string socketBody;
  const bool socketOk = directory::http_post("127.0.0.1", port, false, "/api/log", "text/plain",
                                             credentials, body, &socketStatus, &socketBody);
  check((tag + "socket POST: the answer is the redirect itself").c_str(),
        socketOk && socketStatus == static_cast<uint32_t>(statusCode),
        "status " + std::to_string(socketStatus));

  check((tag + "the server did receive the three requests, with the credential").c_str(),
        server.Requests() == 3 && server.RequestsContaining(kToken) == 3,
        std::to_string(server.RequestsContaining(kToken)) + " of " +
            std::to_string(server.Requests()));
  check((tag + "THE ADDRESS IT POINTED AT RECEIVED NOTHING").c_str(),
        elsewhere.Requests() == 0,
        "requests: " + std::to_string(elsewhere.Requests()) + ", carrying the credential: " +
            std::to_string(elsewhere.RequestsContaining(kToken)));
}

}  // namespace

int main() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock\n");
    return 2;
  }

  test_classification();
  test_product_list();
  test_update_across_the_two_names();
  for (int status : {301, 302, 307, 308}) test_redirect_is_not_followed(status);
  test_former_name_is_accepted();
  test_unlisted_is_neither_sent_nor_erased();
  test_rejected_needs_signing_in();
  test_not_an_answer_about_the_token("500", 500);
  test_not_an_answer_about_the_token("429", 429);
  test_server_unreachable();

  WSACleanup();
  if (gFailures == 0) {
    std::printf("directory_migration_test: ALL PASS (%d checks)\n", gChecks);
    return 0;
  }
  std::printf("directory_migration_test: FAIL (%d of %d failed)\n", gFailures, gChecks);
  return 1;
}
