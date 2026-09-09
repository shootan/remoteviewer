// The two transports must answer the same way.
//
// There are two now: the socket the directory has always used for http, and WinHTTP for https.
// Which one runs is decided by the url's scheme, so a difference between them is a bug that only
// appears on one kind of deployment -- and the https one is the deployment nobody can attach a
// debugger to. So the two are held against the same server here, byte for byte: same status, same
// body, same answer when there is no answer at all.
//
// The server is a listening socket in this process on an ephemeral loopback port. Nothing is
// installed, no port is fixed, no other process is started, and the test cannot touch a machine
// beyond this one. TLS is not exercised here -- that needs a certificate this test has no business
// creating -- so what is compared is the contract, with `secure` false on both sides. The TLS
// posture itself is asserted by reading the code, and is deliberately not switchable.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"
#include "winhttp_transport.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

/** One canned exchange: what the server sends back, or nothing at all. */
struct Script {
  std::string response;  // empty = accept and close without answering
};

/**
 * A one-connection-at-a-time http server, only as much of one as this test needs.
 *
 * Serves each queued script to the next connection in order, and keeps what it received so the
 * request either transport built can be compared as well as the reply each parsed.
 */
class TinyServer {
 public:
  bool Start(std::vector<Script> scripts) {
    scripts_ = std::move(scripts);
    listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only: nothing off this machine
    addr.sin_port = 0;                              // the OS picks a free port
    if (bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    if (getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    port_ = ntohs(addr.sin_port);
    if (::listen(listen_, 4) != 0) return false;
    worker_ = std::thread([this] { Serve(); });
    return true;
  }

  void Stop() {
    stopping_ = true;
    if (listen_ != INVALID_SOCKET) {
      closesocket(listen_);
      listen_ = INVALID_SOCKET;
    }
    if (worker_.joinable()) worker_.join();
  }

  uint16_t port() const { return port_; }
  std::string TakeRequest() {
    std::string out;
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (!requests_.empty()) {
        out = requests_.front();
        requests_.erase(requests_.begin());
      }
    }
    return out;
  }

 private:
  void Serve() {
    size_t next = 0;
    while (!stopping_) {
      SOCKET s = accept(listen_, nullptr, nullptr);
      if (s == INVALID_SOCKET) return;

      // Read the head, then whatever Content-Length says is still coming.
      std::string raw;
      char buf[4096];
      size_t headEnd = std::string::npos;
      size_t want = 0;
      for (;;) {
        if (headEnd == std::string::npos) {
          headEnd = raw.find("\r\n\r\n");
          if (headEnd != std::string::npos) {
            const size_t at = raw.find("Content-Length:");
            want = at == std::string::npos ? 0 : strtoul(raw.c_str() + at + 15, nullptr, 10);
            if (raw.size() >= headEnd + 4 + want) break;
          }
        } else if (raw.size() >= headEnd + 4 + want) {
          break;
        }
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
      }
      {
        std::lock_guard<std::mutex> lk(mu_);
        requests_.push_back(raw);
      }

      const std::string& reply = next < scripts_.size() ? scripts_[next].response : kEmpty;
      ++next;
      size_t sent = 0;
      while (sent < reply.size()) {
        const int n = send(s, reply.data() + sent, static_cast<int>(reply.size() - sent), 0);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
      }
      shutdown(s, SD_SEND);
      closesocket(s);
    }
  }

  static const std::string kEmpty;
  SOCKET listen_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::vector<Script> scripts_;
  std::vector<std::string> requests_;
  std::mutex mu_;
  std::thread worker_;
  std::atomic<bool> stopping_{false};
};

const std::string TinyServer::kEmpty;

std::string http_response(int status, const std::string& body, bool withLength = true) {
  std::string head = "HTTP/1.1 " + std::to_string(status) + " X\r\n";
  if (withLength) head += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  head += "Connection: close\r\n\r\n";
  return head + body;
}

/** What a caller ends up with, whichever transport carried it. */
struct Answer {
  bool ok = false;
  uint32_t status = 0;
  std::string body;
};

Answer viaSocket(uint16_t port, const std::string& body) {
  Answer a;
  a.ok = remote60::native_poc::directory::http_post("127.0.0.1", port, false, "/api/logs",
                                                    "application/json", "X-Test: 1\r\n", body,
                                                    &a.status, &a.body);
  return a;
}

Answer viaWinHttp(uint16_t port, const std::string& body) {
  remote60::native_poc::net::HttpResult r;
  const bool ok = remote60::native_poc::net::http_exchange(
      "127.0.0.1", port, false, "POST", "/api/logs", "X-Test: 1\r\n", body, "application/json",
      5000, &r);
  Answer a;
  a.ok = ok;
  a.status = r.status;
  a.body = r.body;
  return a;
}

std::string describe(const Answer& a, const Answer& b) {
  return "socket{" + std::to_string(a.ok) + "," + std::to_string(a.status) + "," +
         std::to_string(a.body.size()) + "} winhttp{" + std::to_string(b.ok) + "," +
         std::to_string(b.status) + "," + std::to_string(b.body.size()) + "}";
}

/** Runs the same request through both transports against the same scripted reply. */
void compare(const char* name, const std::string& reply, const std::string& requestBody,
             bool expectOk, uint32_t expectStatus, const std::string& expectBody) {
  TinyServer server;
  if (!server.Start({Script{reply}, Script{reply}})) {
    check(name, false, "the test server did not start");
    return;
  }
  const Answer a = viaSocket(server.port(), requestBody);
  const Answer b = viaWinHttp(server.port(), requestBody);
  server.Stop();

  const std::string detail = describe(a, b);
  check((std::string(name) + ": both report the same reached/not-reached").c_str(), a.ok == b.ok,
        detail);
  check((std::string(name) + ": both report the same status").c_str(), a.status == b.status,
        detail);
  check((std::string(name) + ": both return the same body").c_str(), a.body == b.body, detail);
  check((std::string(name) + ": and it is what the server sent").c_str(),
        a.ok == expectOk && a.status == expectStatus && a.body == expectBody, detail);
}

}  // namespace

