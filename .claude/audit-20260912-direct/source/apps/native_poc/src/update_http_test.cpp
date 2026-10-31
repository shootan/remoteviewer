// The update client's TLS posture, tested where it can actually be tested.
//
// Two rules carry the weight here -- no cleartext, and no redirect down to cleartext -- and both
// live in ordinary functions with no I/O, so they can be checked by calling them rather than by
// standing up a certificate authority.
//
// One case does touch the network, and it is the one worth touching it for: a real listening
// socket that speaks plain HTTP, addressed as https://. The client must fail rather than fall
// back. Nothing is disabled to make that work, which is the point -- if the client ever grew a
// "just this once" bypass, this case would start passing for the wrong reason.
//
// No test here reaches the real directory server or the NAS.

#include "update_http.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

namespace {

using namespace remote60::native_poc::update;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::string narrow(const std::wstring& w) {
  return std::string(w.begin(), w.end());
}

/**
 * A socket that accepts one connection and answers with plain HTTP.
 *
 * It exists to be addressed as https:// and refused. Bound to loopback on an ephemeral port, so
 * it cannot be reached from outside this machine and cannot collide with anything.
 */
class CleartextListener {
 public:
  bool start() {
    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // let the OS choose
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    if (getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    port_ = ntohs(addr.sin_port);
    if (listen(sock_, 1) != 0) return false;

    thread_ = std::thread([this] {
      while (!stop_.load()) {
        SOCKET client = accept(sock_, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        ++accepted_;
        // Deliberately cleartext. A TLS client must not be able to make anything of this.
        const char* body = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
        send(client, body, static_cast<int>(strlen(body)), 0);
        closesocket(client);
      }
    });
    return true;
  }
  uint16_t port() const { return port_; }
  int accepted() const { return accepted_.load(); }
  void stop() {
    stop_.store(true);
    if (sock_ != INVALID_SOCKET) closesocket(sock_);
    if (thread_.joinable()) thread_.join();
    sock_ = INVALID_SOCKET;
  }
  ~CleartextListener() { stop(); }

 private:
  SOCKET sock_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<int> accepted_{0};
};

}  // namespace

int main() {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  // ---------------------------------------------------------------- scheme

  {
    HttpsUrl url;
    std::string err;
    check("http:// is refused", !parse_https_url("http://example.org/m.txt", &url, &err), err);
    check("and says so specifically", err.find("http://") != std::string::npos, err);

    check("HTTP:// in capitals is also refused",
          !parse_https_url("HTTP://example.org/m.txt", &url, &err), err);
    check("a bare host is refused", !parse_https_url("example.org/m.txt", &url, &err), err);
    check("ftp is refused", !parse_https_url("ftp://example.org/m.txt", &url, &err), err);
    check("file is refused", !parse_https_url("file:///C:/m.txt", &url, &err), err);
    check("empty is refused", !parse_https_url("", &url, &err), err);
    check("https with no host is refused", !parse_https_url("https://", &url, &err), err);
    // A URL that reads as one host and resolves as another is a classic; refused rather than parsed.
    check("credentials in the URL are refused",
          !parse_https_url("https://evil.example@real.example/m", &url, &err), err);
  }

  {
    HttpsUrl url;
    std::string err;
    check("a plain https URL parses", parse_https_url("https://example.org/m.txt", &url, &err), err);
    check("host", narrow(url.host) == "example.org", narrow(url.host));
    check("default port is 443", url.port == 443, std::to_string(url.port));
    check("path", narrow(url.path) == "/m.txt", narrow(url.path));

    check("an explicit port parses", parse_https_url("https://example.org:8443/a/b?c=d", &url, &err), err);
    check("port", url.port == 8443, std::to_string(url.port));
    check("path keeps the query", narrow(url.path) == "/a/b?c=d", narrow(url.path));

    check("no path becomes /", parse_https_url("https://example.org", &url, &err), err);
    check("path is /", narrow(url.path) == "/", narrow(url.path));

    check("port 0 is refused", !parse_https_url("https://example.org:0/", &url, &err), err);
    check("port 70000 is refused", !parse_https_url("https://example.org:70000/", &url, &err), err);
    check("a non-numeric port is refused", !parse_https_url("https://example.org:abc/", &url, &err), err);
    // The '@' after the first slash is part of the path, not credentials.
    check("an @ in the path is fine", parse_https_url("https://example.org/a@b", &url, &err), err);
  }

  // ---------------------------------------------------------------- redirects

  {
    std::string err;
    check("https to https is allowed",
          redirect_is_allowed("https://a.example/m", "https://b.example/m", &err), err);
    check("https to http is REFUSED",
          !redirect_is_allowed("https://a.example/m", "http://b.example/m", &err), err);
    check("and the refusal names the target",
          err.find("redirect target refused") != std::string::npos, err);
    check("redirect to a bare host is refused",
          !redirect_is_allowed("https://a.example/m", "b.example/m", &err), err);
    check("redirect to file:// is refused",
          !redirect_is_allowed("https://a.example/m", "file:///C:/x", &err), err);
    check("a redirect from a non-https origin is refused too",
          !redirect_is_allowed("http://a.example/m", "https://b.example/m", &err), err);
  }

  // ---------------------------------------------------------------- fetch entry points

  {
    std::string out;
    std::string err;
    check("https_get_text refuses http:// without connecting",
          https_get_text("http://127.0.0.1:1/m", 4096, &out, &err) == FetchStatus::BadUrl, err);
    check("and wrote nothing", out.empty());

    check("https_get_file refuses http:// without connecting",
          https_get_file("http://127.0.0.1:1/a", L"should-not-exist.tmp", 1024, &err) ==
              FetchStatus::BadUrl,
          err);
    check("and created no file",
          GetFileAttributesW(L"should-not-exist.tmp") == INVALID_FILE_ATTRIBUTES);
  }

  // ---------------------------------------------------------------- a real socket, no TLS

  {
    CleartextListener listener;
    check("cleartext listener started", listener.start());
    const std::string url = "https://127.0.0.1:" + std::to_string(listener.port()) + "/m.txt";

    std::string out;
    std::string err;
    const FetchStatus status = https_get_text(url, 4096, &out, &err);
    // The server speaks plain HTTP; a TLS client must not be able to make anything of it. What
    // matters is that this does not succeed -- and that it does not succeed by quietly speaking
    // cleartext, which the empty body confirms.
    check("addressing a cleartext server as https fails", status != FetchStatus::Ok,
          std::string(fetch_status_name(status)) + " " + err);
    check("nothing was returned from it", out.empty(), out);
    listener.stop();
  }

  {
    // Nothing listening at all. Distinct from the case above: this one is a connect failure, and
    // it should not be reported as a URL problem.
    std::string out;
    std::string err;
    const FetchStatus status = https_get_text("https://127.0.0.1:9/m.txt", 4096, &out, &err);
    check("an unreachable https endpoint is a connect failure, not a bad URL",
          status == FetchStatus::ConnectFailed, std::string(fetch_status_name(status)) + " " + err);
  }

  WSACleanup();

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
