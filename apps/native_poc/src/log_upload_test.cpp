// Unit test for the log uploader (log_upload.hpp) against an in-process fake directory: the
// credential lifecycle the 2026-09-07 field run broke on (P10) -- a token replaced while the
// uploader runs, an identity change, a 401 that must pause rather than burn batches, bounded
// retry for outages, immediate discard for a server's final word, and a diag that never holds a
// token. Real sockets on 127.0.0.1, no product process.
//
// Build: remote60_log_upload_test (CMake). Run: prints "log_upload_test: PASS", exit 0.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "log_upload.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++gFailures;                                                     \
    }                                                                  \
  } while (0)

std::string gDiagDir;

std::string header_value(const std::string& headers, const std::string& name) {
  // Case-insensitive "name:" at line start.
  std::string lower;
  lower.reserve(headers.size());
  for (char c : headers) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  std::string key = name + ":";
  for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  size_t pos = 0;
  while ((pos = lower.find(key, pos)) != std::string::npos) {
    if (pos == 0 || lower[pos - 1] == '\n') {
      size_t start = pos + key.size();
      while (start < headers.size() && headers[start] == ' ') ++start;
      const size_t end = headers.find("\r\n", start);
      return headers.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    pos += key.size();
  }
  return {};
}

/** Minimal HTTP/1.1 sink: reads one request per connection, answers a scripted status, closes. */
class FakeLogServer {
 public:
  struct Request {
    std::string headers;
    std::string body;
    std::string auth() const { return header_value(headers, "Authorization"); }
    std::string hostToken() const { return header_value(headers, "x-host-token"); }
    std::string device() const { return header_value(headers, "x-log-device"); }
    std::string stream() const { return header_value(headers, "x-log-stream"); }
  };
  // status -1 = accept, read, close without answering (what a dead middlebox looks like);
  // delayMs = how long to sit on the request before answering (an in-flight send).
  struct Step {
    int status = 200;
    int delayMs = 0;
  };

  bool Start() {
    listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listenSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    if (listen(listenSock_, 16) != 0) return false;
    int len = sizeof(addr);
    getsockname(listenSock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] { Loop(); });
    return true;
  }

  void Stop() {
    if (listenSock_ != INVALID_SOCKET) {
      closesocket(listenSock_);
      listenSock_ = INVALID_SOCKET;
    }
    if (thread_.joinable()) thread_.join();
  }

  uint16_t port() const { return port_; }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

  // Next responses in order; when the script is empty every request gets 200 at once.
  void Script(std::vector<int> statuses) {
    std::vector<Step> steps;
    for (int s : statuses) steps.push_back(Step{s, 0});
    ScriptSteps(std::move(steps));
  }
  void ScriptSteps(std::vector<Step> steps) {
    std::lock_guard<std::mutex> lk(mu_);
    script_ = std::move(steps);
  }

  std::vector<Request> Requests() {
    std::lock_guard<std::mutex> lk(mu_);
    return requests_;
  }
  size_t Count() {
    std::lock_guard<std::mutex> lk(mu_);
    return requests_.size();
  }
  bool WaitFor(size_t n, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
      if (Count() >= n) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return Count() >= n;
  }
  void Clear() {
    std::lock_guard<std::mutex> lk(mu_);
    requests_.clear();
    script_.clear();
  }

 private:
  void Loop() {
    for (;;) {
      SOCKET c = accept(listenSock_, nullptr, nullptr);
      if (c == INVALID_SOCKET) return;
      std::string raw;
      char buf[4096];
      size_t headerEnd = std::string::npos;
      size_t contentLength = 0;
      for (;;) {
        const int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
        if (headerEnd == std::string::npos) {
          headerEnd = raw.find("\r\n\r\n");
          if (headerEnd != std::string::npos) {
            const std::string cl = header_value(raw.substr(0, headerEnd + 2), "Content-Length");
            contentLength = cl.empty() ? 0 : static_cast<size_t>(std::strtoul(cl.c_str(), nullptr, 10));
          }
        }
        if (headerEnd != std::string::npos && raw.size() >= headerEnd + 4 + contentLength) break;
      }
      Step step;
      {
        std::lock_guard<std::mutex> lk(mu_);
        if (headerEnd != std::string::npos) {
          Request r;
          r.headers = raw.substr(0, headerEnd + 2);
          r.body = raw.substr(headerEnd + 4, contentLength);
          requests_.push_back(std::move(r));
        }
        if (!script_.empty()) {
          step = script_.front();
          script_.erase(script_.begin());
        }
      }
      if (step.delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(step.delayMs));
      if (step.status >= 0) {
        const std::string resp = "HTTP/1.1 " + std::to_string(step.status) +
                                 " X\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
        send(c, resp.data(), static_cast<int>(resp.size()), 0);
      }
      shutdown(c, SD_BOTH);
      closesocket(c);
    }
  }

  SOCKET listenSock_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::thread thread_;
  std::mutex mu_;
  std::vector<Step> script_;
  std::vector<Request> requests_;
};

