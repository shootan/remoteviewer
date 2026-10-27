#include "viewer_managed_standby.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "directory_client.hpp"
#include "directory_session_client.hpp"
#include "managed_viewer_ipc.hpp"
#include "poc_protocol.hpp"
#include "standby_directory.hpp"
#include "standby_file_store.hpp"
#include "standby_runner.hpp"
#include "viewer_constants.hpp"

#pragma comment(lib, "bcrypt.lib")

namespace remote60::native_poc::viewer {

namespace {

using managed_ipc::Channel;
using managed_ipc::Message;
using managed_ipc::Type;

uint64_t mono_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool random_bytes(uint8_t* b, size_t n) {
  return BCryptGenRandom(nullptr, b, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

std::string hex(const uint8_t* b, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < n; ++i) {
    out.push_back(d[b[i] >> 4]);
    out.push_back(d[b[i] & 15]);
  }
  return out;
}

// The generation file's name: the FULL SHA-256 of the PC key (Codex ①) -- never the raw origin/account/host text.
std::string sha256_hex(const std::string& text) {
  uint8_t digest[32] = {};
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
            BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0 &&
            BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), static_cast<ULONG>(text.size()),
                           0) == 0 &&
            BCryptFinishHash(h, digest, sizeof(digest), 0) == 0;
  if (h) BCryptDestroyHash(h);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  return ok ? hex(digest, sizeof(digest)) : std::string();
}

std::string fresh_token() {
  uint8_t b[16] = {};
  return random_bytes(b, sizeof(b)) ? hex(b, sizeof(b)) : std::string();
}

// What the shell may show (model A states). "ready" only for an entry the Host admitted and that Ping keeps alive:
// Parked with an entry id. Idle / waiting for a capability / handed off say nothing more than "preparing".
std::string state_name(const StandbyRunner::Status& s) {
  if (!s.exists) return "gone";
  switch (s.state) {
    case StandbyEntryClient::State::Parked: return s.entryId != 0 ? "ready" : "preparing";
    case StandbyEntryClient::State::Activating:
    case StandbyEntryClient::State::Active: return "active";
    case StandbyEntryClient::State::Unsupported: return "unsupported";
    case StandbyEntryClient::State::Waiting: return "retry";
    default: return "preparing";
  }
}

// A session that ended without its window's X (the host gone, a takeover, a failed connect) leaves the window it
// made: in an ordinary viewer the process ending took it away. Here the process goes on, so it is closed and its
// messages drained (WM_DESTROY posts the quit that ends this drain).
void close_session_window(ViewerContext& ctx) {
  if (ctx.session.hwnd && IsWindow(ctx.session.hwnd)) DestroyWindow(ctx.session.hwnd);
  MSG m;
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
    if (m.message == WM_QUIT) break;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

void log(const std::string& line) { std::cout << "[viewer][standby] " << line << std::endl; }

// How long after an activation's session ended this viewer says "returning" before "ready".
// ka-pc-impl r2 (Codex D1): this is NOT a proof that the Host has given the grant back -- nothing on the existing wire
// says that (a Pong only says the entry is alive; SessionOver is local). It is a display interval: the Host ends a
// session after 10 s of control silence (host_startup_control.cpp, UdpControlLink 10000), so for about that long
// the previous connection may still be being cleaned up there. "ready" means: the local hand-back is done and the
// lease's entry is alive -- not "the Host has finished cleaning up". A tap during "returning" is allowed (the device
// takes the grant back with a newer generation), and so is a release.
constexpr uint64_t kReturningMs = 12000;

}  // namespace

int run_managed_standby(const std::function<bool(ViewerContext&)>& load, const std::function<int(ViewerContext&)>& run) {
  auto base = std::make_unique<ViewerContext>();
  if (!load(*base)) return 2;
  HANDLE rd = managed_ipc::handle_from_arg(base->args.ipcReadHandle);
  HANDLE wr = managed_ipc::handle_from_arg(base->args.ipcWriteHandle);
  if (!rd || !wr) {
    std::cerr << "[viewer][standby] --ipc-read / --ipc-write are not handles this process holds" << std::endl;
    return 2;
  }
  base.reset();
  Channel ch;
  ch.Start(rd, wr);

  // Init first, bounded: nothing happens before the shell has said who this is.
  Message init;
  const uint64_t initDeadline = mono_ms() + 15000;
  bool gotInit = false;
  while (!gotInit && mono_ms() < initDeadline && ch.open()) {
    Message m;
    if (ch.Receive(&m, 200) && m.type == Type::Init) {
      init = m;
      gotInit = true;
    }
  }
  if (!gotInit) {
    log("no Init from the shell (" + ch.why() + "); exiting");
    return 2;
  }
  const std::string url = init.get("url"), hostId = init.get("hostId"), key = init.get("key");
  const std::string inc = init.get("inc"), epoch = init.get("epoch"), genDir = init.get("genDir");
  std::mutex tokenMu;
  std::string token = init.get("session");
  uint64_t tokenVersion = init.num("tokenVersion");
  if (url.empty() || hostId.empty() || key.empty() || inc.empty() || epoch.empty() || genDir.empty() || token.empty()) {
    log("an incomplete Init; exiting");
    return 2;
  }
  auto stamped = [&](Type t) {
    Message m;
    m.type = t;
    m.fields["key"] = key;
    m.fields["inc"] = inc;
    m.fields["epoch"] = epoch;
    return m;
  };
  auto mine = [&](const Message& m) { return m.get("key") == key && m.get("inc") == inc && m.get("epoch") == epoch; };
  ch.Send(stamped(Type::Hello));

  auto fail = [&](const std::string& reason, int rc) {
    Message s = stamped(Type::State);
    s.fields["state"] = "failed";
    s.fields["reason"] = reason;
    ch.Send(s);
    Message b = stamped(Type::Bye);
    b.fields["reason"] = reason;
    ch.Send(b);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    log("cannot keep this PC: " + reason);
    return rc;
  };

  // The generation store of this PC key: one live writer (the store's own lock file), the full key hash as its name.
  const std::string keyHash = sha256_hex(key);
  if (keyHash.empty()) return fail("no sha-256", 3);
  {
    // UTF-8 -> UTF-16: the profile folder may be non-ASCII (a Korean user name); the store opens its files the same way.
    const int n = MultiByteToWideChar(CP_UTF8, 0, genDir.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<size_t>(n) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, genDir.c_str(), -1, &w[0], n);
    CreateDirectoryW(w.c_str(), nullptr);
  }
  FileGenerationStore store(genDir + "\\" + keyHash + ".gen");
  if (!store.owns()) return fail("generation store held by another process", 3);
  GenerationAllocator gens(store, random_bytes);

  // Where observations go: the directory's health metadata (the same rule as the ordinary directory path).
  std::string dirHost, urlError;
  uint16_t httpPort = 0;
  bool secure = false;
  if (!directory::parse_directory_url(url, &dirHost, &httpPort, &urlError, &secure)) return fail("bad url", 3);
  directory::ObserveEndpoint advertised;
  std::string healthError;
  (void)directory_observe_from_health(url, &advertised, &healthError);
  const uint16_t observePort = directory::observe_port_for(advertised, httpPort, secure);
  if (observePort == 0) return fail("no observe endpoint", 3);

  StandbyDirectoryConfig cfg;
  cfg.url = url;
  cfg.session = [&tokenMu, &token] {
    std::lock_guard<std::mutex> lock(tokenMu);
    return token;
  };
  cfg.hostId = hostId;
  cfg.observeHost = advertised.host.empty() ? dirHost : advertised.host;
  cfg.observePort = observePort;
  cfg.freshToken = fresh_token;
  cfg.note = [](const std::string& what) { log("capability: " + what); };
  const uint32_t features = kUdpFeatureVideoFec | kUdpFeatureVideoFecInterleaved | kUdpFeatureVideoNack;
  StandbyRunner runner(gens, features, [cfg](const std::string&) { return standby_directory_capability(cfg); }, mono_ms);
  if (!runner.Start(key)) return fail("the lease could not be opened", 3);
  log("standing by for " + hostId);

  std::string lastState;
  uint64_t lastStateAt = 0;
  uint64_t returningUntil = 0;
  std::string exitReason;
  auto publish = [&](bool force) {
    const StandbyRunner::Status s = runner.StatusOf(key);
    std::string name = state_name(s);
    if (name == "ready" && mono_ms() < returningUntil) name = "returning";
    if (!force && name == lastState && mono_ms() - lastStateAt < 2000) return;
    Message m = stamped(Type::State);
    m.fields["state"] = name;
    m.fields["lost"] = std::to_string(s.lost);
    m.fields["entry"] = s.entryId != 0 ? "1" : "0";
    ch.Send(m);
    if (name != lastState) log("state " + name);
    lastState = name;
    lastStateAt = mono_ms();
  };
  auto renew = [&](const Message& m) {
    const uint64_t v = m.num("tokenVersion");
    std::lock_guard<std::mutex> lock(tokenMu);
    if (v > tokenVersion && !m.get("session").empty()) {  // a late renewal never replaces a newer token
      token = m.get("session");
      tokenVersion = v;
    }
  };

  // One activation: one ordinary session on the lease's socket.
  auto activate = [&](const std::string& op) -> bool /* go on standing by */ {
    const uint64_t t0 = mono_ms();
    ClientSessionConnectArgs args;
    uint64_t leaseOp = 0;
    if (!runner.TakeActivation(key, &args, &leaseOp)) {
      Message e = stamped(Type::SessionEnded);
      e.fields["op"] = op;
      e.fields["rc"] = "0";
      e.fields["reason"] = "not-ready";
      ch.Send(e);
      return true;
    }
    auto ctx = std::make_unique<ViewerContext>();
    if (!load(*ctx)) {
      runner.Abandon(key, leaseOp, &args);
      Message e = stamped(Type::SessionEnded);
      e.fields["op"] = op;
      e.fields["rc"] = "2";
      e.fields["reason"] = "config";
      ch.Send(e);
      return true;
    }
    ctx->standby = &args;
    ctx->session.milestone = [&ch, &stamped, op, t0](const char* what) {
      Message m = stamped(std::string(what) == "picker-ready" ? Type::PickerReady : Type::Presented);
      m.fields["op"] = op;
      m.fields["ms"] = std::to_string(mono_ms() - t0);
      ch.Send(m);
    };
    std::atomic<bool> over{false};
    std::atomic<bool> stopAfter{false};
    std::atomic<bool> parkAsked{false};
    ViewerContext* live = ctx.get();
    // ka-pc-impl r2 (Codex N4): the owner fence first -- input and the clipboard cut on the UI thread, then the
    // window's own WM_CLOSE teardown. A connect still in progress ends; a session whose window does not exist yet
    // never starts (run_session_body checks connectCancelled before the window and after the connect).
    auto closeIt = [live] {
      live->connectCancelled.store(true, std::memory_order_release);
      HWND h = live->session.hwnd;
      if (h && IsWindow(h)) PostMessageW(h, kMsgOwnerFence, 0, 0);
    };
    // The channel during the session: Park / Release / Shutdown close the window (the same path as its X), a
    // renewed token is kept for the lease, a second Activate is nothing (this one is running).
    std::thread watcher([&] {
      while (!over.load()) {
        Message m;
        if (!ch.Receive(&m, 100)) {
          if (!ch.open() && !stopAfter.exchange(true)) {
            exitReason = "shell gone";
            closeIt();
          }
          continue;
        }
        if (!mine(m)) continue;
        if (m.type == Type::Park && m.get("op") == op) {
          parkAsked.store(true);
          closeIt();
        } else if (m.type == Type::Release || m.type == Type::Shutdown) {
          if (!stopAfter.exchange(true)) exitReason = m.type == Type::Release ? "release" : "shutdown";
          closeIt();
        } else if (m.type == Type::TokenRenewed) {
          renew(m);
        }
      }
    });
    log("activation op=" + op);
    const int rc = run(*ctx);  // run_session closes and drains the session's window on every way out
    over.store(true);
    watcher.join();
    close_session_window(*ctx);  // a second, harmless pass (nothing left after run_session)
    // The session's threads have joined (run returned after shutdown_viewer): the lease takes its socket back.
    const bool taken = ctx->standbyTaken;
    if (taken) {
      runner.SessionOver(key, leaseOp);
      returningUntil = mono_ms() + kReturningMs;
    } else {
      runner.Abandon(key, leaseOp, &args);
    }
    // ka-pc-impl r2 (Codex N6): "connected" is the session's real connect (handshake + control, the line the
    // ordinary path also counts) -- not that the lease's socket was taken. A handshake that failed after the take
    // is a failed connect (rc 6), and says so.
    const bool connected = ctx->sessionConnected;
    Message e = stamped(Type::SessionEnded);
    e.fields["op"] = op;
    e.fields["rc"] = std::to_string(rc);
    e.fields["reason"] = parkAsked.load() ? "park"
                         : stopAfter.load() ? exitReason
                         : !connected ? "connect-failed"
                         : (rc == 0 ? "closed" : "ended");
    e.fields["connected"] = connected ? "1" : "0";
    e.fields["taken"] = taken ? "1" : "0";
    ch.Send(e);
    log("activation op=" + op + " ended rc=" + std::to_string(rc) + " connected=" + (connected ? "1" : "0") +
        " taken=" + (taken ? "1" : "0"));
    ctx.reset();
    publish(true);
    return !stopAfter.load();
  };

  publish(true);
  for (;;) {
    publish(false);
    if (!ch.open()) {
      exitReason = "shell gone";
      break;
    }
    Message m;
    if (!ch.Receive(&m, 200)) continue;
    if (!mine(m)) {
      log("a message for another owner or child, ignored");
      continue;
    }
    if (m.type == Type::Activate) {
      if (!activate(m.get("op"))) break;
    } else if (m.type == Type::Release || m.type == Type::Shutdown) {
      exitReason = m.type == Type::Release ? "release" : "shutdown";
      break;
    } else if (m.type == Type::TokenRenewed) {
      renew(m);
    }
  }
  runner.Release(key);
  Message b = stamped(Type::Bye);
  b.fields["reason"] = exitReason;
  ch.Send(b);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  log("ending: " + exitReason);
  return 0;
}

}  // namespace remote60::native_poc::viewer
