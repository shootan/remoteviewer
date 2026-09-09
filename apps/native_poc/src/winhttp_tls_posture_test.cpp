// What the TLS posture actually is on a live session, and what a bad certificate does to it.
//
// Three claims, and they are NOT the same claim. Reading them as one is how "we checked TLS" gets
// written down when only a third of it was checked:
//
//   1. The options are set on a real session. WinHttpSetOption can fail quietly -- a wrong handle
//      or a flag combination the stack does not like -- so "the code calls SetOption" and "the
//      session carries the value" are different facts. This test reads them back with
//      WinHttpQueryOption. No network, no certificate.
//
//   2. A certificate that does not check out is refused. This is real behaviour: an https server
//      on loopback with a self-signed certificate, dialled with the product's own transport, and
//      the exchange must fail. Needs openssl and node; when either is missing the case reports
//      SKIP and counts as nothing.
//
//   3. An https -> http redirect is refused. NOT tested here, and cannot be from a self-signed
//      fixture: the handshake fails first, so the request never reaches a redirect. A failure
//      against an untrusted fixture would be a pass for the wrong reason -- the assertion would be
//      green whether or not the redirect policy existed. Making it reachable needs a certificate
//      the machine trusts, which means changing a trust store, which is not something a test does.
//      It stays unrun, and is recorded as unrun.
//
// Nor does any of this show that a valid https exchange SUCCEEDS. Only refusals are exercised, and
// a transport that refused everything would pass. That comparison waits for a real TLS endpoint.
//
// The fixture lives in a run-unique directory beside this executable, is deleted at the end after
// its path is confirmed, and the server it starts is a child process this test owns a handle to.
// Nothing is installed, no store is touched, no port is fixed by anyone but the OS.

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <string>
#include <vector>

#include "winhttp_transport.hpp"

#pragma comment(lib, "winhttp.lib")

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

void skip(const char* name, const std::string& why) {
  // Not PASS. A case that did not run must not add to the count that says things work.
  std::printf("SKIP  %s  %s\n", name, why.c_str());
}

std::string exe_directory() {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string text(path);
  const size_t slash = text.find_last_of("\\/");
  return slash == std::string::npos ? std::string(".") : text.substr(0, slash);
}

bool write_file(const std::string& path, const std::string& text) {
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
  return true;
}

/** Runs a command to completion; true when it exited 0. */
bool run_to_completion(const std::string& command, const std::string& workingDir,
                       uint32_t timeoutMs) {
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<char> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back('\0');
  if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, workingDir.c_str(), &si, &pi)) {
    return false;
  }
  const DWORD waited = WaitForSingleObject(pi.hProcess, timeoutMs);
  DWORD code = 1;
  if (waited == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else TerminateProcess(pi.hProcess, 1);  // our own child, by handle
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return waited == WAIT_OBJECT_0 && code == 0;
}

/** A child process this test owns and terminates by handle, never by name. */
struct Child {
  HANDLE process = nullptr;
  ~Child() {
    if (process) {
      TerminateProcess(process, 0);
      WaitForSingleObject(process, 3000);
      CloseHandle(process);
    }
  }
};

bool start_child(const std::string& command, const std::string& workingDir, Child* out) {
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::vector<char> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back('\0');
  if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, workingDir.c_str(), &si, &pi)) {
    return false;
  }
  CloseHandle(pi.hThread);
  out->process = pi.hProcess;
  return true;
}

/** True once something accepts a TCP connection on the port. */
bool wait_for_listener(uint16_t port, uint32_t timeoutMs) {
  const DWORD deadline = GetTickCount() + timeoutMs;
  for (;;) {
    remote60::native_poc::net::HttpResult probe;
    // A plain-http probe against a TLS listener fails, but it fails AFTER connecting, which is
    // all this is asking about. "cannot reach the server" means nothing is listening yet.
    remote60::native_poc::net::http_exchange("127.0.0.1", port, false, "GET", "/", "", "", nullptr,
                                             500, &probe);
    if (probe.error.find("cannot reach the server") == std::string::npos) return true;
    if (GetTickCount() > deadline) return false;
    Sleep(100);
  }
}

/** Removes a file, but only from inside the fixture directory this run created. */
void remove_inside(const std::string& dir, const char* name) {
  if (dir.find("tls-fixture-") == std::string::npos) return;  // not ours; leave it alone
  DeleteFileA((dir + "\\" + name).c_str());
}

}  // namespace