constexpr uint32_t kHold = 100000;  // a flush interval long enough that nothing flushes on its own
constexpr uint32_t kFast = 30;

LogUploadConfig base_config(const FakeLogServer& server, const std::string& identity) {
  LogUploadConfig c;
  c.directoryUrl = server.url();
  c.device = "test-device";
  c.identity = identity;
  c.flushIntervalMs = kFast;
  c.retryBaseDelayMs = 40;
  c.retryMaxAgeMs = 10000;
  return c;
}

bool wait_until(const std::function<bool()>& pred, int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

// Re-configure with the same credentials and a new flush interval: "unchanged" by contract, but
// the cadence applies, which is how the tests hold the queue and then release it deterministically.
void set_flush(LogUploadConfig& c, uint32_t flushMs) {
  c.flushIntervalMs = flushMs;
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  CHECK(reason == "unchanged");
  // Let the worker take the wake, find nothing and go back to sleep on the new cadence before
  // the test queues the line that must wait for the next event.
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
}

std::string read_diag() {
  std::ifstream f(gDiagDir + "\\GNLink\\log_upload.diag", std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// 1. Start, host token on the wire, the stream/device headers, then a token replaced while a
//    line is queued: the queue survives and goes out with the new token.
void test_token_replaced_same_identity(FakeLogServer& server) {
  std::printf("[1] token replaced, same identity\n");
  server.Clear();
  LogUploadConfig c = base_config(server, "acct/machine-A");
  c.hostToken = "HOST-TOKEN-ONE";
  c.flushIntervalMs = kHold;
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  CHECK(reason.rfind("started", 0) == 0);
  log_upload_enqueue("host", "line one");
  set_flush(c, kFast);
  CHECK(server.WaitFor(1, 2000));
  auto reqs = server.Requests();
  CHECK(reqs.size() == 1);
  if (!reqs.empty()) {
    CHECK(reqs[0].hostToken() == "HOST-TOKEN-ONE");
    CHECK(reqs[0].auth().empty());
    CHECK(reqs[0].device() == "test-device");
    CHECK(reqs[0].stream() == "host");
    CHECK(reqs[0].body == "line one\n");
  }

  // Re-registered: new token, same account/machine. A line queued before the switch still goes,
  // and goes with the NEW token.
  set_flush(c, kHold);
  log_upload_enqueue("host", "queued before switch");
  c.hostToken = "HOST-TOKEN-TWO";
  c.flushIntervalMs = kFast;
  CHECK(log_upload_configure(c, &reason));
  CHECK(reason.rfind("token replaced", 0) == 0);
  CHECK(server.WaitFor(2, 2000));
  reqs = server.Requests();
  CHECK(reqs.size() >= 2);
  if (reqs.size() >= 2) {
    CHECK(reqs[1].hostToken() == "HOST-TOKEN-TWO");
    CHECK(reqs[1].body.find("queued before switch") != std::string::npos);
  }
  const LogUploadStatus st = log_upload_status();
  CHECK(st.running && st.credentials && !st.authRejected);
  CHECK(st.sentBatches == 2);
  CHECK(st.discardedLines == 0);
  log_upload_stop();
}

// 2. Identity changed (another account signs in on this machine): what was queued under the
//    old owner is discarded, never sent with the new token; lines after the switch flow.
void test_identity_change_discards_queue(FakeLogServer& server) {
  std::printf("[2] identity change discards the queue\n");
  server.Clear();
  LogUploadConfig c = base_config(server, "alice/machine-A");
  c.sessionToken = "SESSION-ALICE";
  c.flushIntervalMs = kHold;
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("client", "alice secret line");
  CHECK(log_upload_status().droppedLines == 0);
  c.identity = "bob/machine-A";
  c.sessionToken = "SESSION-BOB";
  c.flushIntervalMs = kFast;
  CHECK(log_upload_configure(c, &reason));
  CHECK(reason.rfind("owner changed", 0) == 0);
  CHECK(log_upload_status().discardedLines == 1);
  log_upload_enqueue("client", "bob line");
  CHECK(server.WaitFor(1, 2000));
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  const auto reqs = server.Requests();
  CHECK(reqs.size() == 1);
  for (const auto& r : reqs) {
    CHECK(r.auth() == "Bearer SESSION-BOB");
    CHECK(r.body.find("alice") == std::string::npos);
  }
  log_upload_stop();
}

// 3. 401: the batch is held, sending pauses (no burn), the callback fires once, the queue stays
//    bounded while paused, and a new token sends the held batch first.
void test_401_pauses_until_new_token(FakeLogServer& server) {
  std::printf("[3] 401 pauses until a new token\n");
  server.Clear();
  server.Script({401});
  std::atomic<int> callbacks{0};
  log_upload_set_auth_rejected_callback([&callbacks] { ++callbacks; });
  LogUploadConfig c = base_config(server, "acct/machine-A");
  c.hostToken = "STALE";
  c.queueCapBytes = 600;  // small: proves the pause is memory-bounded
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("host", "first batch line");
  CHECK(server.WaitFor(1, 2000));
  CHECK(wait_until([] { return log_upload_status().authRejected; }, 2000));
  LogUploadStatus st = log_upload_status();
  CHECK(st.heldBatches == 1);
  CHECK(st.lastStatus == 401);
  CHECK(callbacks.load() == 1);
  // Paused: more lines do not produce requests, and the queue drops its oldest past the cap.
  for (int i = 0; i < 40; ++i) {
    log_upload_enqueue("host", "paused line number " + std::to_string(i) + " padding");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(server.Count() == 1);
  st = log_upload_status();
  CHECK(st.droppedLines > 0);
  CHECK(st.authRejected);
  // A fresh token: the held batch goes first, with the new header, and the pause ends.
  c.hostToken = "FRESH";
  CHECK(log_upload_configure(c, &reason));
  CHECK(reason.find("resumes after 401") != std::string::npos);
  CHECK(server.WaitFor(2, 2000));
  const auto reqs = server.Requests();
  CHECK(reqs.size() >= 2);
  if (reqs.size() >= 2) {
    CHECK(reqs[0].hostToken() == "STALE");
    CHECK(reqs[1].hostToken() == "FRESH");
    CHECK(reqs[1].body.find("first batch line") != std::string::npos);
  }
  CHECK(wait_until([] { return !log_upload_status().authRejected && log_upload_status().heldBatches == 0; }, 2000));
  CHECK(callbacks.load() == 1);
  log_upload_set_auth_rejected_callback({});
  log_upload_stop();
}

// 4. Outage: unreachable / 503 answers are retried with backoff, at most retryMaxAttempts
//    sends, then the batch is given up; later batches still flow once the server is back.
void test_transient_retry_is_bounded(FakeLogServer& server) {
  std::printf("[4] transient failures retry, bounded\n");
  server.Clear();
  server.Script({-1, 503, 503});  // three chances, all bad -> gave up
  LogUploadConfig c = base_config(server, "acct/machine-A");
  c.hostToken = "T";
  c.retryMaxAttempts = 3;
  c.retryBaseDelayMs = 40;
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("host", "doomed batch");
  CHECK(server.WaitFor(3, 3000));
  CHECK(wait_until([] { return log_upload_status().discardedBatches == 1; }, 2000));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(server.Count() == 3);  // no fourth attempt
  LogUploadStatus st = log_upload_status();
  CHECK(st.retriedBatches == 2);
  CHECK(st.failedSends == 3);
  CHECK(st.heldBatches == 0);
  CHECK(!st.authRejected);
  // Back to normal.
  log_upload_enqueue("host", "after outage");
  CHECK(server.WaitFor(4, 2000));
  CHECK(wait_until([] { return log_upload_status().sentBatches == 1; }, 2000));
  // One transient failure then success: the SAME batch is delivered on the retry.
  server.Script({503});
  log_upload_enqueue("host", "retried once");
  CHECK(server.WaitFor(6, 3000));
  const auto reqs = server.Requests();
  CHECK(reqs.size() >= 6);
  if (reqs.size() >= 6) {
    CHECK(reqs[4].body == reqs[5].body);
    CHECK(reqs[5].body.find("retried once") != std::string::npos);
  }
  log_upload_stop();
}

// 5. The server's final word (413 here) is not retried: one request, batch discarded.
void test_permanent_discards_at_once(FakeLogServer& server) {
  std::printf("[5] permanent rejection discards at once\n");
  server.Clear();
  server.Script({413});
  LogUploadConfig c = base_config(server, "acct/machine-A");
  c.hostToken = "T";
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("host", "too big");
  CHECK(server.WaitFor(1, 2000));
  CHECK(wait_until([] { return log_upload_status().discardedBatches == 1; }, 2000));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(server.Count() == 1);
  CHECK(log_upload_status().heldBatches == 0);
  log_upload_stop();
}

// 6. Signed out: credentials cleared, the queue discarded, later lines dropped (not queued for
//    whoever signs in next); a configure resumes.
void test_clear_credentials(FakeLogServer& server) {
  std::printf("[6] clear credentials\n");
  server.Clear();
  LogUploadConfig c = base_config(server, "acct/machine-A");
  c.hostToken = "T";
  c.flushIntervalMs = kHold;
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("host", "before sign-out");
  log_upload_clear_credentials("signed out");
  LogUploadStatus st = log_upload_status();
  CHECK(st.running && !st.credentials);
  CHECK(st.discardedLines == 1);
  log_upload_enqueue("host", "while signed out");
  CHECK(log_upload_status().droppedLines == 1);
  c.flushIntervalMs = kFast;
  c.hostToken = "T2";
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("host", "after sign-in");
  CHECK(server.WaitFor(1, 2000));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto reqs = server.Requests();
  CHECK(reqs.size() == 1);
  if (!reqs.empty()) {
    CHECK(reqs[0].body == "after sign-in\n");
    CHECK(reqs[0].hostToken() == "T2");
  }
  log_upload_stop();
}

// 7. A 401 that answers a send made with a token that has since been replaced is about the old
//    token: no pause, and the batch goes again with the new one.
void test_stale_401_after_replacement(FakeLogServer& server) {
  std::printf("[7] a 401 for a replaced token is not a pause\n");
  server.Clear();
  server.ScriptSteps({FakeLogServer::Step{401, 400}});  // the answer arrives 400ms later
  std::atomic<int> callbacks{0};
  log_upload_set_auth_rejected_callback([&callbacks] { ++callbacks; });
  LogUploadConfig c = base_config(server, "acct/machine-A");
  c.hostToken = "OLD";
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("host", "in flight during the swap");
  CHECK(server.WaitFor(1, 2000));  // received; the server is now sitting on it
  c.hostToken = "NEW";
  CHECK(log_upload_configure(c, &reason));  // replaced while the OLD send is in flight
  CHECK(server.WaitFor(2, 3000));           // the 401 came back, the batch went again with NEW
  const auto reqs = server.Requests();
  CHECK(reqs.size() >= 2);
  if (reqs.size() >= 2) {
    CHECK(reqs[0].hostToken() == "OLD");
    CHECK(reqs[1].hostToken() == "NEW");
    CHECK(reqs[0].body == reqs[1].body);
  }
  CHECK(wait_until([] { return log_upload_status().sentBatches == 1; }, 2000));
  CHECK(!log_upload_status().authRejected);
  CHECK(callbacks.load() == 0);
  log_upload_set_auth_rejected_callback({});
  log_upload_stop();
}

// 9. Owner fencing: a request of owner A is in flight (the server sits on it) when the owner
//    changes -- another account, another server, another device, or a sign-out followed by the
//    same account signing in again. The late answer, 401 or 500, must not pause the new session,
//    must not resurrect the batch, and A's body must never reach the new owner's destination.
void late_answer_for_previous_owner(FakeLogServer& server, FakeLogServer& server2, int status, int change) {
  const char* what = change == 0 ? "account" : change == 1 ? "server url" : change == 2 ? "device" : "sign-out + same account";
  std::printf("[9] late %d after an owner change (%s) is discarded\n", status, what);
  server.Clear();
  server2.Clear();
  server.ScriptSteps({FakeLogServer::Step{status, 400}});  // the answer arrives 400 ms later
  std::atomic<int> callbacks{0};
  log_upload_set_auth_rejected_callback([&callbacks] { ++callbacks; });
  LogUploadConfig a = base_config(server, "alice/machine-A");
  a.sessionToken = "SESSION-A";
  std::string reason;
  CHECK(log_upload_configure(a, &reason));
  log_upload_enqueue("client", "alice private line");
  CHECK(server.WaitFor(1, 2000));  // in flight; the server is sitting on it
  FakeLogServer* receiver = &server;
  LogUploadConfig b = a;
  switch (change) {
    case 0: b.identity = "bob/machine-A"; b.sessionToken = "SESSION-B"; break;
    case 1: b.directoryUrl = server2.url(); receiver = &server2; break;
    case 2: b.device = "other-device"; break;
    default: log_upload_clear_credentials("logged out"); b.sessionToken = "SESSION-A2"; break;
  }
  CHECK(log_upload_configure(b, &reason));
  CHECK(reason.rfind("owner changed", 0) == 0);
  const size_t before = receiver->Count();
  log_upload_enqueue("client", "new owner line");
  CHECK(receiver->WaitFor(before + 1, 2000));
  // Let the late answer land and anything it might wrongly trigger play out.
  CHECK(wait_until([] { return log_upload_status().foreignAnswersDiscarded == 1; }, 2000));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  for (FakeLogServer* srv : {&server, &server2}) {
    const auto reqs = srv->Requests();
    for (size_t i = 0; i < reqs.size(); ++i) {
      if (srv == &server && i == 0) continue;  // the in-flight request itself
      CHECK(reqs[i].body.find("alice private line") == std::string::npos);
      CHECK(reqs[i].auth() == "Bearer " + b.sessionToken);
      CHECK(reqs[i].device() == b.device);
    }
  }
  const LogUploadStatus st = log_upload_status();
  CHECK(!st.authRejected);
  CHECK(st.heldBatches == 0);
  CHECK(st.foreignAnswersDiscarded == 1);
  CHECK(st.sentBatches == 1);
  CHECK(callbacks.load() == 0);
  log_upload_set_auth_rejected_callback({});
  log_upload_stop();
}

// 8. The diag never contains a token, whatever happened above.
void test_diag_has_no_token() {
  std::printf("[8] diag holds no token\n");
  const std::string d = read_diag();
  CHECK(!d.empty());
  for (const char* secret : {"HOST-TOKEN-ONE", "HOST-TOKEN-TWO", "SESSION-ALICE", "SESSION-BOB", "STALE",
                             "FRESH", "SESSION-A", "SESSION-B", "SESSION-A2"}) {
    CHECK(d.find(secret) == std::string::npos);
  }
  CHECK(d.find("auth rejected status=401") != std::string::npos);
  CHECK(d.find("owner changed") != std::string::npos);
  CHECK(d.find("gave up") != std::string::npos);
  CHECK(d.find("late answer for a previous owner discarded") != std::string::npos);
}

}  // namespace

int main() {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  // Point the uploader's diag at a scratch directory so the test neither reads nor writes the
  // user's real %LOCALAPPDATA%\GNLink.
  char tmp[MAX_PATH]{};
  GetTempPathA(MAX_PATH, tmp);
  gDiagDir = std::string(tmp) + "remote60_log_upload_test_" + std::to_string(GetCurrentProcessId());
  CreateDirectoryA(gDiagDir.c_str(), nullptr);
  CreateDirectoryA((gDiagDir + "\\GNLink").c_str(), nullptr);
  SetEnvironmentVariableA("LOCALAPPDATA", gDiagDir.c_str());
  _putenv_s("LOCALAPPDATA", gDiagDir.c_str());
  _putenv_s("REMOTE60_LOG_UPLOAD", "");

  FakeLogServer server;
  FakeLogServer server2;
  if (!server.Start() || !server2.Start()) {
    std::printf("log_upload_test: could not start the fake servers\n");
    return 2;
  }
  test_token_replaced_same_identity(server);
  test_identity_change_discards_queue(server);
  test_401_pauses_until_new_token(server);
  test_transient_retry_is_bounded(server);
  test_permanent_discards_at_once(server);
  test_clear_credentials(server);
  test_stale_401_after_replacement(server);
  for (const int status : {401, 500}) {
    for (int change = 0; change < 4; ++change) late_answer_for_previous_owner(server, server2, status, change);
  }
  test_diag_has_no_token();
  server.Stop();
  server2.Stop();

  DeleteFileA((gDiagDir + "\\GNLink\\log_upload.diag").c_str());
  RemoveDirectoryA((gDiagDir + "\\GNLink").c_str());
  RemoveDirectoryA(gDiagDir.c_str());
  WSACleanup();
  if (gFailures == 0) {
    std::printf("log_upload_test: PASS\n");
    return 0;
  }
  std::printf("log_upload_test: FAIL (%d)\n", gFailures);
  return 1;
}