int main() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }

  compare("a plain 200", http_response(200, "{\"ok\":true}"), "{\"a\":1}", true, 200,
          "{\"ok\":true}");

  // The server's own wording is what the caller shows. A transport that dropped the body on a
  // non-200 would leave "something went wrong" and nothing to act on.
  compare("a 404 with the server's reason", http_response(404, "{\"error\":\"no such host\"}"),
          "{\"a\":1}", true, 404, "{\"error\":\"no such host\"}");
  compare("a 401", http_response(401, "{\"error\":\"bad token\"}"), "{\"a\":1}", true, 401,
          "{\"error\":\"bad token\"}");
  compare("a 204 with no body", http_response(204, ""), "{\"a\":1}", true, 204, "");

  // Bodies that are not small and not ascii: a length or an encoding handled differently by the
  // two would be a difference only https deployments would ever see.
  compare("a body bigger than one read", http_response(200, std::string(100000, 'x')), "{\"a\":1}",
          true, 200, std::string(100000, 'x'));
  compare("utf-8 survives byte for byte",
          http_response(200, "{\"name\":\"\xed\x95\x9c\xea\xb8\x80\"}"), "{\"a\":1}", true, 200,
          "{\"name\":\"\xed\x95\x9c\xea\xb8\x80\"}");

  // Past the shared limit both must refuse, and refuse the same way. They used to cap at three
  // different sizes and hand back the truncated prefix as if it were the answer -- the caller then
  // reported malformed json, which is true of what it was given and false about the server.
  {
    const std::string huge(remote60::native_poc::net::kMaxHttpResponseBytes + 1024, 'y');
    compare("a response past the limit", http_response(200, huge), "{\"a\":1}", false, 0, "");
  }

  // No Content-Length: the body ends when the connection does.
  compare("a reply delimited by the close", http_response(200, "{\"ok\":true}", false),
          "{\"a\":1}", true, 200, "{\"ok\":true}");

  // Accepted and then nothing. Both must call this not-reached rather than inventing a status,
  // because the caller's retry rules turn on exactly that difference.
  compare("a server that answers nothing", "", "{\"a\":1}", false, 0, "");

  // Nothing listening on the port at all.
  {
    TinyServer server;
    server.Start({});
    const uint16_t port = server.port();
    server.Stop();  // the port is now free, and almost certainly unused
    const Answer a = viaSocket(port, "{}");
    const Answer b = viaWinHttp(port, "{}");
    check("a closed port is not-reached on both", !a.ok && !b.ok, describe(a, b));
    check("...and neither invents a status", a.status == 0 && b.status == 0, describe(a, b));
  }

  // The headers the caller passed have to arrive, or the server sees an unauthenticated request
  // and the difference shows up as a 401 on one transport only.
  {
    TinyServer server;
    server.Start({Script{http_response(200, "{}")}, Script{http_response(200, "{}")}});
    (void)viaSocket(server.port(), "{\"a\":1}");
    const std::string first = server.TakeRequest();
    (void)viaWinHttp(server.port(), "{\"a\":1}");
    const std::string second = server.TakeRequest();
    server.Stop();

    check("the socket path sends the caller's extra header",
          first.find("X-Test: 1") != std::string::npos);
    check("the winhttp path sends it too", second.find("X-Test: 1") != std::string::npos);
    check("both send the content type",
          first.find("application/json") != std::string::npos &&
              second.find("application/json") != std::string::npos);
    check("both send the body",
          first.find("{\"a\":1}") != std::string::npos &&
              second.find("{\"a\":1}") != std::string::npos);
    check("both send it as POST to the same path",
          first.rfind("POST /api/logs", 0) == 0 && second.rfind("POST /api/logs", 0) == 0);
  }

  WSACleanup();
  std::printf(gFailures == 0 ? "\nall http contract checks passed\n" : "\n%d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