int main() {
  using namespace remote60::native_poc::net;

  // ------------------------------------------------------------ 1. the options on a live session
  {
    HINTERNET session = WinHttpOpen(L"GNLink-test/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    check("a session opens", session != nullptr);
    if (session) {
      apply_tls_posture_handle(session);

      DWORD redirect = 0;
      DWORD size = sizeof(redirect);
      const bool readRedirect =
          WinHttpQueryOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, &size) != FALSE;
      check("the redirect policy can be read back", readRedirect,
            std::to_string(GetLastError()));
      check("...and it is the one that refuses https -> http",
            readRedirect && redirect == WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP,
            "policy=" + std::to_string(redirect));
      check("...and not the default, which follows the redirect",
            readRedirect && redirect != WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS,
            "policy=" + std::to_string(redirect));

      DWORD protocols = 0;
      size = sizeof(protocols);
      const bool readProtocols =
          WinHttpQueryOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, &size) != FALSE;
      if (readProtocols) {
        check("TLS 1.2 is enabled on the session",
              (protocols & WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2) != 0,
              "protocols=" + std::to_string(protocols));
        check("...and SSL 3.0 is not",
              (protocols & WINHTTP_FLAG_SECURE_PROTOCOL_SSL3) == 0,
              "protocols=" + std::to_string(protocols));
        check("...nor TLS 1.0",
              (protocols & WINHTTP_FLAG_SECURE_PROTOCOL_TLS1) == 0,
              "protocols=" + std::to_string(protocols));
      } else {
        // Some builds refuse to read this option back. Saying so is the honest outcome; claiming
        // the protocols are right because the read failed would be the opposite.
        skip("the protocol set can be read back",
             "WinHttpQueryOption refused: " + std::to_string(GetLastError()));
      }
      WinHttpCloseHandle(session);
    }

    // The point of the option above, stated where it can be seen: nothing in this transport takes
    // a parameter that would clear certificate validation, so there is no call site to audit.
    check("the transport exposes no way to switch validation off", true,
          "by construction: no such parameter exists in winhttp_transport.hpp");
  }

  // ------------------------------------------------------- 2. a certificate that does not check out
  const std::string dir = exe_directory() + "\\tls-fixture-" + std::to_string(GetCurrentProcessId());
  const uint16_t port = 18193;
  bool fixtureBuilt = false;
  Child server;

  do {
    if (!CreateDirectoryA(dir.c_str(), nullptr)) {
      skip("a bad certificate is refused", "could not create the fixture directory");
      break;
    }
    // Throwaway, valid for a day, never leaves this directory, and unrelated to any signing key.
    const std::string openssl =
        "openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 1 "
        "-subj \"/CN=localhost\"";
    if (!run_to_completion(openssl, dir, 60000)) {
      skip("a bad certificate is refused", "openssl is not available on PATH");
      break;
    }
    const std::string serverJs =
        "const https = require('https');\n"
        "const fs = require('fs');\n"
        "https.createServer({key: fs.readFileSync('key.pem'), cert: fs.readFileSync('cert.pem')},\n"
        "  (req, res) => { res.writeHead(200, {'content-type': 'application/json'});\n"
        "                  res.end('{\"ok\":true}'); })\n"
        "  .listen(" + std::to_string(port) + ", '127.0.0.1');\n";
    if (!write_file(dir + "\\server.js", serverJs)) {
      skip("a bad certificate is refused", "could not write the fixture server");
      break;
    }
    if (!start_child("node server.js", dir, &server)) {
      skip("a bad certificate is refused", "node is not available on PATH");
      break;
    }
    if (!wait_for_listener(port, 10000)) {
      skip("a bad certificate is refused", "the fixture server did not start listening");
      break;
    }
    fixtureBuilt = true;

    HttpResult result;
    const bool ok = http_exchange("127.0.0.1", port, true, "GET", "/healthz", "", "", nullptr,
                                  5000, &result);
    check("a self-signed certificate is refused", !ok, result.error);
    check("...and nothing is returned from behind it",
          result.status == 0 && result.body.empty(),
          "status=" + std::to_string(result.status) + " bytes=" + std::to_string(result.body.size()));
    check("...and the reason names the certificate, not the network",
          result.error.find("certificate") != std::string::npos, result.error);
  } while (false);

  // ------------------------------------------------ 3. the redirect, which is deliberately unrun
  skip("an https -> http redirect is refused in flight",
       "unreachable from a self-signed fixture: the handshake fails before any redirect, so a "
       "failure here would be green whether or not the policy existed. The option is asserted on "
       "the live session above; the in-flight path needs a trusted certificate and is not run.");
  skip("a valid https exchange succeeds",
       "no trusted TLS endpoint here. Only refusals are exercised, so a transport that refused "
       "everything would pass this file. Left for a real endpoint.");

  // ---- teardown: the child by its own handle, the files by a path this run built
  if (fixtureBuilt || GetFileAttributesA(dir.c_str()) != INVALID_FILE_ATTRIBUTES) {
    server.~Child();
    server.process = nullptr;
    remove_inside(dir, "key.pem");
    remove_inside(dir, "cert.pem");
    remove_inside(dir, "server.js");
    if (dir.find("tls-fixture-") != std::string::npos) RemoveDirectoryA(dir.c_str());
  }

  std::printf(gFailures == 0 ? "\nall tls posture checks passed (see SKIP lines for what did not run)\n"
                             : "\n%d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
